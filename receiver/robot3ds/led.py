"""3DS の入力で Raspberry Pi につないだ LED を光らせる.

    python3 -m robot3ds.led

配線 (GPIO は BCM 番号。LED の長い足 (+) を GPIO 側にする):
    GPIO17 (11番ピン) ── 330Ω ── LED1 ── GND (9番ピン)   A ボタンを押している間点灯
    GPIO27 (13番ピン) ── 330Ω ── LED2 ── GND (14番ピン)  明るさ = タッチ位置 / スライドパッド上

通信が途切れたら (0.5 秒) 両方とも消灯する。
"""

from __future__ import annotations

import argparse
import logging
import time
from dataclasses import dataclass

from .protocol import DEFAULT_PORT, Button, ControllerState
from .receiver import ControllerReceiver

log = logging.getLogger(__name__)

TOUCH_X_MAX = 319


@dataclass(frozen=True)
class LedOutput:
    on: bool           # LED1 の点灯
    brightness: float  # LED2 の明るさ (0.0..1.0)


OFF = LedOutput(False, 0.0)


def to_led_output(state: ControllerState | None) -> LedOutput:
    if state is None or state.stop_requested:
        return OFF
    if state.touching:
        # 下画面の左端 = 0、右端 = 1
        brightness = state.touch_x / TOUCH_X_MAX
    else:
        # スライドパッドを上に倒すほど明るい
        brightness = state.circle[1]
    return LedOutput(state.pressed(Button.A), min(1.0, max(0.0, brightness)))


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description="3DS で Raspberry Pi の LED を光らせる")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--led-pin", type=int, default=17, help="A ボタンで点灯する LED の GPIO 番号")
    ap.add_argument("--pwm-pin", type=int, default=27, help="明るさを変える LED の GPIO 番号")
    ap.add_argument("--timeout", type=float, default=0.5, help="通信が途切れて消灯するまでの秒数")
    args = ap.parse_args(argv)

    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")

    try:
        from gpiozero import LED, PWMLED
    except ImportError:
        raise SystemExit("gpiozero がありません。Raspberry Pi で `sudo apt install python3-gpiozero` を実行してください。")

    led = LED(args.led_pin)
    pwm = PWMLED(args.pwm_pin)
    receiver = ControllerReceiver(port=args.port, timeout=args.timeout)
    log.info("listening on udp port %d (LED1=GPIO%d, LED2=GPIO%d)", receiver.port, args.led_pin, args.pwm_pin)

    last: LedOutput | None = None
    try:
        while True:
            out = to_led_output(receiver.poll())
            if out != last:
                led.value = out.on
                pwm.value = out.brightness
                print(f"\rLED1={'ON ' if out.on else 'off'}  LED2={out.brightness * 100:5.1f}%", end="", flush=True)
                last = out
            time.sleep(1 / 30)
    except KeyboardInterrupt:
        print()
    finally:
        led.off()
        pwm.off()
        led.close()
        pwm.close()
        receiver.close()


if __name__ == "__main__":
    main()
