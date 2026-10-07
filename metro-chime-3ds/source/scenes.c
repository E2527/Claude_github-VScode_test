// Skyline, rain, tunnel and metro map, plus a transition for each change:
//   skyline -> rain   : clouds roll in, lightning (the cut hides under the flash), rain starts
//   rain -> tunnel    : a tunnel portal comes out of the rain and fills the view
//   tunnel -> metro   : headlights, flash, a train pulls in and the camera pulls back onto the map
//   metro -> skyline  : zoom into a station, EXIT sign, a window opens onto the street
#include <3ds.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "scenes.h"

#define W g_w
#define H g_h

static float T, pulse, level;
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }
static u32 hash3(u32 a, u32 b, u32 c) { u32 h = a * 374761393u + b * 668265263u + c * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }

/* ---------------- skyline ---------------- */
#define LW 560
#define NB 40
typedef struct { float x, w, h; u32 seed; u8 antenna, beacon; } Bld;
typedef struct { float depth, speed, hMin, hMax, win, stereo; u32 fill; int n; Bld b[NB]; float off; } Layer;
static Layer layers[3];
typedef struct { int road; float x, len, sp, w; u32 c; } Streak;
#define NS 27
static Streak streaks[NS];
static const struct { float y, slope; int dir; u32 c0, c1; } ROADS[3] = {
	{0.80f, -0.06f, 1, 0xffe2b0, SODIUM}, {0.88f, -0.04f, -1, TAIL, 0xff7a5c}, {0.95f, -0.02f, 1, SIGNAL, SODIUM},
};

static void spawnStreak(Streak *s, int road, bool anywhere) {
	s->road = road;
	s->x = anywhere ? frand(0, W) : (ROADS[road].dir > 0 ? -80 : W + 80);
	s->len = frand(40, 150); s->sp = frand(160, 360); s->w = frand(1, 2.2f);
	s->c = rand() % 10 < 7 ? ROADS[road].c0 : ROADS[road].c1;
}

static void skylineInit(void) {
	const float D[3][7] = {{0.3f, 3, 0.30f, 0.58f, 0.08f, 3.0f, 0}, {0.6f, 7, 0.18f, 0.44f, 0.13f, 2.0f, 0}, {1.0f, 13, 0.08f, 0.28f, 0.18f, 1.0f, 0}};
	const u32 F[3] = {0x141c31, 0x0f1628, 0x070a13};
	for (int l = 0; l < 3; l++) {
		Layer *L = &layers[l];
		L->depth = D[l][0]; L->speed = D[l][1]; L->hMin = D[l][2]; L->hMax = D[l][3]; L->win = D[l][4]; L->stereo = D[l][5];
		L->fill = F[l]; L->n = 0; L->off = frand(0, LW);
		float x = 0;
		while (x < LW && L->n < NB) {
			Bld *b = &L->b[L->n++];
			b->x = x; b->w = 12 + frand(0, 44) * L->depth + 9;
			b->h = H * frand(L->hMin, L->hMax);
			b->seed = rand(); b->antenna = rand() % 4 == 0; b->beacon = rand() % 10 < 3;
			x += b->w + frand(0, 3);
		}
	}
	for (int i = 0; i < NS; i++) spawnStreak(&streaks[i], i % 3, true);
}

static void skylineUpdate(float dt) {
	for (int l = 0; l < 3; l++) layers[l].off = fmodf(layers[l].off + layers[l].speed * dt * (1 + level * 2), LW);
	for (int i = 0; i < NS; i++) {
		Streak *s = &streaks[i];
		int dir = ROADS[s->road].dir;
		s->x += s->sp * dir * dt * (1 + level * 1.5f);
		if ((dir > 0 && s->x - s->len > W + 20) || (dir < 0 && s->x + s->len < -20)) spawnStreak(s, s->road, false);
	}
}

static void skylineDraw(void) {
	vgrad(0, 0, W, H * 0.55f, col(0x090d18, 1), col(0x111633, 1));
	vgrad(0, H * 0.55f, W, H * 0.45f + 1, col(0x111633, 1), col(0x2a1c33, 1));
	vgrad(0, H * 0.35f, W, H * 0.65f, col(SODIUM, 0), col(SODIUM, 0.14f + pulse * 0.12f + level * 0.18f));
	for (int l = 0; l < 3; l++) {
		Layer *L = &layers[l];
		float sx = DX(L->stereo);
		for (int copy = 0; copy < 2; copy++) {
			float base = -L->off + copy * LW + sx;
			for (int i = 0; i < L->n; i++) {
				Bld *b = &L->b[i];
				float x = base + b->x;
				if (x > W + 8 || x + b->w < -8) continue;
				float top = H - b->h;
				rect(x, top, b->w, b->h, col(L->fill, 1));
				if (b->antenna) rect(x + b->w / 2 - 1, top - 10 * L->depth - 4, 2, 10 * L->depth + 4, col(L->fill, 1));
				if (b->beacon) rect(x + b->w / 2 - 1.5f, top - 10 * L->depth - 6, 3, 3, col(TAIL, 0.6f + 0.4f * sinf(T * 2 + b->seed)));
				float ws = 1.5f + L->depth * 1.5f, gap = ws + 2.5f + L->depth * 2.5f;
				int r = 0;
				for (float wy = top + 4; wy < H - 3; wy += gap, r++) {
					int c = 0;
					for (float wx = x + 3; wx < x + b->w - ws - 1; wx += gap * 0.8f, c++) {
						u32 h = hash3(b->seed, r, c);
						if ((h & 1023) < L->win * 1024) {
							float a = 0.2f + ((h >> 10) & 255) / 255.f * 0.5f;
							if (l == 2) a = fminf(1, a + pulse * 0.35f);
							rect(wx, wy, ws, ws * 1.3f, ((h >> 18) & 3) ? col(SODIUM, a) : col(SIGNAL, a * 0.85f));
						}
					}
				}
			}
		}
	}
	blend_add(true);
	for (int i = 0; i < NS; i++) {
		Streak *s = &streaks[i];
		int dir = ROADS[s->road].dir;
		float y0 = H * ROADS[s->road].y + (s->x - W / 2) * ROADS[s->road].slope;
		float x2 = s->x - s->len * dir, y2 = H * ROADS[s->road].y + (x2 - W / 2) * ROADS[s->road].slope;
		line(s->x + DX(0.5f), y0, x2 + DX(0.5f), y2, s->w, col(s->c, 1), col(s->c, 0));
	}
	blend_add(false);
}

/* ---------------- rain ---------------- */
#define NBOKEH 26
#define NDROP 110
#define NBEAD 30
typedef struct { float x, y, r, a, ph, vx; u32 c; } Bokeh;
typedef struct { float x, y, l, v; } Drop;
typedef struct { float x, y, r, v; } Bead;
static Bokeh bokeh[NBOKEH];
static Drop drops[NDROP];
static Bead beads[NBEAD];

static void rainInit(void) {
	const u32 C[5] = {SODIUM, TAIL, SIGNAL, GREEN, 0xffe2b0};
	for (int i = 0; i < NBOKEH; i++) bokeh[i] = (Bokeh){frand(0, W), frand(H * 0.1f, H), frand(10, 40), frand(0.25f, 0.7f), frand(0, 6.28f), frand(-5, 5), C[rand() % 5]};
	for (int i = 0; i < NDROP; i++) drops[i] = (Drop){frand(-W * 0.2f, W), frand(0, H), frand(7, 20), frand(350, 600)};
	for (int i = 0; i < NBEAD; i++) beads[i] = (Bead){frand(0, W), frand(0, H), frand(1.2f, 3.2f), 0};
}

static void dropsUpdate(float dt) {
	for (int i = 0; i < NDROP; i++) {
		Drop *d = &drops[i];
		d->y += d->v * dt; d->x += d->v * dt * 0.15f;
		if (d->y > H + 20) { d->y = -20; d->x = frand(-W * 0.2f, W); }
	}
}

static void dropsDraw(float alpha, int count) {
	u32 c = col(0xb4d2ff, 0.28f * alpha);
	for (int i = 0; i < count && i < NDROP; i++) {
		Drop *d = &drops[i];
		line(d->x + DX(0.3f), d->y, d->x - d->l * 0.15f + DX(0.3f), d->y - d->l, 1, c, c);
	}
}

static void rainUpdate(float dt) {
	for (int i = 0; i < NBOKEH; i++) {
		Bokeh *b = &bokeh[i];
		b->x += b->vx * dt;
		if (b->x < -b->r) b->x = W + b->r;
		if (b->x > W + b->r) b->x = -b->r;
	}
	dropsUpdate(dt);
	for (int i = 0; i < NBEAD; i++) {
		Bead *b = &beads[i];
		if (b->v == 0 && frand(0, 1) < dt * 0.05f) b->v = frand(30, 100);
		b->y += b->v * dt;
		if (b->y > H + 8) { b->y = frand(-8, H * 0.5f); b->x = frand(0, W); b->v = 0; }
	}
}

static void rainDraw(void) {
	vgrad(0, 0, W, H * 0.6f, col(0x060c16, 1), col(0x0b1824, 1));
	vgrad(0, H * 0.6f, W, H * 0.4f + 1, col(0x0b1824, 1), col(0x1b1426, 1));
	blend_add(true);
	for (int i = 0; i < NBOKEH; i++) {
		Bokeh *b = &bokeh[i];
		float a = b->a * (0.75f + 0.25f * sinf(T * 0.8f + b->ph)) + pulse * 0.15f;
		glow(b->x + DX(2.5f), b->y, b->r, b->c, a);
	}
	blend_add(false);
	dropsDraw(1, NDROP);
	for (int i = 0; i < NBEAD; i++) {
		Bead *b = &beads[i];
		disc(b->x + DX(-0.4f), b->y, b->r, col(0xffffff, 0.12f));
		disc(b->x - b->r * 0.3f + DX(-0.4f), b->y - b->r * 0.3f, b->r * 0.35f, col(0xffffff, 0.35f));
	}
}

/* ---------------- tunnel ---------------- */
static float tdist;
typedef struct { float cx, cy, f; } TView;
static TView tview(void) { return (TView){W / 2.f, H * 0.46f, W * 1.3f}; }
static void tp(const TView *v, float x, float y, float z, float *sx, float *sy) { *sx = v->cx + x * v->f / z; *sy = v->cy + y * v->f / z; }

static void tunnelUpdate(float dt) { tdist += dt * 11 * (1 + level * 1.5f); }

static void tunnelDrawView(const TView *v, float dist) {
	rect(0, 0, W, H, col(0x05070d, 1));
	blend_add(true);
	glow(v->cx + DX(4), v->cy, v->f * 0.35f, SODIUM, 0.25f + pulse * 0.1f);
	float sx0, sy0, sx1, sy1;
	// ring ribs
	float ph = fmodf(dist, 3.f);
	for (float z = 3 - ph; z < 36; z += 3) {
		if (z < 0.5f) continue;
		tp(v, -1.6f, -1.1f, z, &sx0, &sy0); tp(v, 1.6f, 1.0f, z, &sx1, &sy1);
		float d = DX(fminf(4, z * 0.12f));
		frame(sx0 + d, sy0, sx1 - sx0, sy1 - sy0, fmaxf(1, 3 / z), col(SIGNAL, fminf(0.22f, 0.9f / z)));
	}
	// ceiling lamps
	ph = fmodf(dist, 1.2f);
	for (float z = 1.2f - ph; z < 40; z += 1.2f) {
		if (z < 0.35f) continue;
		float a = fminf(1, 4 / z) * (0.8f + pulse * 0.2f), d = DX(fminf(4, z * 0.12f));
		for (int s = -1; s <= 1; s += 2) {
			tp(v, 1.15f * s, -1.05f, z, &sx0, &sy0); tp(v, 1.15f * s, -1.05f, z + 0.7f, &sx1, &sy1);
			line(sx0 + d, sy0, sx1 + d, sy1, fmaxf(1.5f, 9 / z), col(SODIUM, a), col(SODIUM, a));
			if (z < 4) glow(sx0 + d, sy0, 50 / z, SODIUM, a * 0.6f);
		}
	}
	// lane markings
	ph = fmodf(dist, 3.5f);
	for (float z = 3.5f - ph; z < 30; z += 3.5f) {
		if (z < 0.4f) continue;
		float d = DX(fminf(4, z * 0.12f));
		for (int s = -1; s <= 1; s += 2) {
			tp(v, 0.55f * s, 1, z, &sx0, &sy0); tp(v, 0.55f * s, 1, z + 1.2f, &sx1, &sy1);
			u32 c = col(TEXT, fminf(0.7f, 1.4f / z));
			line(sx0 + d, sy0, sx1 + d, sy1, fmaxf(1, 5 / z), c, c);
		}
	}
	// green emergency exit signs
	ph = fmodf(dist + 6, 17);
	for (float z = 17 - ph; z < 40; z += 17) {
		if (z < 0.5f) continue;
		tp(v, -1.6f, -0.25f, z, &sx0, &sy0);
		float s = 50 / z;
		rect(sx0 + DX(fminf(4, z * 0.12f)), sy0, s * 0.9f, s * 0.45f, col(GREEN, fminf(0.9f, 3 / z)));
	}
	// the car ahead
	float cz = 9 + sinf(T * 0.35f) * 2.5f;
	for (int s = -1; s <= 1; s += 2) {
		tp(v, 0.28f * s, 0.72f, cz, &sx0, &sy0);
		glow(sx0 + DX(1.2f), sy0, 16 + pulse * 6, TAIL, 0.9f);
	}
	blend_add(false);
}

static void tunnelDraw(void) { TView v = tview(); tunnelDrawView(&v, tdist); }

/* ---------------- metro map ---------------- */
#define NLINE 5
#define NPT 40
typedef struct { u32 c; char letter[2]; float px[NPT], py[NPT], cum[NPT]; int n; u8 station[NPT]; float total, delay; bool noTrains; } MLine;
static MLine mlines[NLINE];
static float mcell, mborn;
typedef struct { float z, fx, fy, cx, cy; } Xf;
static const Xf XF_ID = {1, 0, 0, 0, 0};

static void mlineMake(MLine *L, u32 c, char letter, int i) {
	const int D[8][2] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
	int cols = (int)ceilf(W / mcell), rows = (int)ceilf(H / mcell);
	int x, y, d;
	if (i % 2 == 0) { x = -1; y = 2 + rand() % (rows - 3 > 1 ? rows - 3 : 1); d = 0; }
	else { x = 1 + rand() % (cols - 2); y = -1; d = 2; }
	L->c = c; L->letter[0] = letter; L->letter[1] = 0; L->n = 0; L->delay = i * 0.3f; L->noTrains = false;
	L->px[0] = x * mcell; L->py[0] = y * mcell; L->station[0] = 0; L->n = 1;
	for (int g = 0; g < NPT - 1; g++) {
		int n = g == 0 && i == 0 ? 6 : 2 + rand() % 4;
		x += D[d][0] * n; y += D[d][1] * n;
		L->px[L->n] = x * mcell; L->py[L->n] = y * mcell; L->station[L->n] = 1; L->n++;
		if (x < -1 || y < -1 || x > cols + 1 || y > rows + 1) { L->station[L->n - 1] = 0; break; }
		int t = rand() % 100;
		if (t < 35) d = (d + 1) % 8; else if (t < 70) d = (d + 7) % 8;
		if (i % 2 == 0 && D[d][0] < 0) d = 0;
		if (i % 2 == 1 && D[d][1] < 0) d = 2;
	}
	L->cum[0] = 0;
	for (int k = 1; k < L->n; k++) L->cum[k] = L->cum[k - 1] + hypotf(L->px[k] - L->px[k - 1], L->py[k] - L->py[k - 1]);
	L->total = L->cum[L->n - 1];
}

static void metroInit(void) {
	mcell = fmaxf(20, fminf(W, H) / 9.f);
	mborn = T;
	const u32 C[NLINE] = {SIGNAL, SODIUM, TAIL, GREEN, VIOLET};
	const char *letters = "SGTNV";
	for (int i = 0; i < NLINE; i++) mlineMake(&mlines[i], C[i], letters[i], i);
}

static void mlineAt(const MLine *L, float s, float *x, float *y, int *seg) {
	int k = 1;
	while (k < L->n - 1 && L->cum[k] < s) k++;
	float sl = L->cum[k] - L->cum[k - 1]; if (sl <= 0) sl = 1;
	float f = clampf((s - L->cum[k - 1]) / sl, 0, 1);
	*x = L->px[k - 1] + (L->px[k] - L->px[k - 1]) * f;
	*y = L->py[k - 1] + (L->py[k] - L->py[k - 1]) * f;
	if (seg) *seg = k;
}

static inline void xf(const Xf *v, float x, float y, float *sx, float *sy) {
	*sx = v->cx + (x - v->fx) * v->z; *sy = v->cy + (y - v->fy) * v->z;
}

static void metroDrawXf(const Xf *v) {
	float age = T - mborn, ls = sqrtf(v->z), d = DX(1.5f);
	rect(0, 0, W, H, col(0x0a0f1c, 1));
	// grid
	u32 gc = col(SIGNAL, 0.06f);
	float step = mcell * v->z;
	if (step > 4) {
		float ox, oy; xf(v, 0, 0, &ox, &oy);
		for (float x = fmodf(ox, step) - step; x < W; x += step) rect(x + d, 0, 1, H, gc);
		for (float y = fmodf(oy, step) - step; y < H; y += step) rect(0, y, W, 1, gc);
	}
	for (int i = 0; i < NLINE; i++) {
		MLine *L = &mlines[i];
		float prog = clampf((age - L->delay) / 2.4f, 0, 1);
		float len = L->total * (1 - powf(1 - prog, 3));
		if (len <= 0) continue;
		float w = mcell * 0.17f * ls, ex, ey; int seg;
		mlineAt(L, len, &ex, &ey, &seg);
		u32 c = col(L->c, 1);
		for (int k = 1; k <= seg; k++) {
			float x0, y0, x1, y1;
			xf(v, L->px[k - 1], L->py[k - 1], &x0, &y0);
			if (k == seg) xf(v, ex, ey, &x1, &y1); else xf(v, L->px[k], L->py[k], &x1, &y1);
			line(x0 + d, y0, x1 + d, y1, w, c, c);
			disc(x1 + d, y1, w / 2, c);
		}
		float r = (mcell * 0.15f + pulse * 2) * ls;
		for (int k = 1; k < L->n; k++) {
			if (!L->station[k] || L->cum[k] > len) continue;
			float sx, sy; xf(v, L->px[k], L->py[k], &sx, &sy);
			if (sx < -r * 2 || sx > W + r * 2 || sy < -r * 2 || sy > H + r * 2) continue;
			disc(sx + d, sy, r + 2 * ls, col(INK, 1));
			disc(sx + d, sy, r, col(TEXT, 1));
		}
		if (prog >= 1 && !L->noTrains) {
			blend_add(true);
			for (int n = 0; n < 2; n++) {
				float s = fmodf(T * mcell * 1.8f + n * L->total / 2 + L->delay * 400, L->total), tx, ty, sx, sy;
				mlineAt(L, s, &tx, &ty, NULL); xf(v, tx, ty, &sx, &sy);
				glow(sx + d, sy, (11 + pulse * 5) * ls, L->c, 1);
			}
			blend_add(false);
		}
		float bx, by, sx, sy;
		mlineAt(L, fminf(len, mcell * 1.6f), &bx, &by, NULL); xf(v, bx, by, &sx, &sy);
		disc(sx + d, sy, mcell * 0.34f * ls, c);
		disc(sx + d, sy, mcell * 0.27f * ls, col(INK, 1));
		text(F_DISPLAY, L->letter, sx + d, sy - mcell * 0.24f * ls, mcell * 0.48f * ls, col(TEXT, 1), AL_CENTER);
	}
}

static void metroDraw(void) { Xf v = XF_ID; metroDrawXf(&v); }

/* ---------------- train (side view) ---------------- */
static void drawTrain(float x, float y, float s, u32 stripe, float alpha) {
	if (alpha <= 0) return;
	const float CAR = 300, GAP = 10, total = 3 * CAR + 2 * GAP;
	for (int i = 0; i < 3; i++) {
		float cx = x + (-total / 2 + i * (CAR + GAP)) * s;
		rect(cx + 20 * s, y + 38 * s, (CAR - 40) * s, 10 * s, col(0x20263a, alpha));
		rect(cx, y - 40 * s, CAR * s, 80 * s, col(0xcfd6e4, alpha));
		rect(cx, y + 6 * s, CAR * s, 9 * s, col(stripe, alpha));
		for (int w = 0; w < 5; w++) rect(cx + (22 + w * 56) * s, y - 28 * s, 34 * s, 22 * s, col(0xffe2b0, alpha));
		for (int dd = 0; dd < 3; dd++) {
			float dx = 70 + dd * 80;
			rect(cx + dx * s, y - 32 * s, 20 * s, 66 * s, col(0x8f98ad, alpha));
			rect(cx + (dx + 4) * s, y - 26 * s, 12 * s, 18 * s, col(0xffe2b0, alpha));
		}
	}
	rect(x + (-total / 2 + 4) * s, y + 16 * s, 8 * s, 8 * s, col(0xffffff, alpha));
}

/* ---------------- scene table and transitions ---------------- */
typedef struct { const char *name; void (*init)(void); void (*update)(float); void (*draw)(void); } Scene;
static void noInit(void) {}
static const Scene SCENES[4] = {
	{"SKYLINE", skylineInit, skylineUpdate, skylineDraw},
	{"RAIN", rainInit, rainUpdate, rainDraw},
	{"TUNNEL", noInit, tunnelUpdate, tunnelDraw},
	{"METRO", metroInit, NULL, metroDraw},
};
static int cur = 0, from = -1;
static float tk, tdur;   // transition clock and length

typedef struct { float x, y, r, lag; } Cloud;
#define NCLOUD 22
static Cloud clouds[NCLOUD];
static float trainF[2], stationS[2];

static void transSetup(void) {
	if (cur == 1) {            // skyline -> rain
		tdur = 5;
		for (int i = 0; i < NCLOUD; i++) clouds[i] = (Cloud){frand(-0.1f, 1.1f) * W, frand(-H * 0.05f, H * 0.4f), frand(45, 100), frand(0, 0.25f)};
	} else if (cur == 2) {     // rain -> tunnel
		tdur = 4.5f;
	} else if (cur == 3) {     // tunnel -> metro
		tdur = 6.5f;
		metroInit();
		mlines[0].delay = -5; mlines[0].noTrains = true;
		mborn = T + tdur * 0.45f;
		mlineAt(&mlines[0], mcell * 4, &trainF[0], &trainF[1], NULL);
	} else {                   // metro -> skyline
		tdur = 4.5f;
		float best = 1e9f; stationS[0] = W / 2; stationS[1] = H / 2;
		for (int i = 0; i < NLINE; i++) for (int k = 1; k < mlines[i].n; k++) {
			if (!mlines[i].station[k]) continue;
			float d = hypotf(mlines[i].px[k] - W / 2, mlines[i].py[k] - H * 0.55f);
			if (d < best) { best = d; stationS[0] = mlines[i].px[k]; stationS[1] = mlines[i].py[k]; }
		}
	}
}

static void flash(float k, float at, float width, float peak) {
	float f = 1 - fabsf(k - at) / width;
	if (f > 0) rect(0, 0, W, H, col(0xffffff, peak * f));
}

static void transDraw(float k) {
	if (cur == 1) {
		bool after = k > 0.45f;
		if (after) rainDraw(); else skylineDraw();
		float cover = after ? 1 - ease((k - 0.45f) / 0.55f) : ease(k / 0.5f);
		rect(0, 0, W, H, col(0x05080e, 0.5f * cover));
		for (int i = 0; i < NCLOUD; i++) {
			Cloud *c = &clouds[i];
			float y = after ? lerpf(c->y, -c->r * 2 - 20, ease((k - 0.45f) / 0.55f)) : lerpf(-c->r * 2 - H * 0.2f, c->y, ease((k - c->lag) / 0.5f));
			float x = c->x + DX(3.5f);
			disc(x, y, c->r, col(0x323b58, 0.85f));
			disc(x - c->r * 0.6f, y + c->r * 0.2f, c->r * 0.7f, col(0x2b3350, 0.9f));
			disc(x + c->r * 0.65f, y + c->r * 0.15f, c->r * 0.75f, col(0x2b3350, 0.9f));
			blend_add(true);
			glow(x, y + c->r * 0.55f, c->r * 0.9f, SODIUM, 0.25f * cover);
			blend_add(false);
		}
		flash(k, 0.43f, 0.03f, 0.55f);
		flash(k, 0.475f, 0.015f, 0.9f);
		if (!after) dropsDraw(1, (int)(NDROP * ease((k - 0.3f) / 0.15f)));
	} else if (cur == 2) {
		rainDraw();
		float e = powf(k, 2.3f);
		float pw = lerpf(W * 0.16f, W * 3.6f, e), ph = pw * 0.62f, cx = W / 2.f + DX(1), cy = H * 0.66f;
		float fa = clampf(k * 5, 0, 1);
		// facade around the opening
		float fl = cx - pw * 1.4f, fr = cx + pw * 1.4f, ft = cy - ph * 1.35f;
		u32 wall = col(0x111723, fa);
		rect(fl, ft, fr - fl, cy - ph - ft, wall);
		rect(fl, cy - ph, cx - pw / 2 - fl, H - (cy - ph) + 2, wall);
		rect(cx + pw / 2, cy - ph, fr - (cx + pw / 2), H - (cy - ph) + 2, wall);
		// inside: the tunnel, growing with the opening
		TView v = tview();
		float sc = fminf(1, pw / (W * 1.4f));
		TView in = {lerpf(cx, v.cx, sc), lerpf(cy - ph * 0.4f, v.cy, sc), v.f * sc};
		clip_top(cx - pw / 2, cy - ph, cx + pw / 2, H);
		tunnelDrawView(&in, tdist);
		clip_off();
		// arch corners, lamps on the facade and the green road sign
		float r = pw * 0.22f;
		C2D_DrawTriangle(cx - pw / 2, cy - ph, wall, cx - pw / 2 + r, cy - ph, wall, cx - pw / 2, cy - ph + r, wall, 0.5f);
		C2D_DrawTriangle(cx + pw / 2, cy - ph, wall, cx + pw / 2 - r, cy - ph, wall, cx + pw / 2, cy - ph + r, wall, 0.5f);
		line(cx - pw / 2 + r, cy - ph, cx + pw / 2 - r, cy - ph, fmaxf(1, pw * 0.012f), col(SODIUM, 0.35f * fa), col(SODIUM, 0.35f * fa));
		float sw = pw * 0.48f, sh = pw * 0.1f, sy = cy - ph - pw * 0.2f;
		rect(cx - sw / 2, sy, sw, sh, col(0x1d6b45, fa));
		frame(cx - sw / 2 + pw * 0.008f, sy + pw * 0.008f, sw - pw * 0.016f, sh - pw * 0.016f, fmaxf(1, pw * 0.005f), col(TEXT, fa));
		if (sh > 6) text(F_DISPLAY, "TUNNEL  1.2 km", cx, sy + sh * 0.12f, sh * 0.78f, col(TEXT, fa), AL_CENTER);
		dropsDraw(1 - span(k, 0.6f, 0.95f), NDROP);
	} else if (cur == 3) {
		const float Z0 = 40, s0 = H * 0.22f / 80;
		float zt = ease((k - 0.45f) / 0.55f), z = powf(Z0, 1 - zt);
		float cx = lerpf(W / 2.f, trainF[0], zt), cy = lerpf(H / 2.f, trainF[1], zt);
		if (k < 0.23f) {
			tunnelDraw();
			TView v = tview();
			float a = span(k, 0, 0.22f), hz = lerpf(40, 0.7f, a * a), sx, sy;
			blend_add(true);
			for (int s = 0; s < 2; s++) { tp(&v, s ? 1.15f : 0.7f, 0.55f, hz, &sx, &sy); glow(sx + DX(1), sy, 34 / hz + 6, 0xffffff, 1); }
			blend_add(false);
		} else {
			Xf x = {z, trainF[0], trainF[1], cx, cy};
			metroDrawXf(&x);
			float arrive = span(k, 0.24f, 0.45f), speed = powf(1 - arrive, 2);
			float ox = lerpf(W * 3.2f, 0, 1 - powf(1 - arrive, 3)), s = s0 * z / Z0;
			u32 fr = (u32)(T * 30);   // same streaks for both eyes within a frame
			if (speed > 0.02f) for (int i = 0; i < 12; i++) {
				u32 h = hash3(i, fr, 7);
				float yy = cy + ((h & 255) / 255.f * 120 - 60) * s, xx = ((h >> 8) & 1023) / 1023.f * W;
				line(xx, yy, xx + 120 * speed, yy, 1, col(TEXT, 0.2f * speed), col(TEXT, 0));
			}
			drawTrain(cx + ox + DX(-0.5f), cy, s, mlines[0].c, clampf((z - 2) / 6, 0, 1));
			float pill = 1 - clampf((z - 2) / 6, 0, 1);
			if (pill > 0) { blend_add(true); glow(cx + DX(1.5f), cy, 12, mlines[0].c, pill); blend_add(false); }
		}
		flash(k, 0.23f, 0.035f, 0.9f);
	} else {
		float a = ease(k / 0.4f), z = lerpf(1, 6, a) * lerpf(1, 1.3f, span(k, 0.4f, 1));
		float cx = lerpf(stationS[0], W / 2.f, a), cy = lerpf(stationS[1], H * 0.55f, a);
		Xf x = {z, stationS[0], stationS[1], cx, cy};
		metroDrawXf(&x);
		float sa = span(k, 0.15f, 0.3f) * (1 - span(k, 0.5f, 0.6f));
		if (sa > 0) {
			float bx = cx + 30, by = cy - 80;
			rect(bx + DX(-1), by, 104, 38, col(0xffd23f, sa));
			text(F_DISPLAY, "EXIT A2", bx + 9 + DX(-1), by + 1, 24, col(INK, sa), AL_LEFT);
			text(F_SYS, "出口・地上へ", bx + 9 + DX(-1), by + 24, 11, col(INK, sa), AL_LEFT);
		}
		if (k > 0.4f) {
			float R = lerpf(mcell * 0.15f * sqrtf(z), W * 0.75f, powf(span(k, 0.4f, 1), 1.6f)), rh = R * H / W;
			clip_top(cx - R, cy - rh, cx + R, cy + rh);
			skylineDraw();
			clip_off();
			float b = 14, t = 3;
			u32 c = col(SIGNAL, 1 - span(k, 0.85f, 1));
			rect(cx - R, cy - rh, b, t, c); rect(cx - R, cy - rh, t, b, c);
			rect(cx + R - b, cy - rh, b, t, c); rect(cx + R - t, cy - rh, t, b, c);
			rect(cx - R, cy + rh - t, b, t, c); rect(cx - R, cy + rh - b, t, b, c);
			rect(cx + R - b, cy + rh - t, b, t, c); rect(cx + R - t, cy + rh - b, t, b, c);
		}
	}
}

void scenes_init(void) {
	skylineInit(); rainInit(); metroInit();
	T = 0;
}

void scenes_resize(void) {   // the screen changed orientation: rebuild everything for the new size
	skylineInit(); rainInit(); metroInit();
	from = -1;
}

void scenes_next(void) {
	from = cur;
	cur = (cur + 1) % 4;
	if (cur == 0) skylineInit();
	if (cur == 1) rainInit();
	tk = 0;
	transSetup();
}

void scenes_update(float dt, float p, float lv) {
	T += dt; pulse = p; level = lv;
	// keep both ends of a transition moving
	for (int i = 0; i < 4; i++)
		if ((i == cur || (from >= 0 && i == from)) && SCENES[i].update) SCENES[i].update(dt);
	if (from >= 0) {
		tk += dt;
		if (tk >= tdur) {
			from = -1;
			if (cur == 3) for (int i = 0; i < NLINE; i++) mlines[i].noTrains = false;
		}
	}
}

void scenes_draw(void) {
	if (from >= 0) transDraw(tk / tdur);
	else SCENES[cur].draw();
	// darken the top so the clock stays readable
	vgrad(0, 0, W, H * 0.6f, col(INK, 0.5f), col(INK, 0.15f));
}

int scenes_index(void) { return cur; }
int scenes_count(void) { return 4; }
const char *scenes_name(void) { return SCENES[cur].name; }
int scenes_busy(void) { return from >= 0; }
