// Software synthesizer and step sequencer for Metro Chime.
// Runs in its own thread and streams PCM to the DSP through NDSP.
#pragma once
#include <stdbool.h>

#define SYN_BPM 122

bool synth_init(void);          // false if the DSP firmware is missing (no sound)
void synth_exit(void);

void synth_music(bool on);      // start the groove from the top / fade it out
bool synth_playing(void);

// Schedule a time signal on the next bar line. With countIn, a 4-3-2-1 bar comes first.
// Starts the groove if it is not already running.
void synth_announce(bool countIn);
// Steps (16ths) where the count-in and the first card start; -1 while unscheduled / not used.
void synth_announce_steps(int *countStep, int *cardStep);

double synth_now(void);         // seconds of audio currently reaching the speakers
double synth_step_time(int step);
int synth_step_at(double t);    // step being heard at time t (-1 before the groove starts)
float synth_level(void);        // 0..1 loudness for visuals
