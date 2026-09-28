// 3DS -> ロボット 通信パケット定義
// 受信側の実装は receiver/robot3ds/protocol.py と一致させること。
#pragma once

#include <stdint.h>

#define R3DS_MAGIC "R3DS"
#define R3DS_VERSION 1
#define R3DS_DEFAULT_PORT 4950

// flags
#define R3DS_FLAG_STOP 0x01          // アプリ終了時などに送る停止要求
#define R3DS_FLAG_TOUCH 0x02         // タッチパネルに触れている
#define R3DS_FLAG_NEW3DS 0x04        // New 3DS (Cスティック / ZL / ZR 有効)

// 全フィールド little endian。合計 40 バイト。
typedef struct __attribute__((packed)) {
	char magic[4];      // "R3DS"
	uint8_t version;    // R3DS_VERSION
	uint8_t flags;      // R3DS_FLAG_*
	uint16_t reserved;
	uint32_t seq;       // 送信ごとに +1
	uint32_t buttons;   // libctru の KEY_* ビットそのまま (hidKeysHeld)
	int16_t circle_x;   // スライドパッド (約 -156..156)
	int16_t circle_y;
	int16_t cstick_x;   // Cスティック (New 3DS のみ)
	int16_t cstick_y;
	uint16_t touch_x;   // 0..319
	uint16_t touch_y;   // 0..239
	int16_t accel_x;
	int16_t accel_y;
	int16_t accel_z;
	int16_t gyro_x;
	int16_t gyro_y;
	int16_t gyro_z;
} R3dsPacket;

_Static_assert(sizeof(R3dsPacket) == 40, "R3dsPacket must be 40 bytes");
