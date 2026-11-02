# --- lib
#include "esp_chip_info.h": Chứa cấu trúc và hàm để lấy thông tin về dòng chip (số nhân CPU, model chip).

#include "esp_flash.h": Thư viện quản lý và giao tiếp với chip bộ nhớ Flash ngoại vi (giúp đọc dung lượng thực tế của Flash).

#include "esp_heap_caps.h": Cung cấp các hàm quản lý bộ nhớ theo vùng (như phân vùng RAM nội hay PSRAM ngoại vi qua cờ MALLOC_CAP_SPIRAM).

#include "esp_system.h": Chứa các hàm hệ thống cốt lõi của ESP-IDF.

#include "freertos/FreeRTOS.h": Thư viện nền tảng của hệ điều hành thời gian thực FreeRTOS được tích hợp sẵn trong ESP-IDF.

#include "freertos/task.h": Chứa các hàm quản lý tiến trình/tác vụ (task), độ trễ (vTaskDelay), quản lý luồng.

#include <stdio.h>: Thư viện chuẩn C phục vụ cho việc in ấn dữ liệu ra màn hình (hàm printf).

# --- code
void app_main(void): Điểm khởi đầu (entry point) của mọi chương trình chạy trên ESP-IDF (tương tự như hàm main() trong C tiêu chuẩn).

vTaskDelay(pdMS_TO_TICKS(2000));: Tạm dừng hoạt động của tiến trình trong 2000 mili-giây (2 giây). Việc này giúp đợi cho cổng Serial và nguồn điện ổn định hẳn sau khi mạch vừa khởi động xong trước khi in dữ liệu.

esp_chip_info_t chip_info;: Khởi tạo một biến cấu trúc chip_info để chứa thông tin chi tiết về phần cứng chip.

esp_chip_info(&chip_info);: Gọi hàm hệ thống của ESP-IDF để điền dữ liệu phần cứng thực tế vào biến chip_info.

printf(..., chip_info.cores);: In ra màn hình số lượng nhân CPU của chip (với ESP32-S3, giá trị này sẽ trả về 2 vì đây là chip Dual-core).

uint32_t flash_size = 0;: Khai báo biến lưu dung lượng Flash dưới dạng số nguyên không dấu 32-bit, khởi tạo giá trị ban đầu bằng 0.if (esp_flash_get_size(NULL, &flash_size) == ESP_OK): Gọi hàm đọc kích thước chip Flash mặc định. Nếu lệnh thực thi thành công (ESP_OK), nó sẽ gán tổng số bytes vào biến flash_size.flash_size / (1024 * 1024): Đổi đơn vị từ Bytes sang Megabytes (MB) (chia cho $1024 \times 1024$) để dễ đọc. Kết quả in ra sẽ là 16 MB cho bản N16R8.

size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);: Lệnh kiểm tra dung lượng bộ nhớ ngoại vi SPIRAM (PSRAM) thông qua cờ quản lý heap MALLOC_CAP_SPIRAM.

if (psram_total > 0): Kiểm tra xem PSRAM có phản hồi hay không. Nếu lớn hơn 0 nghĩa là hệ thống đã nhận diện thành công RAM phụ (in ra số dung lượng tính bằng MB, đối với bản R8 kết quả sẽ báo khoảng 8 MB).

else: Nếu bằng 0, nghĩa là cấu hình sdkconfig chưa bật tính năng sử dụng PSRAM, hệ thống sẽ in ra câu cảnh báo màu vàng/đỏ.

In dòng kết thúc khối kiểm tra phần cứng.

int count = 0;: Khởi tạo biến đếm giây bắt đầu từ 0.

while (1): Tạo vòng lặp vô hạn để giữ cho chương trình chạy liên tục không bị kết thúc.

printf(..., count++);: In ra màn hình thông báo hệ thống đang chạy kèm theo số giây tăng dần (0 s, 1 s, 2 s,...), sau đó tăng biến count lên 1 đơn vị (count++).

vTaskDelay(pdMS_TO_TICKS(1000));: Tạm dừng tiến trình trong 1000 ms (1 giây) trước khi lặp lại vòng tiếp theo. Lệnh này cực kỳ quan trọng trên FreeRTOS để nhường quyền xử lý CPU cho các tác vụ nền khác (như duy trì kết nối, quản lý mạng/Wi-Fi), tránh gây lỗi tràn bộ đệm hoặc kích hoạt cơ chế ngắt bảo vệ Watchdog Reset (WDT).

# --- stack ini
[env:esp32-s3-devkitc-1]: Định nghĩa một môi trường build (environment) có tên là esp32-s3-devkitc-1. Bạn có thể đặt tên này tùy ý, nhưng để chuẩn theo phần cứng devkit phổ biến.

platform = espressif32: Khai báo nền tảng phần cứng là chip ESPressif ESP32 (PlatformIO sẽ tự động tải bộ công cụ biên dịch - toolchain phù hợp về máy).

board = esp32-s3-devkitc-1: Chọn mẫu board phần cứng gốc trong hệ thống của PlatformIO làm khuôn mẫu để cấu hình các chân, xung nhịp và ánh xạ phần cứng cơ bản.

framework = espidf: Chỉ định sử dụng bộ khung phát triển ESP-IDF chính hãng của Espressif (lập trình bằng C thuần với FreeRTOS, tối ưu hiệu năng và kiểm soát phần cứng sâu hơn so với dùng Arduino framework).

monitor_speed = 115200: Cấu hình tốc độ baud (tốc độ truyền dữ liệu qua cổng Serial) mặc định là 115200 để khi mở Serial Monitor trên VS Code bạn đọc được chữ hiển thị rõ ràng, không bị lỗi ký tự rác.

board_upload.flash_size = 16MB: Báo cho công cụ nạp code (uploader) biết chip của bạn có dung lượng Flash là 16MB, để nó cấp phát đúng dung lượng vùng chứa firmware khi nạp vào mạch.

board_build.flash_size = 16MB: Khai báo cho trình biên dịch biết kích thước tổng thể của chip Flash là 16MB, giúp hệ thống định vị chính xác giới hạn bộ nhớ vật lý.

board_build.partitions = partitions.csv: Chỉ định PlatformIO sử dụng file tùy chỉnh tên là partitions.csv (nằm trong thư mục gốc của project) để chia lại dung lượng bộ nhớ trong của 16MB Flash (ví dụ: chia phân vùng chứa code ứng dụng app0/app1, vùng lưu file dữ liệu NVS, vùng lưu OTA update,... thay vì dùng bảng phân vùng mặc định vốn chỉ nhận diện 2MB hoặc 4MB nhỏ hẹp).

board_build.flash_mode = dio: Cấu hình chế độ giao tiếp giữa CPU và chip Flash là DIO (Dual I/O).

Lý do: Mặc dù có một số board dùng chế độ QIO (Quad I/O), nhưng chế độ DIO mang lại độ ổn định cực kỳ cao, đặc biệt an toàn và tương thích với hầu hết các dòng module ESP32-S3 Flash 16MB trên thị trường, tránh hiện tượng lỗi khởi động (boot loop).

board_build.sdkconfig = sdkconfig.defaults: Trỏ tới file cấu hình sâu cấp hệ thống tên là sdkconfig.defaults.

File này đóng vai trò cực kỳ quan trọng đối với dòng N16R8, vì đây là nơi bạn bật các thông số phần cứng nâng cao mà file platformio.ini không với tới được — tiêu biểu nhất là bật tính năng nhận diện 8MB PSRAM (Octal PSRAM) và cấu hình tần số xung nhịp cho RAM phụ. Nếu thiếu dòng này, hệ thống sẽ không nhận diện được 8MB RAM ngoại vi.