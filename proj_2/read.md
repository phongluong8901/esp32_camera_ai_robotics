# --- touch
TÀI LIỆU KỸ THUẬT: MODULE CẢM ỨNG CHẠM (TOUCH SENSOR MODULE)
1. Giới thiệu phần cứng
Tên thiết bị: Module cảm ứng điện dung (phổ biến nhất là IC TTP223).

Nguyên lý hoạt động: Sử dụng công nghệ cảm ứng điện dung (Capacitive Touch). Khi có ngón tay (hoặc vật thể dẫn điện) lại gần hoặc chạm vào bề mặt điện cực, điện dung tại điểm đó thay đổi, mạch điện tử sẽ lập tức chuyển đổi trạng thái tín hiệu ở chân đầu ra.

Điện áp hoạt động: Thường từ 2.0V đến 5.5V (tương thích hoàn hảo với nguồn 3.3V của các dòng board ESP32 / ESP32-S3).


2. Sơ đồ đấu dây chi tiết (Wiring Diagram)
Module cảm ứng chạm loại đơn giản thường có 3 chân cơ bản, được đấu nối trực tiếp với board ESP32-S3-DevKitC-1:


Ký hiệu chân trên ModuleChức năng chânĐấu nối với chân tương ứng trên ESP32-S3Ghi chú kỹ thuậtGNDChân nối đất (Ground)Chân GNDNối chung đất để khép kín mạch điện và chuẩn hóa mức tham chiếu điện áp.

VCC	Chân cấp nguồn dương	Chân 3V3 (hoặc 3.3V)	Cấp nguồn nuôi mạch điện tử bên trong module. Tránh cắm nhầm sang 5V nếu không cần thiết.

I/O (hoặc OUT)	Chân xuất tín hiệu dữ liệu	Chân GPIO 4 (hoặc bất kỳ chân GPIO Input nào)	Truyền mức điện áp logic (0 hoặc 1) về vi điều khiển để thông báo trạng thái chạm.

3. Giải thích chi tiết ý nghĩa từng chân phần cứng
Chân GND (Ground): Cực âm nguồn (0V), đóng vai trò mốc chuẩn điện áp cho toàn bộ hệ thống giữa ESP32-S3 và module cảm ứng.

Chân VCC (Voltage Common Collector): Nhận nguồn điện áp dương 3.3V từ vi điều khiển để duy trì hoạt động cho chip cảm ứng trên module.

Chân I/O / OUT (Input/Output Signal):

Đây là đường truyền thông tin cốt lõi.

Ở trạng thái bình thường (không có người chạm), chân này duy trì một mức điện áp cố định.

Khi có ngón tay chạm vào vùng cảm ứng, trạng thái điện áp tại chân này sẽ thay đổi lập tức để báo hiệu cho vi điều khiển xử lý.