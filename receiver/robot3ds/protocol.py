"""3DS から送られてくる UDP パケットの定義 (3ds/source/protocol.h と対応)."""

from __future__ import annotations

import struct
from dataclasses import dataclass

MAGIC = b"R3DS"
VERSION = 1
DEFAULT_PORT = 4950

FLAG_STOP = 0x01
FLAG_TOUCH = 0x02
FLAG_NEW3DS = 0x04

# magic, version, flags, reserved, seq, buttons,
# circle x/y, cstick x/y, touch x/y, accel xyz, gyro xyz
_FORMAT = "<4sBBHII4h2H6h"
PACKET_SIZE = struct.calcsize(_FORMAT)
assert PACKET_SIZE == 40

CIRCLE_MAX = 156.0  # スライドパッドのおおよその最大値
CSTICK_MAX = 146.0


class Button:
    """libctru の KEY_* ビット."""

    A = 1 << 0
    B = 1 << 1
    SELECT = 1 << 2
    START = 1 << 3
    DRIGHT = 1 << 4
    DLEFT = 1 << 5
    DUP = 1 << 6
    DDOWN = 1 << 7
    R = 1 << 8
    L = 1 << 9
    X = 1 << 10
    Y = 1 << 11
    ZL = 1 << 14
    ZR = 1 << 15
    TOUCH = 1 << 20

    NAMES = {
        "A": A, "B": B, "SELECT": SELECT, "START": START,
        "DRIGHT": DRIGHT, "DLEFT": DLEFT, "DUP": DUP, "DDOWN": DDOWN,
        "R": R, "L": L, "X": X, "Y": Y, "ZL": ZL, "ZR": ZR,
    }


class PacketError(ValueError):
    pass


def _clamp(v: float) -> float:
    return max(-1.0, min(1.0, v))


@dataclass(frozen=True)
class ControllerState:
    flags: int
    seq: int
    buttons: int
    circle_x: int
    circle_y: int
    cstick_x: int
    cstick_y: int
    touch_x: int
    touch_y: int
    accel: tuple[int, int, int]
    gyro: tuple[int, int, int]

    def pressed(self, button: int) -> bool:
        return bool(self.buttons & button)

    def pressed_names(self) -> list[str]:
        return [name for name, bit in Button.NAMES.items() if self.buttons & bit]

    @property
    def stop_requested(self) -> bool:
        return bool(self.flags & FLAG_STOP)

    @property
    def touching(self) -> bool:
        return bool(self.flags & FLAG_TOUCH)

    @property
    def is_new3ds(self) -> bool:
        return bool(self.flags & FLAG_NEW3DS)

    @property
    def circle(self) -> tuple[float, float]:
        """スライドパッドを -1.0..1.0 に正規化 (右 / 上 が正)."""
        return _clamp(self.circle_x / CIRCLE_MAX), _clamp(self.circle_y / CIRCLE_MAX)

    @property
    def cstick(self) -> tuple[float, float]:
        return _clamp(self.cstick_x / CSTICK_MAX), _clamp(self.cstick_y / CSTICK_MAX)


def parse(data: bytes) -> ControllerState:
    if len(data) != PACKET_SIZE:
        raise PacketError(f"bad packet size {len(data)} (expected {PACKET_SIZE})")
    (magic, version, flags, _reserved, seq, buttons,
     cx, cy, sx, sy, tx, ty, ax, ay, az, gx, gy, gz) = struct.unpack(_FORMAT, data)
    if magic != MAGIC:
        raise PacketError(f"bad magic {magic!r}")
    if version != VERSION:
        raise PacketError(f"unsupported version {version}")
    return ControllerState(flags, seq, buttons, cx, cy, sx, sy, tx, ty, (ax, ay, az), (gx, gy, gz))


def build(
    *,
    seq: int = 0,
    buttons: int = 0,
    circle: tuple[int, int] = (0, 0),
    cstick: tuple[int, int] = (0, 0),
    touch: tuple[int, int] | None = None,
    accel: tuple[int, int, int] = (0, 0, 0),
    gyro: tuple[int, int, int] = (0, 0, 0),
    flags: int = 0,
) -> bytes:
    """パケットを組み立てる (テストや 3DS なしでの動作確認用)."""
    if touch is not None:
        flags |= FLAG_TOUCH
        buttons |= Button.TOUCH
    tx, ty = touch or (0, 0)
    return struct.pack(
        _FORMAT, MAGIC, VERSION, flags, 0, seq & 0xFFFFFFFF, buttons,
        *circle, *cstick, tx, ty, *accel, *gyro,
    )


def seq_newer(seq: int, last: int) -> bool:
    """32bit のラップアラウンドを考慮して seq が last より新しいか."""
    diff = (seq - last) & 0xFFFFFFFF
    return 0 < diff < 0x80000000
