"""コントローラー入力 -> 差動二輪ロボットの走行指令 への変換."""

from __future__ import annotations

from dataclasses import dataclass

from .protocol import Button, ControllerState


@dataclass(frozen=True)
class DriveCommand:
    linear: float   # 前進 +, 後退 -   (-1.0..1.0)
    angular: float  # 左旋回 +, 右旋回 - (-1.0..1.0)
    left: float     # 左車輪 (-1.0..1.0)
    right: float    # 右車輪 (-1.0..1.0)

    @classmethod
    def stop(cls) -> "DriveCommand":
        return cls(0.0, 0.0, 0.0, 0.0)

    @property
    def is_stop(self) -> bool:
        return self.left == 0.0 and self.right == 0.0


@dataclass
class DriveConfig:
    # このボタンを押している間だけ動く (デッドマンスイッチ)。None で無効。
    deadman: int | None = Button.R
    # 押している間は最高速度
    turbo: int | None = Button.L
    normal_speed: float = 0.5
    turbo_speed: float = 1.0
    deadzone: float = 0.12


def _apply_deadzone(v: float, dz: float) -> float:
    if abs(v) < dz:
        return 0.0
    # デッドゾーン外を 0..1 に再スケールして滑らかにする
    sign = 1.0 if v > 0 else -1.0
    return sign * (abs(v) - dz) / (1.0 - dz)


def to_drive_command(state: ControllerState | None, cfg: DriveConfig) -> DriveCommand:
    if state is None or state.stop_requested:
        return DriveCommand.stop()
    if cfg.deadman is not None and not state.pressed(cfg.deadman):
        return DriveCommand.stop()

    x, y = state.circle
    x = _apply_deadzone(x, cfg.deadzone)
    y = _apply_deadzone(y, cfg.deadzone)

    # 十字キーはデジタル操作 (スライドパッドより優先)
    if state.pressed(Button.DUP):
        y = 1.0
    elif state.pressed(Button.DDOWN):
        y = -1.0
    if state.pressed(Button.DLEFT):
        x = -1.0
    elif state.pressed(Button.DRIGHT):
        x = 1.0

    speed = cfg.turbo_speed if cfg.turbo is not None and state.pressed(cfg.turbo) else cfg.normal_speed
    linear = y
    angular = -x

    left = linear - angular
    right = linear + angular
    m = max(1.0, abs(left), abs(right))
    left, right = left / m * speed, right / m * speed

    return DriveCommand(linear * speed, angular * speed, left, right)
