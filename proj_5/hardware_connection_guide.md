# TÀI LIỆU KỸ THUẬT: TỔNG HỢP SƠ ĐỒ ĐẤU NỐI PHẦN CỨNG ESP32-S3

Tài liệu này tổng hợp toàn bộ sơ đồ chân (pinout) và hướng dẫn đấu nối vật lý giữa board vi điều khiển **ESP32-S3-DevKitC-1 (N16R8)** và các module ngoại vi đã thử nghiệm.

---

## 1. Thông số phần cứng chung
* **Vi điều khiển:** ESP32-S3-DevKitC-1 (Flash 16MB, PSRAM 8MB).
* **Điện áp logic hoạt động:** $3.3\text{V}$ (Tuyệt đối không cấp nguồn $5\text{V}$ vào các chân GPIO hoặc chân VDD của các module logic nhỏ để tránh cháy chip).
* **Mức chuẩn chung:** Tất cả các module đều dùng chung điểm **GND** để khép kín mạch điện.

---

## 2. Module Cảm ứng chạm (Touch Sensor - TTP223)
Module cảm ứng điện dung đơn, giao tiếp qua mức logic GPIO đơn thuần.

| Ký hiệu trên Module | Chức năng chân | Đấu nối với chân trên ESP32-S3 | Ghi chú kỹ thuật |
| :--- | :--- | :--- | :--- |
| **GND** | Nối đất | Chân **GND** | Khép kín mạch điện, chống trôi tín hiệu. |
| **VCC** | Nguồn dương | Chân **3V3** | Cấp nguồn nuôi mạch cảm ứng ($2.0\text{V} - 5.5\text{V}$). |
| **I/O** (OUT) | Tín hiệu đầu ra | Chân **GPIO 4** | Cấu hình chế độ Input, tắt các điện trở kéo nội. |

---

## 3. Cảm biến khoảng cách TOF050C (Chip VL6180X)
Cảm biến đo khoảng cách hồng ngoại thời gian bay, giao tiếp qua chuẩn **I2C**.

| Ký hiệu trên Module | Chức năng chân | Đấu nối với chân trên ESP32-S3 | Ghi chú kỹ thuật |
| :--- | :--- | :--- | :--- |
| **VIN / VCC** | Nguồn dương | Chân **3V3** | Cấp nguồn hoạt động cho cảm biến. |
| **GND** | Nối đất | Chân **GND** | Nối chung đất hệ thống. |
| **SCL** | I2C Clock | Chân **GPIO 8** | Đường xung nhịp bus I2C. |
| **SDA** | I2C Data | Chân **GPIO 9** | Đường dữ liệu bus I2C. |
| **SHUT / SHDN** | Tắt/Mở cảm biến | Nối trực tiếp lên **3V3** | Kéo lên mức cao để luôn bật cảm biến (có thể bỏ trống nếu module tự kéo nội). |
| **INT** (NT) | Chân ngắt | *Bỏ trống (Không cắm)* | Chỉ dùng trong các ứng dụng ngắt nâng cao. |

---

## 4. Microphone Kỹ thuật số INMP441
Microphone thu âm chất lượng cao, giao tiếp qua chuẩn **I2S**.

| Ký hiệu trên Module | Tên đầy đủ / Chức năng | Đấu nối với chân trên ESP32-S3 | Ghi chú kỹ thuật |
| :--- | :--- | :--- | :--- |
| **VDD** | Nguồn dương | Chân **3V3** | Cấp nguồn nuôi micro ($3.3\text{V}$ cố định). |
| **GND** | Nối đất | Chân **GND** | Nối chung đất hệ thống. |
| **SD** | Serial Data (Dữ liệu) | Chân **GPIO 7** | Đường truyền dữ liệu âm thanh thô về ESP32-S3. |
| **WS** | Word Select (LRCLK) | Chân **GPIO 5** | Xung nhịp xác định kênh âm thanh (Trái/Phải). |
| **SCK** | Serial Clock (BCLK) | Chân **GPIO 6** | Xung nhịp đồng bộ bit dữ liệu. |
| **L/R** | Channel Select | Nối trực tiếp vào **GND** | Cố định chọn kênh Trái (Left). |

---

## 5. Màn hình màu TFT LCD 1.5 inch (Chuẩn SPI 4 đường)
Màn hình hiển thị đồ họa màu sắc, giao tiếp qua chuẩn **SPI tốc độ cao**.

| Ký hiệu trên Màn hình | Chức năng chân | Đấu nối với chân trên ESP32-S3 | Ghi chú kỹ thuật |
| :--- | :--- | :--- | :--- |
| **GND** | Nối đất | Chân **GND** | Khép kín mạch điện. |
| **VCC** | Nguồn dương | Chân **3V3** | Cấp nguồn mạch hiển thị và logic. |
| **SCL** (SCK) | SPI Clock | Chân **GPIO 12** | Xung nhịp đồng bộ SPI. |
| **SDA** (MOSI) | SPI Data In | Chân **GPIO 11** | Đường truyền dữ liệu hình ảnh (Master Out Slave In). |
| **RES** (RST) | Reset phần cứng | Chân **GPIO 14** | Chân khởi động lại trạng thái màn hình. |
| **DC** (RS) | Data / Command | Chân **GPIO 13** | Phân biệt gói tin là Lệnh hay Dữ liệu hiển thị. |
| **CS** | Chip Select | Chân **GPIO 10** | Kích hoạt giao tiếp chọn chip SPI. |
| **BLK** (LED) | Đèn nền (Backlight) | Nối trực tiếp lên **3V3** | Bật sáng tối đa đèn nền màn hình. |

---

## 6. Lưu ý khi lắp ráp phần cứng thực tế
1. **Kiểm tra thông mạch:** Luôn dùng đồng hồ vạn năng kiểm tra chế độ thông mạch (tiếng bíp) giữa `VCC` và `GND` trước khi cấp nguồn để tránh chập cháy linh kiện.
2. **Tiếp xúc mối hàn:** Đảm bảo các chân cắm header được hàn ngấu, không bị hiện tượng hàn lạnh hoặc hở mạch ngầm.
3. **Cố định nguồn:** Khi chạy đồng thời nhiều module, nên cấp nguồn ổn định từ nguồn phụ bên ngoài hoặc cổng USB đủ dòng để tránh sụt áp hệ thống.