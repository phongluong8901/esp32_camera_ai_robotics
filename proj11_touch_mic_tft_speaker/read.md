# --- touch, speaker, mic, tft

# --- dau noi (gnd, 3,3v) cua eps32

# --- dau noi (-, +  5v) cua pin (qua mach sac -> dau ra 5v)


Sơ đồ đấu nối chân (Định nghĩa tập trung trong board_pins.h)ModuleChân linh kiệnChân ESP32-S3Ghi chúTFT ST7789SCL / SDA / RES / DC / CS / BLK12 / 11 / 14 / 13 / 10 / 9Giữ nguyên theo cấu hình cũ của bạnTouch TTP223I/O (SIG) / VCC / GNDGPIO 4 / 3.3V / GNDChạm vào đổi mặt + kêu BeepAmp PAM8403LIN (hoặc RIN) / +5V / GNDGPIO 5 / 5V / GNDChân SW bỏ trống (hoặc nối mức cao), loa nối L+ L-Mic INMP441SCK / WS / SD / L/R / VDDGPIO 6 / GPIO 7 / GPIO 15 / GND / 3.3VChân L/R nối GND để chọn kênh Trái
