#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
  EMOJI_AUTO = 0, // Tự động đổi biểu cảm
  EMOJI_NORMAL,   // Bình thường (có chớp mắt)
  EMOJI_HAPPY,    // Cười híp mắt (^ ^)
  EMOJI_ANGRY,    // Tức giận (mắt đỏ xếch)
  EMOJI_SAD,      // Buồn / Khóc
  EMOJI_SLEEP,    // Ngủ (Zzz)
  EMOJI_LOVE      // Mắt hồng hào hứng
} emoji_mode_t;