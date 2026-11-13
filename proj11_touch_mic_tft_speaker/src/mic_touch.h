#pragma once
#include "emoji_types.h"
#include <stdbool.h>
#include <stdint.h>

void mic_touch_task(void *pvParameters);

bool start_recording(int seconds);
bool is_recording(void);
int get_recorded_samples(const int16_t **out_buf);
void play_recording_speaker_async(void);