// 3DS Robot Controller
// 3DS の入力を UDP でロボット (Raspberry Pi / PC など) に送信する homebrew アプリ。
//
// 操作:
//   SELECT : 送信先 IP アドレスを変更
//   START  : 終了 (終了前に停止パケットを送る)
//   その他 : すべてそのままロボットへ送信

#include <3ds.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "protocol.h"

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000
#define CONFIG_DIR "sdmc:/3ds/robot-controller"
#define CONFIG_PATH CONFIG_DIR "/config.txt"
#define SEND_INTERVAL_FRAMES 2 // 60fps / 2 = 30Hz

typedef struct {
	char ip[16];
	int port;
} Config;

static void config_default(Config *cfg) {
	strcpy(cfg->ip, "192.168.0.10");
	cfg->port = R3DS_DEFAULT_PORT;
}

// config.txt 形式:
//   ip=192.168.0.10
//   port=4950
static void config_load(Config *cfg) {
	config_default(cfg);
	FILE *f = fopen(CONFIG_PATH, "r");
	if (!f) return;
	char line[64];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (strncmp(line, "ip=", 3) == 0) {
			snprintf(cfg->ip, sizeof(cfg->ip), "%.15s", line + 3);
		} else if (strncmp(line, "port=", 5) == 0) {
			int port = atoi(line + 5);
			if (port > 0 && port < 65536) cfg->port = port;
		}
	}
	fclose(f);
}

static void config_save(const Config *cfg) {
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR, 0777);
	FILE *f = fopen(CONFIG_PATH, "w");
	if (!f) return;
	fprintf(f, "ip=%s\nport=%d\n", cfg->ip, cfg->port);
	fclose(f);
}

static bool edit_ip(Config *cfg) {
	SwkbdState swkbd;
	char buf[16];
	swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, sizeof(buf) - 1);
	swkbdSetNumpadKeys(&swkbd, '.', 0);
	swkbdSetInitialText(&swkbd, cfg->ip);
	swkbdSetHintText(&swkbd, "Robot IP address");
	if (swkbdInputText(&swkbd, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM) return false;

	struct in_addr addr;
	if (inet_aton(buf, &addr) == 0) return false;
	snprintf(cfg->ip, sizeof(cfg->ip), "%s", buf);
	config_save(cfg);
	return true;
}

static bool make_dest(const Config *cfg, struct sockaddr_in *dest) {
	memset(dest, 0, sizeof(*dest));
	dest->sin_family = AF_INET;
	dest->sin_port = htons(cfg->port);
	return inet_aton(cfg->ip, &dest->sin_addr) != 0;
}

static void fill_packet(R3dsPacket *pkt, u32 seq, bool is_new3ds, u8 extra_flags) {
	circlePosition circle, cstick;
	touchPosition touch;
	accelVector accel;
	angularRate gyro;
	u32 held = hidKeysHeld();

	hidCircleRead(&circle);
	hidTouchRead(&touch);
	hidAccelRead(&accel);
	hidGyroRead(&gyro);
	memset(&cstick, 0, sizeof(cstick));
	if (is_new3ds) hidCstickRead(&cstick);

	memset(pkt, 0, sizeof(*pkt));
	memcpy(pkt->magic, R3DS_MAGIC, 4);
	pkt->version = R3DS_VERSION;
	pkt->flags = extra_flags;
	if (held & KEY_TOUCH) pkt->flags |= R3DS_FLAG_TOUCH;
	if (is_new3ds) pkt->flags |= R3DS_FLAG_NEW3DS;
	pkt->seq = seq;
	pkt->buttons = held;
	pkt->circle_x = circle.dx;
	pkt->circle_y = circle.dy;
	pkt->cstick_x = cstick.dx;
	pkt->cstick_y = cstick.dy;
	if (held & KEY_TOUCH) {
		pkt->touch_x = touch.px;
		pkt->touch_y = touch.py;
	}
	pkt->accel_x = accel.x;
	pkt->accel_y = accel.y;
	pkt->accel_z = accel.z;
	pkt->gyro_x = gyro.x;
	pkt->gyro_y = gyro.y;
	pkt->gyro_z = gyro.z;
}

static void draw_status(const Config *cfg, const R3dsPacket *pkt, bool dest_ok, int send_errors) {
	printf("\x1b[1;1H");
	printf("=== 3DS Robot Controller ===\x1b[K\n\n");
	printf("3DS IP : %s\x1b[K\n", inet_ntoa((struct in_addr){.s_addr = gethostid()}));
	printf("Target : %s:%d %s\x1b[K\n", cfg->ip, cfg->port, dest_ok ? "" : "(invalid)");
	printf("Sent   : %lu  errors: %d\x1b[K\n\n", (unsigned long)pkt->seq, send_errors);
	printf("Buttons: %08lx\x1b[K\n", (unsigned long)pkt->buttons);
	printf("Circle : %4d %4d\x1b[K\n", pkt->circle_x, pkt->circle_y);
	printf("CStick : %4d %4d\x1b[K\n", pkt->cstick_x, pkt->cstick_y);
	printf("Touch  : %3u %3u %s\x1b[K\n", pkt->touch_x, pkt->touch_y,
	       (pkt->flags & R3DS_FLAG_TOUCH) ? "*" : " ");
	printf("Accel  : %5d %5d %5d\x1b[K\n", pkt->accel_x, pkt->accel_y, pkt->accel_z);
	printf("Gyro   : %5d %5d %5d\x1b[K\n\n", pkt->gyro_x, pkt->gyro_y, pkt->gyro_z);
	printf("SELECT: change IP   START: quit\x1b[K\n");
}

int main(void) {
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);

	bool is_new3ds = false;
	APT_CheckNew3DS(&is_new3ds);
	HIDUSER_EnableAccelerometer();
	HIDUSER_EnableGyroscope();

	u32 *soc_buf = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
	if (!soc_buf || R_FAILED(socInit(soc_buf, SOC_BUFFERSIZE))) {
		printf("socInit failed. Press START to exit.\n");
		while (aptMainLoop()) {
			hidScanInput();
			if (hidKeysDown() & KEY_START) break;
			gspWaitForVBlank();
		}
		free(soc_buf);
		gfxExit();
		return 1;
	}

	int sock = socket(AF_INET, SOCK_DGRAM, 0);

	Config cfg;
	config_load(&cfg);
	struct sockaddr_in dest;
	bool dest_ok = make_dest(&cfg, &dest);

	R3dsPacket pkt;
	memset(&pkt, 0, sizeof(pkt));
	u32 seq = 0;
	u32 frame = 0;
	int send_errors = 0;

	while (aptMainLoop()) {
		hidScanInput();
		u32 down = hidKeysDown();
		if (down & KEY_START) break;
		if (down & KEY_SELECT) {
			if (edit_ip(&cfg)) dest_ok = make_dest(&cfg, &dest);
			consoleClear();
		}

		if (++frame % SEND_INTERVAL_FRAMES == 0) {
			fill_packet(&pkt, seq++, is_new3ds, 0);
			if (dest_ok && sock >= 0 &&
			    sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
				send_errors++;
			}
		}

		draw_status(&cfg, &pkt, dest_ok, send_errors);
		gspWaitForVBlank();
	}

	// ロボットを止めるため停止パケットを数回送る (UDP なので冗長に)
	if (dest_ok && sock >= 0) {
		for (int i = 0; i < 3; i++) {
			fill_packet(&pkt, seq++, is_new3ds, R3DS_FLAG_STOP);
			pkt.buttons = 0;
			sendto(sock, &pkt, sizeof(pkt), 0, (struct sockaddr *)&dest, sizeof(dest));
		}
	}

	if (sock >= 0) close(sock);
	HIDUSER_DisableGyroscope();
	HIDUSER_DisableAccelerometer();
	socExit();
	free(soc_buf);
	gfxExit();
	return 0;
}
