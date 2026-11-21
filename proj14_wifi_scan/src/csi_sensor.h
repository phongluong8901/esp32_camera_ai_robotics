#pragma once
#include <stdbool.h>
#include <stdint.h>


#define CSI_SUB_BINS 16 // Lay mau 16 kenh song mang phu de ve bieu do

typedef struct {
  int8_t rssi;                // Cuong do tin hieu (dBm)
  float variance;             // Chi so nhieu loan CSI (1.0 = nen tinh, >1.6 = chuyen dong)
  float motion;               // Cuong do chuyen dong vuot nen tinh
  float noise_floor;          // Muc nhieu nen tinh da hieu chuan
  uint8_t subs[CSI_SUB_BINS]; // Bien do 16 song mang phu da chuan hoa (0-100)
  uint32_t packet_count;      // Tong so goi tin CSI hop le da thu
  uint32_t raw_cb_count;      // Tong so lan callback CSI duoc goi
  uint16_t pps;               // Toc do goi tin CSI (packets/sec)
  uint16_t last_len;          // Do dai goi CSI gan nhat (bytes)
  bool calibrating;           // Dang hieu chuan nen tinh
  bool mac_filter_active;     // Trang thai bo loc AP BSSID
} csi_metrics_t;

void csi_sensor_init(void);
void csi_sensor_set_target_mac(const uint8_t mac[6]);
void csi_sensor_calibrate(void);
void csi_sensor_get_metrics(csi_metrics_t *out);

#define CSI_RAW_SUBS 62 // 62 subcarrier HT-LTF (bo i=0,1)

typedef struct __attribute__((packed)) {
  uint32_t ts_ms;              // Thoi gian (ms)
  int8_t rssi;                 // RSSI (dBm)
  uint8_t valid_cnt;           // So subcarrier hop le
  uint16_t seq;                // So thu tu goi
  uint16_t amps[CSI_RAW_SUBS]; // Bien do chuan hoa RMS * 1000 (0..65535)
} csi_frame_t;

void csi_sensor_set_stream_enabled(bool enabled);
bool csi_sensor_pop_frame(csi_frame_t *out, uint32_t wait_ms);