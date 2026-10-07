#include <3ds.h>
#include <math.h>
#include "gfx.h"

float g_eye = 0;

static C2D_TextBuf tbuf;
static C2D_Font fonts[4];   // F_SYS stays NULL (system font)

void gfx_init(void) {
	tbuf = C2D_TextBufNew(8192);
	fonts[F_DISPLAY] = C2D_FontLoad("romfs:/display.bcfnt");
	fonts[F_MONO] = C2D_FontLoad("romfs:/mono.bcfnt");
	fonts[F_CLOCK] = C2D_FontLoad("romfs:/clock.bcfnt");
}

void gfx_exit(void) {
	for (int i = 1; i < 4; i++) if (fonts[i]) C2D_FontFree(fonts[i]);
	C2D_TextBufDelete(tbuf);
}

void gfx_frame_begin(void) { C2D_TextBufClear(tbuf); }

u32 col(u32 hex, float a) {
	return C2D_Color32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, (u8)(clampf(a, 0, 1) * 255));
}

void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, c); }
void vgrad(float x, float y, float w, float h, u32 a, u32 b) { C2D_DrawRectangle(x, y, 0.5f, w, h, a, a, b, b); }
void hgrad(float x, float y, float w, float h, u32 a, u32 b) { C2D_DrawRectangle(x, y, 0.5f, w, h, a, b, a, b); }
void line(float x0, float y0, float x1, float y1, float w, u32 c0, u32 c1) { C2D_DrawLine(x0, y0, c0, x1, y1, c1, w, 0.5f); }
void disc(float x, float y, float r, u32 c) { if (r > 0.3f) C2D_DrawCircleSolid(x, y, 0.5f, r, c); }

void frame(float x, float y, float w, float h, float t, u32 c) {
	rect(x, y, w, t, c); rect(x, y + h - t, w, t, c);
	rect(x, y + t, t, h - 2 * t, c); rect(x + w - t, y + t, t, h - 2 * t, c);
}

void glow(float x, float y, float r, u32 hex, float a) {
	if (a <= 0.004f || r < 0.5f) return;
	disc(x, y, r, col(hex, a * 0.22f));
	disc(x, y, r * 0.7f, col(hex, a * 0.3f));
	disc(x, y, r * 0.42f, col(hex, a * 0.45f));
}

void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c) {
	C2D_DrawTriangle(x0, y0, c, x1, y1, c, x2, y2, c, 0.5f);
	C2D_DrawTriangle(x0, y0, c, x2, y2, c, x3, y3, c, 0.5f);
}

void blend_add(bool on) {
	C2D_Flush();
	if (on) C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_SRC_ALPHA, GPU_ONE);
	else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
}

// The top framebuffer is stored rotated (240 wide, 400 tall), so screen x/y swap and flip.
void clip_top(float x0, float y0, float x1, float y1) {
	C2D_Flush();
	int l = (int)clampf(TOP_H - y1, 0, TOP_H), t = (int)clampf(TOP_W - x1, 0, TOP_W);
	int r = (int)clampf(TOP_H - y0, 0, TOP_H), b = (int)clampf(TOP_W - x0, 0, TOP_W);
	if (r <= l) r = l + 1;
	if (b <= t) b = t + 1;
	C3D_SetScissor(GPU_SCISSOR_NORMAL, l, t, r, b);
}

void clip_off(void) { C2D_Flush(); C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0); }

static float scale_for(FontId f, float size) {
	// citro2d draws every font as if its glyph cell were 30px tall at scale 1,
	// so one unit of scale gives lineFeed * 30 / cellHeight pixels of line height.
	FINF_s *info = C2D_FontGetInfo(fonts[f]);   // NULL font = the console's system font
	if (!info || !info->tglp || !info->tglp->cellHeight) return size / 30.f;
	return size / (info->lineFeed * 30.f / info->tglp->cellHeight);
}

static bool parse(FontId f, const char *s, C2D_Text *t) {
	if (!s || !*s) return false;
	const char *end = C2D_TextFontParse(t, fonts[f], tbuf, s);
	(void)end;
	C2D_TextOptimize(t);
	return true;
}

float text_w(FontId f, const char *s, float size) {
	C2D_Text t;
	if (!parse(f, s, &t)) return 0;
	float sc = scale_for(f, size), w, h;
	C2D_TextGetDimensions(&t, sc, sc, &w, &h);
	return w;
}

void text(FontId f, const char *s, float x, float y, float size, u32 c, int align) {
	C2D_Text t;
	if (!parse(f, s, &t)) return;
	float sc = scale_for(f, size);
	if (align != AL_LEFT) {
		float w, h;
		C2D_TextGetDimensions(&t, sc, sc, &w, &h);
		x -= align == AL_CENTER ? w / 2 : w;
	}
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sc, sc, c);
}
