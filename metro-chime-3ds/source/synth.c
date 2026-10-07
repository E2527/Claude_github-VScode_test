// Metro Chime synthesizer: the same groove as the web version (122 BPM, Am9 - Fmaj9 - Cmaj7 - Em7),
// built from oscillators, noise, filters, a dotted-eighth delay and a small Schroeder reverb.
#include <3ds.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "synth.h"

#define RATE     22050
#define NBUF     4
#define BUFLEN   512
#define NVOICE   56
#define TWO_PI   6.28318530718f

static const double STEP_LEN = RATE * 60.0 / SYN_BPM / 4.0;  // samples per 16th note

typedef enum { V_FREE, V_KICK, V_HAT, V_CLAP, V_BASS, V_ARP, V_PAD, V_BELL, V_RISER, V_ZAP, V_PIP } VType;

typedef struct {
	VType type;
	float t, life;          // seconds since start, seconds until the voice is freed
	float f, f2, ph, ph2;   // frequencies and phases
	float amp, dec, att, len;
	float lp, bp, hpx, hpy; // filter state
} Voice;

static Voice voices[NVOICE];
static s16 *audioMem;
static ndspWaveBuf wbuf[NBUF];
static Thread thread;
static LightEvent ev;
static LightLock lk;
static volatile bool running;
static bool haveDsp;

// shared state (guarded by lk)
static int cmdMusic = -1;       // -1 none, 0 stop, 1 start
static int cmdAnnounce = 0;     // 0 none, 1 with count-in, 2 immediate
static int annCount = -1, annCard = -1;
static bool playing, fading;
static double seqStartSample;   // sample at which seqStartStep plays
static int seqStartStep, firstStep;
static double lastFillSample;   // samples generated before the most recent buffer
static u64 lastFillTick;
static volatile float level;

// audio-thread-only state
static double sampleClock;
static int curStep;             // next step to play
static float master = 1.f, masterTarget = 1.f;
static float duck = 1.f;
static u32 rng = 0x12345678;

static float delayBuf[9000]; static int delayPos, delayLen;
static float delayLp;
static float comb[3][900]; static int combLen[3] = {558, 637, 709}, combPos[3]; static float combLp[3];
static float allp[2][260]; static int allpLen[2] = {113, 171}, allpPos[2];

static inline float noise(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(int32_t)rng * (1.f / 2147483648.f); }
static inline float mtof(float m) { return 440.f * powf(2.f, (m - 69.f) / 12.f); }
static inline float envExp(float t, float att, float peak, float dec) {
	if (t < att) return peak * t / att;
	return peak * expf(-(t - att) * 6.9f / dec);
}

static Voice *alloc(VType type, float life) {
	for (int i = 0; i < NVOICE; i++) if (voices[i].type == V_FREE) {
		memset(&voices[i], 0, sizeof(Voice));
		voices[i].type = type; voices[i].life = life;
		return &voices[i];
	}
	return NULL;
}

static void kick(void)                 { alloc(V_KICK, 0.45f); duck = 0.3f; }
static void hat(float v, bool open)    { Voice *x = alloc(V_HAT, open ? 0.3f : 0.08f); if (x) { x->amp = v; x->dec = open ? 0.2f : 0.035f; } }
static void clap(void)                 { alloc(V_CLAP, 0.3f); }
static void bass(int m, float len)     { Voice *x = alloc(V_BASS, len + 0.05f); if (x) { x->f = mtof(m); x->len = len; } }
static void arp(int m)                 { Voice *x = alloc(V_ARP, 0.2f); if (x) x->f = mtof(m); }
static void pad(int m, float dur)      { Voice *x = alloc(V_PAD, dur + 0.5f); if (x) { x->f = mtof(m); x->len = dur; x->ph2 = 0.37f; } }
static void bell(int m, float v)       { Voice *x = alloc(V_BELL, 1.5f); if (x) { x->f = mtof(m); x->amp = v; } }
static void riser(float dur)           { Voice *x = alloc(V_RISER, dur + 0.06f); if (x) x->len = dur; }
static void zap(void)                  { alloc(V_ZAP, 0.2f); }
static void pip(float f, float len)    { Voice *x = alloc(V_PIP, len + 0.05f); if (x) { x->f = f; x->len = len; } }

// One sample of one voice. Writes the dry signal into the bus it belongs to.
static void voiceTick(Voice *v, float dt, float *drum, float *music, float *fx, float *verb, float *dly) {
	float t = v->t, s;
	switch (v->type) {
	case V_KICK: {
		float f = 44.f + 106.f * expf(-t / 0.045f);
		v->ph += f * dt; if (v->ph > 1.f) v->ph -= 1.f;
		*drum += sinf(TWO_PI * v->ph) * envExp(t, 0.003f, 1.f, 0.38f);
		break; }
	case V_HAT: {
		float x = noise();
		v->hpy = 0.55f * (v->hpy + x - v->hpx); v->hpx = x;   // one-pole high-pass
		*drum += v->hpy * envExp(t, 0.001f, v->amp, v->dec);
		break; }
	case V_CLAP: {
		float x = noise(), fc = 2.f * sinf(3.14159f * 1600.f * dt);
		float hp = x - v->lp - 1.2f * v->bp; v->bp += fc * hp; v->lp += fc * v->bp;
		float e = t < 0.033f ? 0.5f * expf(-fmodf(t, 0.011f) * 220.f) : 0.45f * expf(-(t - 0.033f) * 6.9f / 0.19f);
		s = v->bp * e;
		*drum += s; *verb += s;
		break; }
	case V_BASS: {
		v->ph += v->f * dt; if (v->ph > 1.f) v->ph -= 1.f;
		float x = 2.f * v->ph - 1.f;
		float cut = 170.f + 1130.f * expf(-t / 0.06f), fc = 2.f * sinf(3.14159f * cut * dt);
		float hp = x - v->lp - 0.28f * v->bp; v->bp += fc * hp; v->lp += fc * v->bp;
		*music += v->lp * envExp(t, 0.004f, 0.36f, v->len);
		break; }
	case V_ARP: {
		v->ph += v->f * dt; if (v->ph > 1.f) v->ph -= 1.f;
		float x = v->ph < 0.5f ? 1.f : -1.f;
		v->lp += 0.45f * (x - v->lp);
		s = v->lp * envExp(t, 0.002f, 0.06f, 0.15f);
		*music += s; *dly += s * 0.6f;
		break; }
	case V_PAD: {
		v->ph += v->f * dt; if (v->ph > 1.f) v->ph -= 1.f;
		v->ph2 += v->f * 1.00463f * dt; if (v->ph2 > 1.f) v->ph2 -= 1.f;
		float x = (2.f * v->ph - 1.f) + (2.f * v->ph2 - 1.f);
		float sweep = t < v->len * 0.5f ? t / (v->len * 0.5f) : 1.f - (t - v->len * 0.5f) / (v->len * 0.5f);
		float k = 0.13f + 0.18f * fmaxf(0.f, sweep);
		v->lp += k * (x - v->lp);
		float e = t < 0.9f ? t / 0.9f : (t < v->len ? 1.f : fmaxf(0.f, 1.f - (t - v->len) / 0.4f));
		s = v->lp * e * 0.022f;
		*music += s; *verb += s;
		break; }
	case V_BELL: {
		v->ph += v->f * dt; if (v->ph > 1.f) v->ph -= 1.f;
		v->ph2 += v->f * 3.5f * dt; if (v->ph2 > 1.f) v->ph2 -= 1.f;
		float idx = 0.63f * expf(-t / 0.3f);
		s = sinf(TWO_PI * (v->ph + idx * sinf(TWO_PI * v->ph2))) * envExp(t, 0.003f, v->amp, 1.3f);
		*fx += s; *verb += s;
		break; }
	case V_RISER: {
		float x = noise(), p = fminf(1.f, t / v->len);
		float cut = 300.f * powf(3500.f / 300.f, p), fc = 2.f * sinf(3.14159f * cut * dt);
		float hp = x - v->lp - 0.35f * v->bp; v->bp += fc * hp; v->lp += fc * v->bp;
		float e = t < v->len ? 0.0001f * powf(2200.f, p) : 0.f;
		s = v->bp * e;
		*fx += s; *verb += s;
		break; }
	case V_ZAP: {
		float f = 2200.f * powf(110.f / 2200.f, fminf(1.f, t / 0.12f));
		v->ph += f * dt; if (v->ph > 1.f) v->ph -= 1.f;
		*fx += sinf(TWO_PI * v->ph) * envExp(t, 0.002f, 0.1f, 0.12f);
		break; }
	case V_PIP: {   // the classic "pip pip pip poon" time signal tone
		v->ph += v->f * dt; if (v->ph > 1.f) v->ph -= 1.f;
		float e = t < 0.005f ? t / 0.005f : (v->len > 0.5f ? expf(-(t - 0.005f) * 2.2f) : (t < v->len ? 1.f : fmaxf(0.f, 1.f - (t - v->len) / 0.01f)));
		s = sinf(TWO_PI * v->ph) * 0.28f * e;
		*fx += s; *verb += s * 0.4f;
		break; }
	default: break;
	}
	v->t += dt;
	if (v->t >= v->life) v->type = V_FREE;
}

// Am9 - Fmaj9 - Cmaj7 - Em7, two bars each
static const int CH_BASS[4] = {33, 29, 36, 28};
static const int CH_N[4][5] = {{57,60,64,67,71}, {53,57,60,64,67}, {55,59,64,67,71}, {55,59,62,67,71}};
static const u8 BASS_PAT[16] = {0,0,1,1, 0,0,1,0, 0,0,1,1, 0,1,1,0};
static const u8 BASS_OCT[16] = {0,0,0,12, 0,0,0,0, 0,0,12,0, 0,0,12,0};
static const u8 ARP_PAT[16]  = {0,1,2,3, 4,3,2,1, 0,2,4,2, 1,3,4,3};
static const int CARD_BEAT[6] = {0, 1, 3, 4, 5, 7};
static const int CARD_NOTE[6] = {76, 81, 79, 76, 84, 72};

static void playStep(int s) {
	int bar = s / 16, i = s % 16, ch = (bar / 2) % 4;
	float bars = (float)(STEP_LEN * 16 / RATE);
	if (i == 0 && (bar % 2 == 0 || s == firstStep))
		for (int n = 0; n < 5; n++) pad(CH_N[ch][n], bar % 2 == 0 ? bars * 2 : bars);
	if (bar >= 1 && i % 4 == 0) kick();
	if (bar >= 1 && (i == 4 || i == 12)) clap();
	if (i % 4 == 2) hat(0.16f, true);
	else if (bar >= 1) hat(i % 2 ? 0.045f : 0.085f, false);
	if (bar >= 2 && BASS_PAT[i]) bass(CH_BASS[ch] + BASS_OCT[i], (float)(STEP_LEN * 1.5 / RATE));
	if (bar >= 1 && (bar % 4 != 3 || i < 8)) arp(CH_N[ch][ARP_PAT[i]] + 12);

	if (annCount >= 0) {
		int rc = s - annCount;
		if (rc == 0) riser(bars);
		if (rc >= 0 && rc < 16 && rc % 4 == 0) bell(81, 0.07f);
	}
	if (annCard >= 0) {
		int rel = s - annCard;
		if (rel >= 0 && rel % 4 == 0) {
			int beat = rel / 4;
			for (int c = 0; c < 6; c++) if (CARD_BEAT[c] == beat) { bell(CARD_NOTE[c], 0.18f); zap(); }
			if (beat >= 8 && beat <= 10) pip(440.f, 0.1f);
			if (beat == 11) pip(880.f, 1.6f);
			if (beat >= 16) { annCard = -1; annCount = -1; }
		}
	}
}

static void startSeq(int step) {
	playing = true; fading = false;
	master = masterTarget = 1.f;
	curStep = firstStep = seqStartStep = step;
	seqStartSample = sampleClock + 64;
}

static void handleCommands(void) {
	LightLock_Lock(&lk);
	if (cmdMusic == 1 && (!playing || fading)) { if (!playing) startSeq(0); else { fading = false; masterTarget = 1.f; } }
	if (cmdMusic == 0 && playing) { fading = true; masterTarget = 0.f; }
	cmdMusic = -1;
	if (cmdAnnounce) {
		bool countIn = cmdAnnounce == 1, fresh = !playing || fading;
		if (!playing) startSeq(16);
		else if (fading) { fading = false; masterTarget = 1.f; }
		int bar = ((curStep + 15) / 16) * 16;   // next bar line (curStep itself if it is one)
		if (countIn) { annCount = fresh && curStep == firstStep ? bar + 16 : bar; annCard = annCount + 16; }
		else { annCount = -1; annCard = bar; }
		cmdAnnounce = 0;
	}
	LightLock_Unlock(&lk);
}

static void render(s16 *out, int n) {
	const float dt = 1.f / RATE;
	float lev = 0.f;
	for (int k = 0; k < n; k++) {
		if (playing) {
			double next = seqStartSample + (curStep - seqStartStep) * STEP_LEN;
			while (sampleClock >= next) { playStep(curStep); curStep++; next = seqStartSample + (curStep - seqStartStep) * STEP_LEN; }
		}
		float drum = 0, music = 0, fx = 0, verb = 0, dly = 0;
		for (int i = 0; i < NVOICE; i++) if (voices[i].type != V_FREE) voiceTick(&voices[i], dt, &drum, &music, &fx, &verb, &dly);

		// dotted-eighth delay with a darkening feedback loop
		float d = delayBuf[delayPos];
		delayLp += 0.5f * (d - delayLp);
		delayBuf[delayPos] = dly + delayLp * 0.38f;
		if (++delayPos >= delayLen) delayPos = 0;
		music += delayLp * 0.45f; verb += delayLp * 0.2f;

		// small Schroeder reverb
		float r = 0, vin = verb * 0.5f;
		for (int c = 0; c < 3; c++) {
			float y = comb[c][combPos[c]];
			combLp[c] += 0.6f * (y - combLp[c]);
			comb[c][combPos[c]] = vin + combLp[c] * 0.78f;
			if (++combPos[c] >= combLen[c]) combPos[c] = 0;
			r += y;
		}
		for (int a = 0; a < 2; a++) {
			float b = allp[a][allpPos[a]], y = -r + b;
			allp[a][allpPos[a]] = r + b * 0.5f;
			if (++allpPos[a] >= allpLen[a]) allpPos[a] = 0;
			r = y;
		}

		duck += (1.f - duck) * (dt / 0.24f) * 3.f; if (duck > 1.f) duck = 1.f;
		master += (masterTarget - master) * (dt / 0.35f);
		float mix = (drum + music * duck + fx + r * 0.32f) * master;
		mix = mix / (1.f + fabsf(mix));          // soft clip
		out[k] = (s16)(mix * 30000.f);
		lev += fabsf(mix);
		sampleClock += 1.0;
	}
	level = level * 0.6f + 0.4f * fminf(1.f, lev / n * 4.f);

	if (fading && master < 0.002f) {
		LightLock_Lock(&lk);
		playing = false; fading = false;
		LightLock_Unlock(&lk);
		master = masterTarget = 1.f;
	}
}

static void fill(ndspWaveBuf *b) {
	handleCommands();
	LightLock_Lock(&lk);
	lastFillSample = sampleClock;
	lastFillTick = svcGetSystemTick();
	LightLock_Unlock(&lk);
	render(b->data_pcm16, BUFLEN);
	DSP_FlushDataCache(b->data_pcm16, BUFLEN * sizeof(s16));
	ndspChnWaveBufAdd(0, b);
}

static void audioThread(void *arg) {
	(void)arg;
	while (running) {
		for (int i = 0; i < NBUF; i++)
			if (wbuf[i].status == NDSP_WBUF_DONE || wbuf[i].status == NDSP_WBUF_FREE) fill(&wbuf[i]);
		LightEvent_Wait(&ev);
	}
}

static void dspCallback(void *arg) { (void)arg; if (running) LightEvent_Signal(&ev); }

bool synth_init(void) {
	LightLock_Init(&lk);
	delayLen = (int)(STEP_LEN * 3); if (delayLen > 9000) delayLen = 9000;
	if (R_FAILED(ndspInit())) { haveDsp = false; return false; }
	haveDsp = true;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
	float mix[12] = {0}; mix[0] = mix[1] = 1.f;
	ndspChnSetMix(0, mix);
	audioMem = linearAlloc(NBUF * BUFLEN * sizeof(s16));
	memset(audioMem, 0, NBUF * BUFLEN * sizeof(s16));
	memset(wbuf, 0, sizeof(wbuf));
	for (int i = 0; i < NBUF; i++) { wbuf[i].data_vaddr = &audioMem[i * BUFLEN]; wbuf[i].nsamples = BUFLEN; }
	LightEvent_Init(&ev, RESET_ONESHOT);
	running = true;
	ndspSetCallback(dspCallback, NULL);
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	thread = threadCreate(audioThread, NULL, 32 * 1024, prio - 1, -2, false);
	return true;
}

void synth_exit(void) {
	if (!haveDsp) return;
	running = false;
	LightEvent_Signal(&ev);
	threadJoin(thread, U64_MAX);
	threadFree(thread);
	ndspChnReset(0);
	linearFree(audioMem);
	ndspExit();
}

void synth_music(bool on) { LightLock_Lock(&lk); cmdMusic = on ? 1 : 0; LightLock_Unlock(&lk); }
bool synth_playing(void) { LightLock_Lock(&lk); bool p = playing && !fading; LightLock_Unlock(&lk); return p; }

void synth_announce(bool countIn) {
	LightLock_Lock(&lk);
	cmdAnnounce = countIn ? 1 : 2;
	annCount = annCard = -1;
	LightLock_Unlock(&lk);
	if (!haveDsp) {   // no audio: keep the visual timeline running on the system clock
		LightLock_Lock(&lk);
		cmdAnnounce = 0;
		seqStartStep = 16; seqStartSample = synth_now() * RATE;
		annCount = countIn ? 16 : -1; annCard = countIn ? 32 : 16;
		LightLock_Unlock(&lk);
	}
}

void synth_announce_steps(int *countStep, int *cardStep) {
	LightLock_Lock(&lk);
	*countStep = annCount; *cardStep = cmdAnnounce ? -1 : annCard;
	LightLock_Unlock(&lk);
}

double synth_now(void) {
	if (!haveDsp) return osGetTime() / 1000.0;
	LightLock_Lock(&lk);
	double base = lastFillSample - (NBUF - 1) * BUFLEN;
	u64 tick = lastFillTick;
	LightLock_Unlock(&lk);
	double el = (double)(svcGetSystemTick() - tick) / SYSCLOCK_ARM11;
	if (el > (double)BUFLEN / RATE) el = (double)BUFLEN / RATE;
	return base / RATE + el;
}

double synth_step_time(int step) {
	LightLock_Lock(&lk);
	double t = (seqStartSample + (step - seqStartStep) * STEP_LEN) / RATE;
	LightLock_Unlock(&lk);
	return t;
}

int synth_step_at(double t) {
	LightLock_Lock(&lk);
	bool p = playing || !haveDsp;
	double s0 = seqStartSample; int st = seqStartStep;
	LightLock_Unlock(&lk);
	double rel = t * RATE - s0;
	if (!p || rel < 0) return -1;
	return st + (int)(rel / STEP_LEN);
}

float synth_level(void) { return level; }
