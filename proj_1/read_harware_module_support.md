# --- man hinh
Dưới đây là phân loại chi tiết các dòng màn hình phổ biến (OLED, TFT LCD, e-Paper, IPS LCD giao diện RGB/SPI) cùng ưu nhược điểm và khả năng tương thích với ESP32-S3:

1. Màn hình OLED (Ví dụ: SSD1306, SH1106)
Giao tiếp: I2C (chỉ cần 2 chân SDA, SCL) hoặc SPI.

Kích thước phổ biến: 0.96 inch, 1.3 inch (thường có 2 màu: Trắng, Xanh dương, hoặc Vàng/Xanh kết hợp).

Ưu điểm:

Rất tiết kiệm chân (chỉ tốn 2 chân I2C).

Tiêu thụ điện năng thấp, hiển thị chữ cực kỳ sắc nét, độ tương phản cao (màu đen tuyệt đối vì điểm ảnh tự phát sáng).

Thư viện hỗ trợ trên PlatformIO (như Adafruit_GFX và Adafruit_SSD1306) cực kỳ phong phú, dễ code.

Nhược điểm:

Kích thước nhỏ, chỉ hiển thị được vài dòng chữ hoặc icon đơn giản.

Hoàn toàn không hiển thị được hình ảnh động mượt mà hoặc màu sắc (vì là màn hình đơn sắc).

Ứng dụng trên ESP32-S3: Làm màn hình trạng thái mini (hiển thị IP, nhiệt độ, trạng thái Wi-Fi, đồng hồ).

2. Màn hình TFT LCD SPI (Ví dụ: ST7735, ST7789, ILI9341)
Giao tiếp: SPI tốc độ cao (tốn khoảng 4 đến 6 chân GPIO).

Kích thước phổ biến: 1.8 inch, 2.4 inch, 2.8 inch, 3.2 inch (có màu sắc rực rỡ, độ phân giải từ 128x160 đến 240x320).

Ưu điểm:

Hiển thị đầy đủ màu sắc (Full Color RGB), giá thành rất rẻ, dễ mua.

Tốc độ phản hồi nhanh, có thể vẽ đồ họa, icon màu, hoặc làm giao diện menu (UI) trực quan.

Nhược điểm:

Tốn nhiều chân GPIO hơn OLED.

Góc nhìn (viewing angle) ở các bản TFT giá rẻ thường kém (nhìn nghiêng bị đổi màu), trừ khi chọn loại có tấm nền IPS.

Ứng dụng trên ESP32-S3: Làm các thiết bị điều khiển trung tâm cầm tay, giao diện smart home thu nhỏ, hiển thị thông số cảm biến dạng đồ thị màu.

3. Màn hình LCD dùng giao tiếp song song / RGB / DVP (Ví dụ: Các dòng màn hình cảm ứng qua cổng cáp FPC)
Đặc điểm liên quan trực tiếp đến board của bạn: Trên mạch ESP32-S3 DevKitC-1 của bạn có sẵn một cổng cáp FPC (Camera/LCD Connector) và chip hỗ trợ giao diện LCD/Camera phần cứng.

Giao tiếp: Giao diện RGB Parallel (truyền dữ liệu song song 8-bit hoặc 16-bit) kết hợp bộ điều khiển phần cứng LCD trên chip ESP32-S3.

Kích thước phổ biến: 3.5 inch, 4.3 inch, 5.0 inch (thường đi kèm cảm ứng điện dung tích hợp chuẩn giao diện I2C/SPI cảm ứng).

Ưu điểm:

Tốc độ khung hình (FPS) cực kỳ cao, xem được video mượt mà hoặc chạy giao diện đồ họa nặng (như thư viện LVGL - Light and Versatile Graphics Library) tạo giao diện đẹp như smartphone.

Tận dụng tối đa bộ nhớ 8MB PSRAM của dòng N16R8 để làm bộ đệm hình ảnh (framebuffer).

Nhược điểm:

Chiếm dụng cực kỳ nhiều chân GPIO trên mạch (vì truyền dữ liệu song song nhiều đường cùng lúc).

Lập trình cấu hình phức tạp hơn, đòi hỏi cấp nguồn riêng ổn định (vì màn hình lớn rất ngốn điện).

4. Màn hình e-Paper / e-Ink (Màn hình mực điện tử)
Giao tiếp: SPI.

Kích thước phổ biến: 2.13 inch, 2.9 inch, 4.2 inch,...

Ưu điểm:

Tiết kiệm điện tuyệt đối: Chỉ tiêu thụ điện khi thay đổi hình ảnh. Khi hình ảnh đã hiện lên, ngắt hoàn toàn nguồn điện thì màn hình vẫn giữ nguyên nội dung như tờ giấy in.

Đọc rõ dưới ánh nắng mặt trời gắt, không bị chói.

Nhược điểm:

Tốc độ quét hình ảnh (refresh rate) rất chậm (mất vài giây để đổi khung hình), hoàn toàn không thể hiển thị chuyển động hay video.

Ứng dụng trên ESP32-S3: Làm nhãn giá điện tử, đồng hồ để bàn thông minh chạy bằng pin, trạm thời tiết tĩnh cập nhật vài tiếng một lần.


# --- LCD tft 1,5'
1. Nhóm nguồn điện
GND (Ground): Chân nối đất (cực âm 0V), nối chung với chân GND trên mạch ESP32-S3.

VCC / VDD: Chân cấp nguồn dương cho màn hình. Đối với hầu hết các module LCD nhỏ, bạn cấp nguồn 3.3V từ chân 3V3 của ESP32-S3 (hạn chế cấp 5V trực tiếp nếu module không tích hợp mạch hạ áp riêng để tránh làm hỏng màn hình).

2. Nhóm chân giao tiếp dữ liệu (Chuẩn SPI)
SCL / SCK / SCLK (Serial Clock): Chân xung nhịp đồng bộ. Do vi điều khiển phát ra để đồng bộ hóa tốc độ truyền dữ liệu sang màn hình.

SDA / MOSI (Serial Data / Master Out Slave In): Chân truyền dữ liệu hình ảnh, chữ viết từ ESP32-S3 sang màn hình.

3. Nhóm chân điều khiển phần cứng màn hình
RES / RST (Reset): Chân khởi động lại màn hình. Khi kéo chân này xuống mức thấp (LOW) rồi đưa lên cao (HIGH), màn hình sẽ được xóa sạch và khởi động lại về trạng thái mặc định.

DC / RS / A0 (Data/Command): Chân phân biệt loại dữ liệu. Giúp màn hình hiểu được dữ liệu truyền tới là lệnh cài đặt/cấu hình (Command) hay là mã màu điểm ảnh (Data).

CS (Chip Select): Chân chọn chip màn hình. Khi chân này ở mức thấp (LOW), màn hình mới nhận tín hiệu từ ESP32. Nếu bạn chỉ dùng duy nhất một màn hình trong dự án, đôi khi có thể nối luôn chân này xuống GND.

4. Nhóm chân điều khiển đèn nền
BLK / BL / LED (Backlight): Chân điều khiển bật/tắt hoặc chỉnh độ sáng đèn nền của màn hình.

Nếu bạn nối trực tiếp vào nguồn 3.3V, đèn nền sẽ sáng tối đa liên tục.

Nếu bạn nối vào một chân PWM của ESP32-S3, bạn có thể lập trình thay đổi độ sáng màn hình theo ý muốn (tăng giảm độ sáng linh hoạt).