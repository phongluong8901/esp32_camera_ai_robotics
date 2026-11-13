#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

typedef enum {
  EMOJI_AUTO = 0, // Tự động đổi biểu cảm
  EMOJI_NORMAL,   // Bình thường (có nhép miệng theo mic)
  EMOJI_HAPPY,    // Vui vẻ (^ ^)
  EMOJI_ANGRY,    // Tức giận (khi bị hét to hoặc chọn trên web)
  EMOJI_SAD,      // Buồn bã
  EMOJI_SLEEP,    // Đi ngủ (chạm vào sẽ thức dậy)
  EMOJI_LOVE      // Đáng yêu
} emoji_mode_t;

// Biến toàn cục lưu độ mở miệng theo âm lượng Mic (0 -> 35)
extern volatile int g_mic_mouth_open;
// Biến toàn cục lưu mức âm lượng (0 -> 100%) để hiển thị lên Web
extern volatile int g_mic_volume_percent;