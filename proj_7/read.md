# --- Đây là mạch khuếch đại âm thanh PAM8403 (mạch khuếch đại Class D công suất nhỏ, thường là 2x3W chạy nguồn 5V).

1. Cụm chân phía dưới (Nguồn và Tín hiệu âm thanh đầu vào):
R_IN (hoặc RIN): Tín hiệu âm thanh kênh phải (Right Audio In).

GND: Mass chung (nối chung với GND của ESP32 hoặc nguồn phát).

L_IN (hoặc LIN): Tín hiệu âm thanh kênh trái (Left Audio In).

GND (hoặc GNDR): Mass cho tín hiệu.

SW: Chân công tắc tắt/mở nguồn (thường để trống hoặc nối qua nút nhấn, nếu không dùng có thể bỏ qua).

5V (hoặc +5V): Nguồn cấp 5V cho mạch (nối vào nguồn 5V, lưu ý mạch này chạy nguồn 5V, không cấp nguồn 3.3V vì âm thanh sẽ bị méo hoặc không đủ công suất).

GND: Chân nối đất nguồn cấp.

2. Cụm chân phía trên (Ngõ ra đấu nối với Loa):
PAM8403 là mạch khuếch đại ngõ ra dạng cầu (BTL), tuyệt đối không nối chung các chân âm của loa xuống GND, nếu không sẽ làm cháy IC. Bạn đấu nối 2 loa (Loa trái và Loa phải) vào các chân tương ứng như sau:

L+ và L-: Nối vào 2 dây của Loa kênh Trái (Left Speaker).

R+ và R-: Nối vào 2 dây của Loa kênh Phải (Right Speaker).

1. Cách đấu nối 1 loa với mạch PAM8403Đấu vào loa: Bạn lấy 2 dây của chiếc loa đơn cắm vào 2 chân L+ và L- của mạch PAM8403 (kênh trái). Bỏ trống 2 chân R+ và R-.   Cấp nguồn cho mạch PAM8403:Chân 5V của mạch PAM8403 nối vào nguồn 5V.   Chân GND của mạch PAM8403 nối vào nguồn âm VÀ kéo một dây sang chân GND của ESP32-S3 (bắt buộc phải chung Mass thì âm thanh mới không bị rè hoặc tịt).Tín hiệu âm thanh đầu vào (Audio In):Chân L_IN (hoặc LIN) trên mạch PAM8403 sẽ nhận tín hiệu âm thanh phát ra từ ESP32-S3.   2. Tích hợp âm thanh cho dự án Xiaozhi trên ESP32-S3Trong dự án Xiaozhi, ESP32-S3 sẽ cần đồng thời cả Micro (INMP441) để thu giọng bạn nói và Loa (qua PAM8403) để AI trả lời bằng giọng nói.Phần Micro (INMP441): Giữ nguyên sơ đồ chúng ta vừa test thành công:WS $\rightarrow$ GPIO 5SCK $\rightarrow$ GPIO 6SD $\rightarrow$ GPIO 7L/R $\rightarrow$ GNDVDD $\rightarrow$ 3.3V, GND $\rightarrow$ GND