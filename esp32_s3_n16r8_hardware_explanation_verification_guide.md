# HƯỚNG DẪN CHI TIẾT & KIỂM TRA PHẦN CỨNG ESP32-S3 (N16R8)

---

## I. GIẢI THÍCH CÁC THÔNG SỐ PHẦN CỨNG (Dành cho người mới)

### 1. Ký hiệu **N16R8** có nghĩa là gì?
* **N16 (Flash 16MB):** 
  * Giống như **Ổ cứng (SSD)** của máy tính hay bộ nhớ trong của điện thoại.
  * Là nơi lưu trữ code chương trình, lưu dữ liệu hệ thống (WiFi, mật khẩu) và lưu file ngay cả khi **mất điện**.
* **R8 (PSRAM 8MB):** 
  * Giống như **RAM** của máy tính.
  * Dùng để chứa dữ liệu xử lý tạm thời lúc mạch đang chạy (ví dụ: ảnh từ Camera, bộ đệm âm thanh, mô hình AI). **Mất điện sẽ mất sạch dữ liệu này**.
  * Dòng ESP32-S3 bình thường chỉ có ~512KB RAM nội, chip của bạn có thêm **8MB PSRAM ngoài (OPI - Octal SPI)** chạy rất nhanh.

### 2. Ý nghĩa cấu hình trong `platformio.ini`
* **`framework = espidf`**: Sử dụng thư viện gốc chính hãng từ Espressif (cho hiệu năng tối đa, chạy nhanh và tiết kiệm RAM hơn Arduino).
* **`flash_size = 16MB`**: Báo cho công cụ nạp biết mạch có ổ cứng 16MB để không bị cắt xén bộ nhớ.
* **`flash_mode = dio`**: Chế độ đọc Flash chuẩn tương thích với chip Boya trên mạch.

### 3. Ý nghĩa cấu hình trong `sdkconfig.defaults`
* **`CONFIG_ESP32S3_SPIRAM_SUPPORT=y`**: Bật công tắc cho phép ESP32-S3 dùng thanh RAM ngoài (PSRAM).
* **`CONFIG_SPIRAM_MODE_OCT=y`**: Chọn chế độ **8 đường truyền dữ liệu (Octal)** cho RAM, giúp truyền dữ liệu siêu tốc.
* **`CONFIG_SPIRAM_SPEED_80M=y`**: Đặt tốc độ RAM chạy ở **80 MHz**.
* **`CONFIG_SPIRAM_USE_MALLOC=y`**: Cho phép bạn dùng hàm cấp phát bộ nhớ chuẩn trong C (`malloc` hoặc `heap_caps_malloc`) để gọi RAM 8MB này ra dùng dễ dàng.

---

## II. BẢNG PHÂN VÙNG BỘ NHỚ FLASH 16MB (`partitions.csv`)

Tổng dung lượng 16MB Flash được chia thành các khu vực như sau:

| Tên phân vùng | Dung lượng | Chức năng giải thích ngắn gọn |
| :--- | :--- | :--- |
| **`nvs`** | 16 KB | Lưu mật khẩu WiFi, cấu hình nhỏ không bị mất khi cúp điện. |
| **`otadata`** | 8 KB | Quản lý việc cập nhật code từ xa qua mạng (OTA). |
| **`phy_init`** | 4 KB | Dữ liệu hiệu chỉnh sóng WiFi/Bluetooth. |
| **`factory`** | **2 MB** | Chứa file chương trình (Firmware) do bạn viết và nạp vào. |
| **`storage`** | **10 MB** | Vùng trống siêu lớn dùng để lưu file, ảnh, web server, v.v. |

---

## III. CODE C DÙNG ĐỂ KIỂM TRA TOÀN BỘ PHẦN CỨNG (`src/main.c`)

Chép đoạn code này vào file `src/main.c` của dự án để kiểm tra chính xác các thông số phần cứng bất kỳ lúc nào:

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "esp_chip_info.h"

void check_hardware(void) {
    printf("\n==================================================\n");
    printf("         KIỂM TRA PHẦN CỨNG ESP32-S3 (N16R8)\n");
    printf("==================================================\n");

    // 1. Kiểm tra Thông tin Chip CPU
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    printf("[1] THÔNG TIN CPU:\n");
    printf("    - Dòng Chip: ESP32-S3\n");
    printf("    - Số nhân CPU: %d core(s)\n", chip_info.cores);
    printf("    - Revision chip: v%d.%d\n", chip_info.revision / 100, chip_info.revision % 100);

    // 2. Kiểm tra Dung lượng Flash (Ổ cứng 16MB)
    uint32_t flash_size = 0;
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        printf("\n[2] BỘ NHỚ FLASH (Lưu trữ code):\n");
        printf("    - Dung lượng thực tế: %lu MB (%lu Bytes)\n", 
               flash_size / (1024 * 1024), flash_size);
    } else {
        printf("\n[2] BỘ NHỚ FLASH: Không đọc được!\n");
    }

    // 3. Kiểm tra Dung lượng PSRAM (RAM ngoài 8MB)
    printf("\n[3] BỘ NHỚ PSRAM (RAM ngoài 8MB):\n");
    if (esp_psram_is_initialized()) {
        size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        
        printf("    - Trạng thái: ĐÃ BẬT VÀ HOẠT ĐỘNG TỐT!\n");
        printf("    - Tổng dung lượng RAM ngoài: %u MB (%u Bytes)\n", 
               psram_total / (1024 * 1024), psram_total);
        printf("    - RAM ngoài còn trống: %u MB (%u Bytes)\n", 
               psram_free / (1024 * 1024), psram_free);
    } else {
        printf("    - Trạng thái: [!] CHƯA BẬT ĐƯỢC PSRAM!\n");
    }

    // 4. Kiểm tra RAM nội (DRAM)
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    printf("\n[4] BỘ NHỚ SRAM NỘI (RAM trong chip):\n");
    printf("    - RAM nội còn trống: %u KB (%u Bytes)\n", 
           internal_free / 1024, internal_free);

    printf("==================================================\n\n");
}

void app_main(void) {
    // Chờ 1 giây để Serial Monitor ổn định
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Chạy hàm kiểm tra phần cứng
    check_hardware();

    // Vòng lặp báo trạng thái hệ thống
    int count = 0;
    while (1) {
        printf("Hệ thống ESP32-S3 đang hoạt động bình thường... [%d s]\n", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

---

## IV. CÁCH ĐỌC KẾT QUẢ KIỂM TRA BẰNG TAY

Khi bật Serial Monitor (115200 baud), nếu thấy các dòng này nghĩa là mạch hoàn toàn chuẩn:
1. **Dung lượng thực tế: 16 MB** $\rightarrow$ Flash 16MB hoạt động đúng.
2. **Trạng thái: ĐÃ BẬT VÀ HOẠT ĐỘNG TỐT!** $\rightarrow$ PSRAM 8MB đã kích hoạt thành công.
3. **Tổng dung lượng RAM ngoài: 8 MB** $\rightarrow$ Nhận diện đủ 8MB RAM phụ.


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