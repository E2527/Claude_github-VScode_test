"""3DS なしで受信側を試すための疑似送信ツール.

    python -m robot3ds.fake_sender 127.0.0.1

R ボタン(デッドマン)を押しながら「前進 -> 右旋回 -> 後退 -> 停止」を繰り返す。
"""

from __future__ import annotations

import argparse
import socket
import time

from .protocol import DEFAULT_PORT, FLAG_STOP, Button, build

SCRIPT = [
    ("forward", (0, 156)),
    ("turn right", (156, 0)),
    ("backward", (0, -156)),
    ("idle", (0, 0)),
]


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--rate", type=float, default=30.0)
    ap.add_argument("--step", type=float, default=2.0, help="各動作の秒数")
    args = ap.parse_args(argv)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    dest = (args.host, args.port)
    seq = 0
    try:
        while True:
            for name, circle in SCRIPT:
                print(name)
                end = time.monotonic() + args.step
                while time.monotonic() < end:
                    sock.sendto(build(seq=seq, buttons=Button.R, circle=circle), dest)
                    seq += 1
                    time.sleep(1.0 / args.rate)
    except KeyboardInterrupt:
        sock.sendto(build(seq=seq, flags=FLAG_STOP), dest)


if __name__ == "__main__":
    main()
