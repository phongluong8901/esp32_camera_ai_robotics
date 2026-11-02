# --- TOF050C (dùng chip VL6180X)

Dưới đây là tài liệu kỹ thuật và mã nguồn mẫu để bạn test module đo khoảng cách bằng hồng ngoại thời gian bay TOF050C (dùng chip VL6180X) giao tiếp qua chuẩn I2C trên ESP32-S3 sử dụng framework ESP-IDF trong PlatformIO.

1. Sơ đồ đấu nối phần cứng (Wiring Diagram)
Module TOF050C thường có 4 đến 6 chân. Các chân chính cần đấu nối với ESP32-S3:


VIN (hoặc VCC)	Nguồn điện (3.3V - 5V)	Chân 3V3

GND	Nối đất	Chân GND

SCL	I2C Clock	Chân GPIO 8 (hoặc chân SCL tùy chọn của bạn)

SDA	I2C Data	Chân GPIO 9 (hoặc chân SDA tùy chọn của bạn)

SHUT / SHDN	Chân tắt/mở cảm ứng (Active Low)	Nối trực tiếp lên 3V3 (để luôn bật cảm biến)


2. Cấu hình thư viện I2C trong PlatformIO (platformio.ini)
Vì cảm biến này dùng chuẩn giao tiếp I2C, bạn cần đảm bảo cấu hình project sẵn sàng. (Mặc định ESP-IDF tích hợp sẵn thư viện driver/i2c.h nên bạn không cần cài thêm gói mạng nặng nhọc nào).