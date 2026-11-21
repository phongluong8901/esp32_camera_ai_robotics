# --- motor N20 1218 va module PWM TB6612FNG

Trên mạch trong ảnh của bạn thực tế có 3 chân GND (1 chân góc dưới bên trái, 1 chân góc dưới bên phải, và 1 chân nằm ngay dưới VCC ở cột bên phải), và cả 3 chân này đều được nối thông với nhau bên trong bo mạch.   

Để đi dây gọn và hạn chế nhiễu nhất theo đúng sơ đồ mạch trong ảnh, bạn đấu như sau:

Chân GND ngay dưới VCC (cột bên phải): Nối vào cực âm (-) của nguồn 5V (pin dự phòng), vì nó nằm ngay sát chân nguồn VM.

Chân GND góc dưới bên trái (ngay dưới PWMB): Nối vào chân GND của ESP32-S3, vì toàn bộ cột bên trái này là cụm chân tín hiệu nối sang ESP32.

Chân GND góc dưới bên phải (ngay dưới BO1): Để trống, không cần cắm.