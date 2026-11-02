1. Hướng 1: Chạy AI trực tiếp hoàn toàn trên ESP32-S3 (Edge AI)

Sử dụng bộ thư viện chính hãng ESP-WHO hoặc ESP-DL của Espressif.

Thư viện này đã có sẵn các ví dụ mẫu (Examples) chạy sẵn cho camera OV2640/OV5640 như: nhận diện khuôn mặt (face_detection), phát hiện bàn tay, hoặc nhận diện mèo (cat_detection). Bạn chỉ cần nạp vào là chạy.

Face detection / recognition (nhận diện mặt người)
Person detection (có người / không có người)
Object detection (YOLO-style nhiều class)
Custom classification (train model nhận đồ vật riêng)


2. Hướng 2: ESP32-S3 làm nhiệm vụ truyền Stream Video, AI xử lý ở nơi khác (Hybrid)

ESP32-S3 đóng vai trò là "mắt thần" phát luồng video MJPEG qua Wi-Fi.

Một máy tính cá nhân, Raspberry Pi, hoặc một Server nhỏ trong mạng LAN (chạy Python, OpenCV, YOLOv8) sẽ nhận luồng video này để thực hiện tracking, nhận diện vật thể phức tạp với độ chính xác cao, sau đó gửi lệnh điều khiển ngược lại cho ESP32 (ví dụ: quay motor, hú còi, bật đèn).

# ---
