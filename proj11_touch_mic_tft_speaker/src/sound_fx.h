#pragma once
#include "emoji_types.h"
#include <stdint.h>

void init_speaker_pwm(void);
void play_tone(int freq_hz, int duration_ms);
void play_emoji_sound(emoji_mode_t mode);
void play_touch_beep(void);
void play_pcm_samples(const int16_t *samples, int count, int sample_rate);