# -
Nhìn vào các ký hiệu chân SD, VDD, GND, L/R, WS, SCK, đây không phải là cảm biến âm thanh analog thông thường, mà là một Microphone kỹ thuật số sử dụng chuẩn giao tiếp I2S (rất phổ biến là dòng INMP441).

Vì sử dụng chuẩn I2S, module này truyền dữ liệu âm thanh số trực tiếp sang ESP32-S3 thay vì qua chân ADC, cho chất lượng âm thanh cực kỳ sắc nét và chống nhiễu tốt.

SƠ ĐỒ ĐẤU NỐI CHI TIẾT: MICROPHONE I2S (INMP441) VỚI ESP32-S3

VDD	Cấp nguồn dương (3.3V)	Chân 3V3

GND	Nối đất (Ground)	Chân GND

SD (hoặc SDAT)	Serial Data (Dữ liệu âm thanh)	Chân GPIO 7 (hoặc bất kỳ chân GPIO nào làm I2S Data In)

WS (hoặc LRCLK)	Word Select (Xác định kênh Trái/Phải)	Chân GPIO 5 (hoặc chân I2S WS)

SCK (hoặc BCLK)	Serial Clock (Xung nhịp bít)	Chân GPIO 6 (hoặc chân I2S Clock)

L/R	Left / Right Channel Select	Nối trực tiếp vào GND (Chọn kênh Trái - Left) hoặc 3V3 (Chọn kênh Phải)

GIẢI THÍCH Ý NGHĨA CÁC CHÂN:
VDD & GND: Cấp nguồn hoạt động (bắt buộc dùng nguồn 3.3V, tuyệt đối không cắm vào 5V vì sẽ làm cháy chip micro bên trong).

SCK (Serial Clock / BCLK): Xung nhịp đồng bộ từng bit dữ liệu do vi điều khiển hoặc micro cung cấp.

WS (Word Select / LRCLK): Xung nhịp xác định thời điểm truyền dữ liệu của kênh âm thanh bên Trái (Left) hay bên Phải (Right).

SD (Serial Data): Đường truyền tải dữ liệu âm thanh số thô từ micro về chân nhận dữ liệu của ESP32-S3.

L/R (Left/Right): Chân chọn kênh. Khi bạn chỉ dùng 1 micro đơn, bạn thường nối chân này xuống GND để cố định nó chạy ở kênh Trái.