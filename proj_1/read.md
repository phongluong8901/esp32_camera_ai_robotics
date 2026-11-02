# --- khi them vao .ini
no se tu load va setup cac lib (12gb espidf)

# --- upload + monitor thang neu cam board, k can build truoc

# --- thong so
B1. check cau hinh


# --- check board
1. Thông tin Chip & CPU
Log báo: Connected to ESP32-S3 on COM3, Chip type: ESP32-S3 (QFN56) (revision v0.2)

Ý nghĩa: Chip xử lý chính là ESP32-S3, bản phần cứng (revision) v0.2.

Xung nhịp CPU hiện tại: cpu freq: 160000000 Hz (Đang chạy ở 160MHz, có thể cấu hình lên tối đa 240MHz).

Địa chỉ MAC: d8:85:ac:ca:3a:20 (Định danh duy nhất của chip).

2. Bộ nhớ Flash (Ổ cứng lưu code)
Log báo: SPI Flash Size : 16MB, detected chip: boya, flash io: dio

Ý nghĩa: Mạch dùng chip Flash 16MB của hãng Boya, giao tiếp ở chế độ DIO tần số 80MHz.

3. Bộ nhớ PSRAM (RAM mở rộng)
Log báo:

octal_psram: vendor id : 0x0d (AP), density : 0x03 (64 Mbit)

esp_psram: Found 8MB PSRAM device, Speed: 80MHz

SPI SRAM memory test OK

esp_psram: Adding pool of 8192K of PSRAM memory to heap allocator

Ý nghĩa: Mạch có 8MB PSRAM ngoài (64 Mbit) của AP Memory. Hệ thống đã test chạy tốt ở tốc độ 80MHz và đã thêm toàn bộ 8MB RAM này vào bộ nhớ dùng chung.

4. Cấu trúc các phân vùng (partitions.csv)
Log báo trong phần Bootloader:

0 nvs: 16 KB (Lưu cấu hình)

1 otadata: 8 KB (Dữ liệu OTA)

2 phy_init: 4 KB (Hiệu chỉnh RF)

3 factory: 2 MB (Vùng chứa chương trình bạn chạy)

4 storage: 10 MB (Vùng lưu file dữ liệu)

Mọi thông số đều được chip ESP32-S3 tự kiểm tra và in trực tiếp qua cổng COM3, hoàn toàn chuẩn xác với dòng bo mạch ESP32-S3 N16R8.