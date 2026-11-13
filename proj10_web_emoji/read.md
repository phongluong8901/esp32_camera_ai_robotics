# --- man hinh
1. Sơ đồ nối dây (ESP32-S3 <-> TFT 1.54" ST7789)
Trường hợp 1: Mạch có 8 chân (GND, VCC, SCL, SDA, RES, DC, CS, BLK)
Giữ nguyên code ở trên, cắm dây đúng theo bảng:

SCL -> GPIO 12

SDA -> GPIO 11

RES -> GPIO 14

DC  -> GPIO 13

CS  -> GPIO 10

BLK -> GPIO 9 (hoặc bỏ trống cũng sáng, vì đa số mạch ST7789 đã có trở kéo sẵn lên 3.3V).

1. Sơ đồ cắm dây (Theo đúng thứ tự 8 chân trên màn hình của bạn)Thứ tự chân TFTChân ESP32-S3Chức năng1. GNDGNDMass (0V)2. VCC3V3Nguồn 3.3V3. SCLGPIO 12Xung nhịp SPI (SCLK)4. SDAGPIO 11Dữ liệu SPI (MOSI)5. RESGPIO 14Reset màn hình (RST)6. DCGPIO 13Chọn Dữ liệu / Lệnh7. CSGPIO 10Chọn Chip (Chip Select)8. BLKGPIO 9Đèn nền (Backlight)