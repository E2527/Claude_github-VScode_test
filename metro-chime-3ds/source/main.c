// Metro Chime for Nintendo 3DS: a time-signal clock with city motion graphics and electronica.
// Top screen: background scene, clock face, launch intro and the time-signal cards (3D slider adds depth).
// Bottom screen: date details and touch controls.
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gfx.h"
#include "scenes.h"
#include "synth.h"

#define SPB (60.0 / SYN_BPM)
#define SCENE_EVERY 2   // minutes

static const char *WD_EN[7] = {"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY"};
static const char *WD_JP[7] = {"日曜日", "月曜日", "火曜日", "水曜日", "木曜日", "金曜日", "土曜日"};
static const char *MON_EN[12] = {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};

/* ---------------- time ---------------- */
typedef struct { int y, mon, d, wd, yday, h, m, s, ms; } Clock;

// osGetTime() is milliseconds since 1900-01-01 in the console's local time
static Clock clock_at(double offsetSec) {
	u64 ms = osGetTime() + (s64)(offsetSec * 1000);
	time_t unixT = (time_t)(ms / 1000 - 2208988800ULL);
	struct tm *t = gmtime(&unixT);
	return (Clock){t->tm_year + 1900, t->tm_mon, t->tm_mday, t->tm_wday, t->tm_yday, t->tm_hour, t->tm_min, t->tm_sec, (int)(ms % 1000)};
}
static int daysInYear(int y) { return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365; }
static int dowJan1(int y) {   // 0 = Sunday (Sakamoto)
	y -= 1;
	return (y + y / 4 - y / 100 + y / 400 + 1) % 7;
}
static int isoWeeksIn(int y) {
	int d = dowJan1(y);
	return (d == 4 || (d == 3 && daysInYear(y) == 366)) ? 53 : 52;
}
static int isoWeek(const Clock *c) {
	int wd = c->wd ? c->wd : 7;   // Monday = 1 .. Sunday = 7
	int w = (c->yday + 1 - wd + 10) / 7;
	if (w < 1) return isoWeeksIn(c->y - 1);
	if (w > isoWeeksIn(c->y)) return 1;
	return w;
}
static void jpTime(const Clock *c, char *out, size_t n) {
	if (c->m == 0) snprintf(out, n, "%s%d時ちょうど", c->h < 12 ? "午前" : "午後", c->h % 12);
	else snprintf(out, n, "%s%d時%d分", c->h < 12 ? "午前" : "午後", c->h % 12, c->m);
}

/* ---------------- app state ---------------- */
static bool haveAudio, bgmOn;
static int autoMode;   // 0 hourly, 1 every 15 min, 2 off
static float appT;     // seconds since launch

enum { ST_INTRO, ST_FACE };
static int state = ST_INTRO;
static float faceT = -1;      // seconds since the face appeared

// intro
#define NCELL 32
static struct { char target[2][24]; char shown[NCELL]; int n; float lockAt[NCELL]; bool locked[NCELL]; int row[NCELL], colIdx[NCELL]; } board;
static float introT, introOut = -1;
static bool introReady;

// clock face digit roll
static char digits[4] = {'-', '-', '-', '-'};
static float digitChange[4];

// time signal
typedef struct { int v; char main[16]; char sub[48]; int beats; } Card;
enum { V_INK, V_SODIUM, V_SIGNAL, V_TAIL, V_OUTLINE, V_COUNT };
static struct {
	bool active, pending, closing;
	int countStep, cardStep;
	Card cards[6];
	char hm[8], hms[16];
	float closeT;
	int shownKey; double shownAt;
} ann;

// bottom-screen buttons
typedef struct { float x, y, w, h; float pressT; } Button;
enum { B_ANN, B_BGM, B_AUTO, B_SCENE, NBTN };
static Button btn[NBTN];
static const float BTN_LAND[NBTN][4] = {{10, 92, 300, 54}, {10, 154, 96, 58}, {112, 154, 96, 58}, {214, 154, 96, 58}};
static const float BTN_PORT[NBTN][4] = {{10, 128, 220, 56}, {10, 192, 107, 54}, {123, 192, 107, 54}, {10, 252, 220, 44}};
static void layoutButtons(void) {
	for (int i = 0; i < NBTN; i++) {
		const float *r = g_orient == OR_LAND ? BTN_LAND[i] : BTN_PORT[i];
		btn[i].x = r[0]; btn[i].y = r[1]; btn[i].w = r[2]; btn[i].h = r[3];
	}
}

// orientation: auto from the accelerometer, or forced with SELECT
static int orientMode;   // 0 auto, 1 landscape, 2 portrait (top screen on the left), 3 portrait (top screen on the right)
static const char *ORIENT_LABEL[4] = {"向き: 自動", "向き: 横", "向き: 縦 (上画面が左)", "向き: 縦 (上画面が右)"};
static float toastT = -9; static const char *toastMsg;
static struct { bool active; float t, dir; } rot;   // dir: which way the frame turns in
static float faceBase;   // seconds to wait before the face starts its entrance

static float pulse;

/* ---------------- time signal ---------------- */
static void buildCards(void) {
	double t1 = synth_step_time(ann.cardStep);
	Clock c = clock_at(t1 - synth_now());
	char jt[32]; jpTime(&c, jt, sizeof jt);
	snprintf(ann.hm, sizeof ann.hm, "%02d:%02d", c.h, c.m);
	snprintf(ann.hms, sizeof ann.hms, "%02d:%02d:%02d", c.h, c.m, c.s);
	Card cs[6] = {
		{V_INK, "TODAY", "本日は", 1},
		{V_SODIUM, "", "", 2},
		{V_OUTLINE, "", "", 1},
		{V_TAIL, "", "", 1},
		{V_SIGNAL, "", "", 2},
		{V_INK, "LOCAL", "3DS TIME", 1},
	};
	snprintf(cs[1].main, sizeof cs[1].main, "%s", WD_EN[c.wd]);
	snprintf(cs[1].sub, sizeof cs[1].sub, "%s", WD_JP[c.wd]);
	snprintf(cs[2].main, sizeof cs[2].main, "%02d.%02d", c.mon + 1, c.d);
	snprintf(cs[2].sub, sizeof cs[2].sub, "%d月%d日 · %s", c.mon + 1, c.d, MON_EN[c.mon]);
	snprintf(cs[3].main, sizeof cs[3].main, "%d", c.y);
	snprintf(cs[3].sub, sizeof cs[3].sub, "令和%d年", c.y - 2018);
	snprintf(cs[4].main, sizeof cs[4].main, "%s", ann.hm);
	snprintf(cs[4].sub, sizeof cs[4].sub, "%s", jt);
	memcpy(ann.cards, cs, sizeof cs);
}

static void announce(bool countIn) {
	if (ann.active) return;
	memset(&ann, 0, sizeof ann);
	ann.active = ann.pending = true;
	ann.countStep = ann.cardStep = -1;
	ann.shownKey = -99;
	synth_announce(countIn);
}

// Which card is showing at time t: key (-4..-1 count-in, 0..5 cards, 6 final, 7 done), and when it began.
static int annCardAt(double t, double *start) {
	double t1 = synth_step_time(ann.cardStep);
	if (ann.countStep >= 0) {
		double t0 = synth_step_time(ann.countStep);
		if (t < t0) return -99;
		if (t < t1) { int b = (int)((t - t0) / SPB); *start = t0 + b * SPB; return -4 + b; }
	}
	if (t < t1) return -99;
	double beat = (t - t1) / SPB;
	if (beat >= 15) return 7;
	if (beat >= 8) { *start = t1 + 8 * SPB; return 6; }
	int acc = 0;
	for (int i = 0; i < 6; i++) {
		if (beat < acc + ann.cards[i].beats) { *start = t1 + acc * SPB; return i; }
		acc += ann.cards[i].beats;
	}
	return 7;
}

static void annUpdate(float dt) {
	if (!ann.active) return;
	if (ann.pending) {
		int cs, ks;
		synth_announce_steps(&cs, &ks);
		if (ks < 0) return;
		ann.countStep = cs; ann.cardStep = ks; ann.pending = false;
		buildCards();
	}
	double st;
	int key = annCardAt(synth_now(), &st);
	if (key == 7 && !ann.closing) {
		ann.closing = true; ann.closeT = 0;
		if (!bgmOn) synth_music(false);
	}
	if (ann.closing) {
		ann.closeT += dt;
		if (ann.closeT > 0.5f) ann.active = false;
	}
}

static bool annShowing(void) {
	if (!ann.active || ann.pending) return false;
	double st;
	return annCardAt(synth_now(), &st) != -99;
}

/* ---------------- drawing: time signal cards ---------------- */
static void outlineText(FontId f, const char *s, float x, float y, float size, u32 stroke, u32 fill) {
	static const float O[8][2] = {{-1.5f,0},{1.5f,0},{0,-1.5f},{0,1.5f},{-1,-1},{1,1},{-1,1},{1,-1}};
	for (int i = 0; i < 8; i++) text(f, s, x + O[i][0], y + O[i][1], size, stroke, AL_CENTER);
	text(f, s, x, y, size, fill, AL_CENTER);
}

static void drawCards(void) {
	double st = 0;
	double tn = synth_now();
	int key = annCardAt(tn, &st);
	float fade = ann.closing ? 1 - clampf(ann.closeT / 0.5f, 0, 1) : 1;
	if (key == -99) return;
	if (key == 7) key = 6;
	float u = (float)(tn - st);
	int v; const char *mainS, *subS;
	char cnt[12];
	if (key < 0) { snprintf(cnt, sizeof cnt, "%d", -key); v = V_COUNT; mainS = cnt; subS = "STAND BY"; }
	else if (key == 6) { v = V_OUTLINE; mainS = ann.hm; subS = "をお知らせします"; }
	else { v = ann.cards[key].v; mainS = ann.cards[key].main; subS = ann.cards[key].sub; }

	u32 bg; u32 fg = col(TEXT, fade), sub = col(DIM, fade);
	switch (v) {
	case V_SODIUM: bg = col(SODIUM, fade); fg = col(INK, fade); sub = col(INK, 0.7f * fade); break;
	case V_SIGNAL: bg = col(SIGNAL, fade); fg = col(INK, fade); sub = col(INK, 0.7f * fade); break;
	case V_TAIL:   bg = col(TAIL, fade);   fg = col(INK, fade); sub = col(INK, 0.7f * fade); break;
	case V_OUTLINE: bg = col(INK, 0.45f * fade); sub = col(SIGNAL, fade); break;
	case V_COUNT:  bg = col(INK, 0.3f * fade); fg = col(SODIUM, fade); break;
	default:       bg = col(INK, 0.5f * fade); break;
	}
	rect(0, 0, g_w * ease(u / 0.34f), g_h, bg);

	// streak bars
	float bp = ease(u / 0.55f);
	rect(-g_w + bp * 2 * g_w + DX(-0.5f), g_h * 0.16f, g_w, 2, col(SIGNAL, fade));
	rect(-g_w + ease((u - 0.05f) / 0.55f) * 2 * g_w + DX(-0.5f), g_h * 0.76f, g_w, 6, col(SODIUM, fade));
	rect(g_w - ease((u - 0.1f) / 0.55f) * 2 * g_w + DX(-0.5f), g_h * 0.81f, g_w, 2, col(TAIL, fade));

	// main word: slams in from the left, sized to fit
	FontId mf = (v == V_COUNT || key == 4 || key == 6 || key == 2 || key == 3) ? F_CLOCK : F_DISPLAY;
	if (mf == F_CLOCK) for (const char *p = mainS; *p; p++) if (!(*p >= '0' && *p <= '9') && *p != ':') { mf = F_DISPLAY; break; }
	float size = 150, w = text_w(mf, mainS, size);
	if (w > g_w * 0.88f) size *= g_w * 0.88f / w;
	float a = ease(u / 0.2f) * fade;
	float x = g_w / 2 - (1 - ease(u / 0.42f)) * g_w * 0.14f + DX(-2);
	float y = g_h * 0.42f - size * 0.5f;
	if (v == V_OUTLINE) outlineText(mf, mainS, x, y, size, col(SIGNAL, a), col(0x0a0f1c, 0.92f * a));
	else text(mf, mainS, x, y, size, (fg & 0x00ffffff) | ((u32)(a * 255) << 24), AL_CENTER);

	float su = ease((u - 0.08f) / 0.5f);
	text(F_SYS, subS, g_w / 2 + DX(-1), g_h * 0.66f + (1 - su) * 14, 20, (sub & 0x00ffffff) | ((u32)(su * fade * ((sub >> 24) / 255.f) * 255) << 24), AL_CENTER);

	// HUD corners
	text(F_MONO, "METRO CHIME", 10, 8, 11, sub, AL_LEFT);
	text(F_MONO, "TIME SIGNAL", 10, 20, 11, sub, AL_LEFT);
	char bpm[16]; snprintf(bpm, sizeof bpm, "%d BPM", SYN_BPM);
	text(F_MONO, bpm, g_w - 10, g_h - 32, 11, sub, AL_RIGHT);
	text(F_MONO, ann.hms, g_w - 10, g_h - 20, 11, sub, AL_RIGHT);
}

/* ---------------- drawing: clock face ---------------- */
static float enterOff(float delay, float dist) { return faceT < 0 ? 0 : -(1 - ease((faceT - faceBase - delay) / 0.6f)) * dist; }
static float enterA(float delay) { return faceT < 0 ? 1 : ease((faceT - faceBase - delay) / 0.4f); }

static void drawFacePortrait(const Clock *c);
static void drawFace(const Clock *c) {
	if (g_orient != OR_LAND) { drawFacePortrait(c); return; }
	char buf[48];
	float a;
	// status strip
	a = enterA(0.05f);
	text(F_MONO, "LOCAL TIME", 14 + DX(-0.3f), 7, 11, col(DIM, a), AL_LEFT);
	snprintf(buf, sizeof buf, "%02d/%02d %s", scenes_index() + 1, scenes_count(), scenes_name());
	float lw = text_w(F_MONO, buf, 11);
	rect(g_w - 18 - lw + DX(-0.3f), 4, lw + 10, 17, col(INK, 0.6f * a));
	frame(g_w - 18 - lw + DX(-0.3f), 4, lw + 10, 17, 1, col(LINE, a));
	text(F_MONO, buf, g_w - 13 - lw + DX(-0.3f), 7, 11, col(SIGNAL, a), AL_LEFT);

	// weekday
	a = enterA(0.1f);
	float ox = enterOff(0.1f, 40);
	text(F_DISPLAY, WD_EN[c->wd], 16 + ox + DX(-0.8f), 22, 40, col(SODIUM, a), AL_LEFT);
	float ww = text_w(F_DISPLAY, WD_EN[c->wd], 40);
	text(F_SYS, WD_JP[c->wd], 26 + ww + ox + DX(-0.8f), 36, 18, col(TEXT, a), AL_LEFT);

	// big clock with rolling digits
	a = enterA(0.16f);
	ox = -enterOff(0.16f, 50);
	const float size = 132;
	float slot = text_w(F_CLOCK, "0", size) * 1.06f, cw = text_w(F_CLOCK, ":", size) * 1.1f;
	float x = 14 + ox + DX(-1.2f), y = 50;
	for (int i = 0; i < 4; i++) {
		char d[2] = {digits[i], 0};
		float u = appT - digitChange[i], e = ease(u / 0.55f);
		float yo = (1 - e) * size * 0.25f;
		u32 cc = e < 1 ? col(u < 0.3f ? SIGNAL : TEXT, a * e) : col(TEXT, a);
		text(F_CLOCK, d, x + slot / 2, y + yo, size, cc, AL_CENTER);
		x += slot;
		if (i == 1) { text(F_CLOCK, ":", x + cw / 2, y, size, col(SODIUM, (c->s % 2 ? 0.3f : 1) * a), AL_CENTER); x += cw; }
	}
	text(F_MONO, c->h < 12 ? "AM" : "PM", x + 6, y + size * 0.55f, 11, col(DIM, a), AL_LEFT);

	// seconds ticks (quarter marks taller)
	float sp = 364.f / 60, gx = 18 + DX(-0.6f), gy = 186, grow = faceT < 0 ? 1 : ease((faceT - faceBase - 0.28f) / 0.7f);
	for (int i = 0; i < 60 * grow; i++) {
		bool q = i % 15 == 0;
		u32 cc = i == c->s ? col(SODIUM, 1) : i < c->s ? col(TEXT, 1) : col(LINE, 1);
		rect(gx + i * sp, gy + (q ? 0 : 5), sp - 1.4f, q ? 10 : 5, cc);
	}

	// date
	a = enterA(0.36f);
	ox = enterOff(0.36f, 40);
	snprintf(buf, sizeof buf, "%02d.%02d", c->mon + 1, c->d);
	text(F_DISPLAY, buf, 16 + ox + DX(-0.6f), 198, 38, col(TEXT, a), AL_LEFT);
	float dw = text_w(F_DISPLAY, buf, 38);
	snprintf(buf, sizeof buf, "%d", c->y);
	text(F_DISPLAY, buf, 28 + dw + ox + DX(-0.6f), 208, 26, col(SIGNAL, a), AL_LEFT);
	float yw = text_w(F_DISPLAY, buf, 26);
	text(F_MONO, MON_EN[c->mon], 40 + dw + yw + ox + DX(-0.6f), 218, 11, col(DIM, a), AL_LEFT);
}

static void drawDigit(int i, float cx, float y, float size, float a) {
	char d[2] = {digits[i], 0};
	float u = appT - digitChange[i], e = ease(u / 0.55f);
	float yo = (1 - e) * size * 0.25f;
	u32 cc = e < 1 ? col(u < 0.3f ? SIGNAL : TEXT, a * e) : col(TEXT, a);
	text(F_CLOCK, d, cx, y + yo, size, cc, AL_CENTER);
}

static void drawFacePortrait(const Clock *c) {
	char buf[48];
	float a = enterA(0.05f);
	text(F_MONO, "LOCAL TIME", 12, 7, 11, col(DIM, a), AL_LEFT);
	snprintf(buf, sizeof buf, "%02d/%02d %s", scenes_index() + 1, scenes_count(), scenes_name());
	float lw = text_w(F_MONO, buf, 11);
	rect(g_w - 16 - lw, 4, lw + 10, 17, col(INK, 0.6f * a));
	frame(g_w - 16 - lw, 4, lw + 10, 17, 1, col(LINE, a));
	text(F_MONO, buf, g_w - 11 - lw, 7, 11, col(SIGNAL, a), AL_LEFT);

	a = enterA(0.1f);
	float ox = enterOff(0.1f, 40);
	float ws = fminf(44, 44 * (g_w - 28) / fmaxf(1, text_w(F_DISPLAY, WD_EN[c->wd], 44)));
	text(F_DISPLAY, WD_EN[c->wd], 14 + ox, 30, ws, col(SODIUM, a), AL_LEFT);
	text(F_SYS, WD_JP[c->wd], 15 + ox, 32 + ws, 18, col(TEXT, a), AL_LEFT);

	// hours over minutes, with the blinking separator between them
	a = enterA(0.16f);
	ox = -enterOff(0.16f, 50);
	// the digits fill roughly 0.19..0.81 of the line box, so rows sit 0.62 * size + a gap apart
	const float size = 150, hy = 80, my = hy + size * 0.62f + 18;
	float slot = text_w(F_CLOCK, "0", size) * 1.04f, x0 = 10 + ox;
	for (int i = 0; i < 4; i++) drawDigit(i, x0 + slot * (i % 2) + slot / 2, i < 2 ? hy : my, size, a);
	float sepA = (c->s % 2 ? 0.3f : 1) * a, sepY = hy + size * 0.81f + 7;
	rect(x0 + 6, sepY, slot * 2 - 12, 4, col(SODIUM, sepA));
	text(F_MONO, c->h < 12 ? "AM" : "PM", x0 + slot * 2 + 6, sepY - 4, 11, col(DIM, a), AL_LEFT);

	float sp = (g_w - 28) / 60.f, gx = 14, gy = 326, grow = faceT < 0 ? 1 : ease((faceT - faceBase - 0.28f) / 0.7f);
	for (int i = 0; i < 60 * grow; i++) {
		bool q = i % 15 == 0;
		u32 cc = i == c->s ? col(SODIUM, 1) : i < c->s ? col(TEXT, 1) : col(LINE, 1);
		rect(gx + i * sp, gy + (q ? 0 : 5), sp - 1, q ? 10 : 5, cc);
	}

	a = enterA(0.36f);
	ox = enterOff(0.36f, 40);
	snprintf(buf, sizeof buf, "%02d.%02d", c->mon + 1, c->d);
	text(F_DISPLAY, buf, 14 + ox, 340, 38, col(TEXT, a), AL_LEFT);
	float dw = text_w(F_DISPLAY, buf, 38);
	snprintf(buf, sizeof buf, "%d", c->y);
	text(F_DISPLAY, buf, 26 + dw + ox, 350, 26, col(SIGNAL, a), AL_LEFT);
	float yw = text_w(F_DISPLAY, buf, 26);
	text(F_MONO, MON_EN[c->mon], 36 + dw + yw + ox, 359, 11, col(DIM, a), AL_LEFT);
}

/* ---------------- drawing: launch intro ---------------- */
static void drawIntro(void) {
	float t = introT;
	rect(0, 0, g_w, g_h, col(INK, 1));
	// signal line draws across, then bursts open
	float l1 = ease(t / 0.45f), lo = span(t, 0.6f, 0.95f);
	if (lo < 1) rect(0, g_h / 2 - 1 - lo * 60, g_w * l1, 2 + lo * 120, col(SIGNAL, 1 - lo));
	// colour bands sweep diagonally
	const u32 BC[3] = {SODIUM, TAIL, SIGNAL};
	const float BW[3] = {100, 36, 16}, BD[3] = {0, 0.08f, 0.14f};
	for (int i = 0; i < 3; i++) {
		float p = span(t, 0.42f + BD[i], 1.22f + BD[i]);
		if (p <= 0 || p >= 1) continue;
		float x = -140 + ease(p) * (g_w + 300);
		quad(x, -10, x + BW[i], -10, x + BW[i] - 60, g_h + 10, x - 60, g_h + 10, col(BC[i], 1));
	}
	// title
	bool P = g_orient != OR_LAND;
	float s = 70, tx = P ? g_w / 2 : 120, ty = P ? 70 : 34;
	float pa = ease((t - 0.7f) / 0.6f), pb = ease((t - 0.78f) / 0.6f);
	if (t > 0.7f) text(F_DISPLAY, "METRO", tx - (1 - pa) * 260 + DX(-1.5f), ty, s, col(TEXT, 1), AL_CENTER);
	if (t > 0.78f) text(F_DISPLAY, "CHIME", tx + (1 - pb) * 300 + DX(-1.5f), ty + s * 0.8f, s, col(SODIUM, 1), AL_CENTER);
	// split-flap board
	if (t > 1.1f) {
		const float cwid = 11, chgt = 18, x0 = P ? (g_w - 14 * 13) / 2 : 210, by = P ? 212 : 72, bx1 = P ? x0 + 14 * 13 : 392;
		for (int r = 0; r < 2; r++) {
			float y0 = by + r * (chgt + 5);
			for (int i = 0; i < board.n; i++) {
				if (board.row[i] != r) continue;
				float x = x0 + board.colIdx[i] * (cwid + 2) + DX(-0.8f);
				char ch[2] = {board.shown[i], 0};
				if (ch[0] == ' ' || ch[0] == '.' || ch[0] == ':') { text(F_MONO, ch, x + cwid / 2, y0 + 3, 12, col(TEXT, 1), AL_CENTER); continue; }
				rect(x, y0, cwid, chgt, col(INK2, 1));
				frame(x, y0, cwid, chgt, 1, col(LINE, 1));
				text(F_MONO, ch, x + cwid / 2, y0 + 3, 12, board.locked[i] ? col((appT - board.lockAt[i]) < 0.26f ? SODIUM : TEXT, 1) : col(DIM, 1), AL_CENTER);
				rect(x, y0 + chgt / 2, cwid, 1, col(INK, 1));
			}
		}
		text(F_MONO, "TIME SIGNAL SYSTEM", x0, by + 52, 10, col(DIM, 1), AL_LEFT);
		text(F_MONO, introReady ? "READY" : "BOOT", bx1, by + 52, 10, col(SIGNAL, 1), AL_RIGHT);
		if (introReady) text(F_DISPLAY, "TIME SIGNAL", (x0 + bx1) / 2 + DX(-1), by + 74, 28, col(SODIUM, 0.55f + 0.45f * sinf(appT * 6)), AL_CENTER);
	}
}

static void drawBlinds(float p) {   // the intro opens like blinds onto the clock
	for (int i = 0; i < 8; i++) {
		float e = ease((p - i * 0.045f) / 0.7f) * (g_w + 4);
		float y = i * g_h / 8.f;
		rect(i % 2 ? e : -e, y, g_w, g_h / 8.f + 1, col(INK, 1));
	}
}

/* ---------------- drawing: bottom screen ---------------- */
static void drawButton(Button *b, const char *label, const char *sub, const char *key, bool primary, bool on) {
	float pr = 1 - clampf((appT - b->pressT) / 0.25f, 0, 1);
	u32 bg = primary ? col(SODIUM, 1) : col(INK2, 0.9f);
	rect(b->x, b->y, b->w, b->h, bg);
	if (pr > 0) rect(b->x, b->y, b->w, b->h, col(0xffffff, 0.35f * pr));
	frame(b->x, b->y, b->w, b->h, 1, primary ? col(SODIUM, 1) : on ? col(SIGNAL, 1) : col(LINE, 1));
	u32 fg = primary ? col(INK, 1) : col(TEXT, 1);
	u32 sc = primary ? col(INK, 0.65f) : on ? col(SIGNAL, 1) : col(DIM, 1);
	text(F_SYS, label, b->x + 10, b->y + 8, primary ? 22 : 16, fg, AL_LEFT);
	text(F_MONO, sub, b->x + 10, b->y + b->h - 17, 10, sc, AL_LEFT);
	float kx = b->x + b->w - 18, ky = b->y + 8;
	disc(kx + 6, ky + 7, 8, primary ? col(INK, 0.15f) : col(LINE, 1));
	text(F_MONO, key, kx + 6, ky + 1, 11, fg, AL_CENTER);
}

static void drawBottom(const Clock *c) {
	rect(0, 0, g_bw, g_bh, col(INK, 1));
	for (int x = 0; x < g_bw; x += 20) rect(x, 0, 1, g_bh, col(SIGNAL, 0.03f));
	for (int y = 0; y < g_bh; y += 20) rect(0, y, g_bw, 1, col(SIGNAL, 0.03f));
	char buf[48];

	if (state == ST_INTRO && introOut < 0) {
		text(F_DISPLAY, "METRO CHIME", g_bw / 2, 70, 40, col(TEXT, 1), AL_CENTER);
		text(F_MONO, "BOOT SEQUENCE", g_bw / 2, 118, 11, col(DIM, 1), AL_CENTER);
		float p = clampf(introT / 3.2f, 0, 1), bw = fminf(200, g_bw - 40);
		rect(g_bw / 2 - bw / 2, 136, bw, 3, col(LINE, 1));
		rect(g_bw / 2 - bw / 2, 136, bw * p, 3, col(SIGNAL, 1));
		text(F_MONO, "A: START    B: CLOCK ONLY", g_bw / 2, 196, 10, col(DIM, 1), AL_CENTER);
		return;
	}

	text(F_MONO, "METRO CHIME", 10, 7, 11, col(TEXT, 1), AL_LEFT);
	int step = synth_step_at(synth_now());
	for (int i = 0; i < 4; i++) {
		bool on = step >= 0 && (step % 16) / 4 == i;
		rect(g_bw - 58 + i * 12, 8, 8, 8, on ? col(SODIUM, 1) : col(LINE, 1));
	}

	// facts
	const char *dt[4] = {"WEEK", "DAY", "ERA", "TODAY"};
	char dd[4][16], sm[4][16];
	snprintf(dd[0], 16, "%d", isoWeek(c)); sm[0][0] = 0;
	snprintf(dd[1], 16, "%d", c->yday + 1); snprintf(sm[1], 16, "/%d", daysInYear(c->y));
	snprintf(dd[2], 16, "%d", c->y - 2018); snprintf(sm[2], 16, "令和");
	int pct = (c->h * 3600 + c->m * 60 + c->s) / 864;
	snprintf(dd[3], 16, "%d", pct); snprintf(sm[3], 16, "%%");
	bool P = g_orient != OR_LAND;
	int cols = P ? 2 : 4, rows = P ? 2 : 1;
	float cw = (g_bw - 20 - (cols - 1)) / cols, chh = 48, gridH = rows * chh + rows + 1;
	rect(10, 26, g_bw - 20, gridH, col(LINE, 1));
	for (int i = 0; i < 4; i++) {
		float x = 10 + (i % cols) * (cw + 1), y = 27 + (i / cols) * (chh + 1);
		rect(x, y, cw, chh, col(INK, 1));
		text(F_MONO, dt[i], x + 7, y + 5, 10, col(DIM, 1), AL_LEFT);
		text(F_DISPLAY, dd[i], x + 7, y + 17, 26, col(TEXT, 1), AL_LEFT);
		float w = text_w(F_DISPLAY, dd[i], 26);
		if (sm[i][0]) text(i == 2 ? F_SYS : F_MONO, sm[i], x + 10 + w, y + 29, i == 2 ? 11 : 10, col(DIM, 1), AL_LEFT);
	}
	float ly = 26 + gridH + 5;
	rect(10, ly, g_bw - 20, 3, col(LINE, 1));
	hgrad(10, ly, (g_bw - 20) * pct / 100.f, 3, col(SIGNAL, 1), col(SODIUM, 1));

	drawButton(&btn[B_ANN], "時報を再生", "ANNOUNCE", "A", true, false);
	drawButton(&btn[B_BGM], "BGM", bgmOn ? "ON" : "OFF", "X", false, bgmOn);
	drawButton(&btn[B_AUTO], "自動時報", autoMode == 2 ? "OFF" : (autoMode == 0 ? "HOURLY" : "EVERY 15"), "Y", false, autoMode != 2);
	snprintf(buf, sizeof buf, "%s", scenes_name());
	drawButton(&btn[B_SCENE], "シーン", buf, "R", false, false);

	float fy = g_bh - 18;
	if (appT - toastT < 1.8f) text(F_SYS, toastMsg, g_bw / 2, fy - 2, 13, col(SIGNAL, 1), AL_CENTER);
	else if (!haveAudio) text(F_SYS, "サウンドなし: SDの /3ds/dspfirm.cdc が必要", g_bw / 2, fy - 1, 11, col(TAIL, 1), AL_CENTER);
	else text(F_MONO, P ? "SELECT: TURN   START: EXIT" : "SELECT: TURN   START: EXIT   3D: DEPTH", g_bw / 2, fy, 10, col(DIM, 1), AL_CENTER);
}

/* ---------------- intro logic ---------------- */
static void introStart(void) {
	Clock c = clock_at(0);
	snprintf(board.target[0], 24, "%04d.%02d.%02d %.3s", c.y, c.mon + 1, c.d, WD_EN[c.wd]);
	snprintf(board.target[1], 24, "%02d:%02d LOCAL", c.h, c.m);
	board.n = 0;
	for (int r = 0; r < 2; r++) {
		int len = strlen(board.target[r]);
		for (int i = 0; i < len && board.n < NCELL; i++) {
			int k = board.n++;
			board.row[k] = r; board.colIdx[k] = i;
			char ch = board.target[r][i];
			board.locked[k] = ch == ' ' || ch == '.' || ch == ':';
			board.shown[k] = board.locked[k] ? ch : '0';
			board.lockAt[k] = 1.1f + 0.45f + k * 0.07f;
		}
	}
	introT = 0; introOut = -1; introReady = false;
}

static void introEnd(bool withSignal) {
	if (introOut >= 0) return;
	introOut = 0;
	faceT = 0;
	for (int i = 0; i < board.n; i++) { board.locked[i] = true; board.shown[i] = board.target[board.row[i]][board.colIdx[i]]; }
	introReady = true;
	if (withSignal) announce(false);
}

static void introUpdate(float dt) {
	introT += dt;
	if (introOut >= 0) {
		introOut += dt;
		if (introOut > 1.2f) state = ST_FACE;
		return;
	}
	bool all = true;
	for (int i = 0; i < board.n; i++) {
		if (board.locked[i]) continue;
		if (introT >= board.lockAt[i]) { board.locked[i] = true; board.shown[i] = board.target[board.row[i]][board.colIdx[i]]; board.lockAt[i] = appT; }
		else {
			all = false;
			char t = board.target[board.row[i]][board.colIdx[i]];
			board.shown[i] = (t >= '0' && t <= '9') ? '0' + rand() % 10 : 'A' + rand() % 26;
		}
	}
	if (all) introReady = true;
	static float readyAt = -1;
	if (introReady && readyAt < 0) readyAt = introT;
	if (introReady && introT - readyAt > 0.6f) introEnd(true);   // the 3DS can play sound right away
}

/* ---------------- orientation ---------------- */
static void setOrient(int o, bool animate) {
	int prev = g_orient;
	gfx_set_orient(o);
	rot.dir = o == OR_CW || prev == OR_CCW ? 1 : -1;
	gfxSet3D(o == OR_LAND);          // the 3D barrier only works with the console held normally
	layoutButtons();
	scenes_resize();
	if (!animate) return;
	rot.active = true; rot.t = 0;
	faceT = 0; faceBase = 0.62f;     // the face re-enters as the slats open
	for (int i = 0; i < 4; i++) digitChange[i] = appT + 0.62f;
}

// Gravity reads about 512 per g. Held normally it falls on the y/z axes; turned like a book it falls on x,
// and the sign of x tells which way the console was turned: positive x = top screen on the left.
static void orientUpdate(float dt) {
	static float ax, ay, az = 512, holdT;
	static int cand = OR_LAND;
	accelVector a; hidAccelRead(&a);
	ax += (a.x - ax) * 0.2f; ay += (a.y - ay) * 0.2f; az += (a.z - az) * 0.2f;
	int want = g_orient;
	if (orientMode == 0) {
		float mag = sqrtf(ax * ax + ay * ay + az * az);
		if (mag > 200) {
			if (fabsf(ax) > 0.72f * mag) want = ax > 0 ? OR_CCW : OR_CW;   // sign checked on real hardware (emulators report it the other way)
			else if (fabsf(ax) < 0.45f * mag) want = OR_LAND;
		}
	} else want = orientMode == 1 ? OR_LAND : orientMode == 2 ? OR_CCW : OR_CW;
	if (want == g_orient) { cand = want; holdT = 0; return; }
	if (want != cand) { cand = want; holdT = 0; }
	holdT += dt;
	if (holdT > 0.35f || orientMode) setOrient(want, !(state == ST_INTRO && introOut < 0));
}

static void rotLine(float cx, float cy, float ang, float sc, float x0, float y0, float x1, float y1, u32 c) {
	float cs = cosf(ang) * sc, sn = sinf(ang) * sc;
	line(cx + x0 * cs - y0 * sn, cy + x0 * sn + y0 * cs, cx + x1 * cs - y1 * sn, cy + x1 * sn + y1 * cs, 3, c, c);
}

// A frame turns into the new orientation, then slats clear to reveal the re-laid-out screen.
static void drawRotor(float W, float H, bool topScreen) {
	if (!rot.active) return;
	float t = rot.t;
	bool P = g_orient != OR_LAND;
	for (int i = 0; i < 7; i++) {
		float e = ease((t - 0.6f - i * 0.04f) / 0.6f);
		u32 c = col(i % 3 == 1 ? INK2 : INK, 1);
		if (P) { float h = H / 7, d = e * (W + 4) * (i % 2 ? 1 : -1); rect(d, i * h, W, h + 1, c); }
		else { float w = W / 7, d = e * (H + 4) * (i % 2 ? 1 : -1); rect(i * w, d, w + 1, H, c); }
	}
	float sp = ease(t / 0.7f);
	if (sp < 1) rect(0, sp * H, W, 2, col(SIGNAL, 1 - sp));
	float k = ease(t / 0.52f), out = span(t, 0.52f, 0.95f);
	float ang = rot.dir * (M_PI / 2) * (1 - k);
	float sc = lerpf(0.62f, 1, k) * lerpf(1, 1.12f, out), a = 1 - out;
	if (a <= 0) return;
	float fw = W * 0.46f, fh = H * 0.46f, cx = W / 2, cy = H / 2;
	u32 c = col(SIGNAL, a);
	for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) {
		float px = sx * fw / 2, py = sy * fh / 2;
		rotLine(cx, cy, ang, sc, px, py, px - sx * 18, py, c);
		rotLine(cx, cy, ang, sc, px, py, px, py - sy * 18, c);
	}
	if (topScreen && k > 0.6f) {
		Clock now = clock_at(0);
		char hm[8]; snprintf(hm, sizeof hm, "%02d:%02d", now.h, now.m);
		text(F_MONO, P ? "PORTRAIT" : "LANDSCAPE", cx - fw / 2 + 8, cy - fh / 2 + 7, 10, c, AL_LEFT);
		text(F_CLOCK, hm, cx, cy - 34, 64, col(SODIUM, a), AL_CENTER);
	}
}

/* ---------------- input ---------------- */
static bool hit(const Button *b, float u, float v) { return u >= b->x && u < b->x + b->w && v >= b->y && v < b->y + b->h; }

static void press(int which) {
	btn[which].pressT = appT;
	switch (which) {
	case B_ANN: announce(true); break;
	case B_BGM: bgmOn = !bgmOn; synth_music(bgmOn || ann.active); break;
	case B_AUTO: autoMode = (autoMode + 1) % 3; break;
	case B_SCENE: if (!scenes_busy()) scenes_next(); break;
	}
}

int main(void) {
	gfxInitDefault();
	gfxSet3D(true);
	romfsInit();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(12000);
	C2D_Prepare();
	fontEnsureMapped();
	C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	gfx_init();
	srand(osGetTime());
	HIDUSER_EnableAccelerometer();
	layoutButtons();
	haveAudio = synth_init();
	scenes_init();
	introStart();

	int lastMin = -1;
	u64 last = osGetTime();
	while (aptMainLoop()) {
		hidScanInput();
		u32 down = hidKeysDown();
		if (down & KEY_START) break;
		if (down & KEY_SELECT) { orientMode = (orientMode + 1) % 4; toastMsg = ORIENT_LABEL[orientMode]; toastT = appT; }

		u64 nowMs = osGetTime();
		float dt = clampf((nowMs - last) / 1000.f, 0, 0.05f);
		last = nowMs;
		appT += dt;
		if (faceT >= 0) faceT += dt;

		if (state == ST_INTRO && introOut < 0) {
			if (down & KEY_A) introEnd(true);
			if (down & KEY_B) introEnd(false);
		} else {
			if (down & KEY_A) press(B_ANN);
			if (down & KEY_X) press(B_BGM);
			if (down & KEY_Y) press(B_AUTO);
			if (down & (KEY_R | KEY_L)) press(B_SCENE);
			if (down & KEY_TOUCH) {
				touchPosition tp; hidTouchRead(&tp);
				float u, v; gfx_touch(tp.px, tp.py, &u, &v);
				for (int i = 0; i < NBTN; i++) if (hit(&btn[i], u, v)) press(i);
			}
		}

		Clock c = clock_at(0);
		char hm[5]; snprintf(hm, sizeof hm, "%02d%02d", c.h, c.m);
		for (int i = 0; i < 4; i++) if (digits[i] != hm[i]) { digits[i] = hm[i]; digitChange[i] = appT; }
		if (c.m != lastMin) {
			if (lastMin >= 0) {
				if (state == ST_FACE && !ann.active && ((autoMode == 0 && c.m == 0) || (autoMode == 1 && c.m % 15 == 0))) announce(true);
				if (c.m % SCENE_EVERY == 0 && !scenes_busy()) scenes_next();
			}
			lastMin = c.m;
		}
#ifdef METRO_DEMO   // emulator smoke test: walk through every scene change quickly
		{ static float nextDemo = 14; if (appT > nextDemo && !scenes_busy()) { scenes_next(); nextDemo = appT + 9; } }
		{ static int step = 0; const float at[3] = {13, 25, 37}; const int mode[3] = {2, 3, 1};
		  if (step < 3 && appT > at[step]) { orientMode = mode[step]; step++; } }
#endif

		// beat-synced pulse for the visuals
		double tn = synth_now();
		int step = synth_step_at(tn);
		if (step >= 16) pulse = expf(-(float)(tn - synth_step_time(step - step % 4)) * 8);
		else pulse *= 0.9f;

		orientUpdate(dt);
		if (rot.active && (rot.t += dt) > 1.6f) rot.active = false;
		if (state == ST_INTRO) introUpdate(dt);
		annUpdate(dt);
		scenes_update(dt, pulse, synth_level());

		float slider = g_orient == OR_LAND ? osGet3DSliderState() : 0;
		bool cards = annShowing();
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		gfx_frame_begin();
		for (int eye = 0; eye < (slider > 0 ? 2 : 1); eye++) {
			C3D_RenderTarget *t = eye ? topR : top;
			C2D_TargetClear(t, col(INK, 1));
			C2D_SceneBegin(t);
			gfx_view(true);
			g_eye = (eye ? 1 : -1) * slider * 3.f;
			scenes_draw();
			if (state == ST_INTRO && introOut < 0) drawIntro();
			else {
				if (!cards) drawFace(&c);
				else drawCards();
				if (state == ST_INTRO) drawBlinds(introOut);
			}
			drawRotor(g_w, g_h, true);
		}
		C2D_TargetClear(bot, col(INK, 1));
		C2D_SceneBegin(bot);
		gfx_view(false);
		g_eye = 0;
		drawBottom(&c);
		drawRotor(g_bw, g_bh, false);
		C3D_FrameEnd(0);
	}

	synth_exit();
	gfx_exit();
	C2D_Fini();
	C3D_Fini();
	romfsExit();
	gfxExit();
	return 0;
}
