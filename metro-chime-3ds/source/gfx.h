// Drawing helpers shared by the scenes and the UI.
#pragma once
#include <citro2d.h>
#include <stdbool.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

// palette (same as the web version)
#define INK    0x090d18
#define INK2   0x121a2c
#define LINE   0x26314b
#define TEXT   0xe9edf6
#define DIM    0x8a94ad
#define SODIUM 0xffb04a
#define SIGNAL 0x5fe3ff
#define TAIL   0xff4f6a
#define GREEN  0x6fdc8c
#define VIOLET 0xb48cff

typedef enum { F_SYS, F_DISPLAY, F_MONO, F_CLOCK } FontId;
enum { AL_LEFT, AL_CENTER, AL_RIGHT };

// Orientation. Held sideways like a book, each screen is drawn rotated so it reads as a portrait screen.
enum { OR_LAND, OR_CW, OR_CCW };   // CW: turned clockwise (top screen on the right); CCW: top screen on the left
extern int g_orient;
extern float g_w, g_h;     // logical size of the top screen in the current orientation
extern float g_bw, g_bh;   // logical size of the bottom screen
void gfx_set_orient(int o);
void gfx_view(bool topScreen);                                   // call right after C2D_SceneBegin
void gfx_touch(int tx, int ty, float *u, float *v);              // physical touch -> logical coordinates

// pixels of horizontal offset per unit of depth for the eye being drawn (0 when 3D is off)
extern float g_eye;
static inline float DX(float depth) { return g_eye * depth; }

void gfx_init(void);
void gfx_exit(void);
void gfx_frame_begin(void);       // clears the per-frame text buffer

u32 col(u32 hex, float a);
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float ease(float k) { k = clampf(k, 0, 1); return k * k * (3 - 2 * k); }
static inline float span(float k, float a, float b) { return clampf((k - a) / (b - a), 0, 1); }

void rect(float x, float y, float w, float h, u32 c);
void vgrad(float x, float y, float w, float h, u32 top, u32 bottom);
void hgrad(float x, float y, float w, float h, u32 left, u32 right);
void line(float x0, float y0, float x1, float y1, float w, u32 c0, u32 c1);
void frame(float x, float y, float w, float h, float t, u32 c);
void disc(float x, float y, float r, u32 c);
void glow(float x, float y, float r, u32 hex, float a);   // soft additive light
void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, u32 c);

void blend_add(bool on);
void clip_top(float x0, float y0, float x1, float y1);    // scissor in logical top-screen coordinates
void clip_off(void);

float text_w(FontId f, const char *s, float size);
void text(FontId f, const char *s, float x, float y, float size, u32 c, int align);
