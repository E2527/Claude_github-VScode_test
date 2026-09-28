"""3DS からの UDP パケットを受信してロボットの走行指令に変換する.

使い方:
    python -m robot3ds                      # 受信内容を表示するだけ
    python -m robot3ds --serial /dev/ttyUSB0  # マイコンへ走行指令を送る
"""

from __future__ import annotations

import argparse
import logging
import socket
import time
from typing import Callable

from .drive import DriveCommand, DriveConfig, to_drive_command
from .protocol import DEFAULT_PORT, PACKET_SIZE, Button, ControllerState, PacketError, parse, seq_newer

log = logging.getLogger(__name__)

STALE_WINDOW = 1000  # この範囲内で seq が戻ったパケットは遅延到着とみなして捨てる


class ControllerReceiver:
    """UDP でパケットを受け取り、最新の状態を保持する.

    `timeout` 秒パケットが途絶えたら state は None になる (フェイルセーフ)。
    """

    def __init__(self, host: str = "0.0.0.0", port: int = DEFAULT_PORT, timeout: float = 0.5):
        self.timeout = timeout
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.sock.settimeout(0.05)
        self._state: ControllerState | None = None
        self._last_seq: int | None = None
        self._last_rx = 0.0
        self.sender: tuple[str, int] | None = None

    @property
    def port(self) -> int:
        return self.sock.getsockname()[1]

    @property
    def state(self) -> ControllerState | None:
        if self._state is None or time.monotonic() - self._last_rx > self.timeout:
            return None
        return self._state

    def poll(self) -> ControllerState | None:
        """届いているパケットをすべて読み、最新の状態を返す."""
        while True:
            try:
                data, addr = self.sock.recvfrom(PACKET_SIZE + 64)
            except (socket.timeout, BlockingIOError):
                break
            try:
                pkt = parse(data)
            except PacketError as e:
                log.debug("drop packet from %s: %s", addr, e)
                continue
            # 同じ送信元からの古い/重複パケットは捨てる。
            # 大きく巻き戻った場合は 3DS アプリの再起動とみなして受け入れる。
            if (self.sender == addr and self._last_seq is not None
                    and not seq_newer(pkt.seq, self._last_seq)
                    and (self._last_seq - pkt.seq) & 0xFFFFFFFF < STALE_WINDOW):
                continue
            if self.sender != addr:
                log.info("controller connected: %s:%d", *addr)
                self.sender = addr
            self._state = pkt
            self._last_seq = pkt.seq
            self._last_rx = time.monotonic()
            self.sock.settimeout(0)  # 残りはノンブロッキングで読み切る
        self.sock.settimeout(0.05)
        return self.state

    def close(self) -> None:
        self.sock.close()


class SerialDriver:
    """走行指令をシリアルでマイコン (Arduino など) に送る.

    1 行 1 指令: "M <left> <right>\\n" (それぞれ -255..255 の整数)
    """

    def __init__(self, port: str, baud: int = 115200):
        import serial  # pyserial

        self.ser = serial.Serial(port, baud, timeout=0)

    def send(self, cmd: DriveCommand) -> None:
        left = round(cmd.left * 255)
        right = round(cmd.right * 255)
        self.ser.write(f"M {left} {right}\n".encode())

    def close(self) -> None:
        self.send(DriveCommand.stop())
        self.ser.close()


def run(
    receiver: ControllerReceiver,
    cfg: DriveConfig,
    on_command: Callable[[DriveCommand, ControllerState | None], None],
    rate_hz: float = 30.0,
) -> None:
    period = 1.0 / rate_hz
    while True:
        start = time.monotonic()
        state = receiver.poll()
        on_command(to_drive_command(state, cfg), state)
        time.sleep(max(0.0, period - (time.monotonic() - start)))


def _button(name: str) -> int | None:
    if name.lower() == "none":
        return None
    try:
        return Button.NAMES[name.upper()]
    except KeyError:
        raise argparse.ArgumentTypeError(f"unknown button {name!r}") from None


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description="3DS robot controller receiver")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--timeout", type=float, default=0.5, help="フェイルセーフ停止までの秒数")
    ap.add_argument("--deadman", type=_button, default=Button.R, help="押している間だけ動くボタン (none で無効)")
    ap.add_argument("--turbo", type=_button, default=Button.L, help="押している間最高速度になるボタン")
    ap.add_argument("--speed", type=float, default=0.5, help="通常時の速度 (0..1)")
    ap.add_argument("--serial", help="走行指令を送るシリアルポート (例: /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)

    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")

    cfg = DriveConfig(deadman=args.deadman, turbo=args.turbo, normal_speed=args.speed)
    receiver = ControllerReceiver(args.host, args.port, args.timeout)
    driver = SerialDriver(args.serial, args.baud) if args.serial else None
    log.info("listening on udp %s:%d", args.host, receiver.port)

    was_connected = False

    def on_command(cmd: DriveCommand, state: ControllerState | None) -> None:
        nonlocal was_connected
        if driver:
            driver.send(cmd)
        if state is None:
            if was_connected:
                log.warning("controller lost -> STOP")
            was_connected = False
            return
        was_connected = True
        print(f"\rseq={state.seq:<8} L={cmd.left:+.2f} R={cmd.right:+.2f} "
              f"buttons={','.join(state.pressed_names()) or '-':<24}", end="", flush=True)

    try:
        run(receiver, cfg, on_command)
    except KeyboardInterrupt:
        print()
    finally:
        if driver:
            driver.close()
        receiver.close()


if __name__ == "__main__":
    main()
