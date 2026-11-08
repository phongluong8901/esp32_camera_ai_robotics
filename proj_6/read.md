# --- Module Motor TB6612FNG ---

### Bảng sơ đồ chân module TB6612FNG:

#### Hàng bên trái (Chân điều khiển nối với ESP32-S3):
- **PWMA**: Chân PWM tốc độ motor A -> Nối **GPIO 1**
- **AIN2**: Chân hướng 2 motor A -> Nối **GPIO 3**
- **AIN1**: Chân hướng 1 motor A -> Nối **GPIO 2**
- **STBY**: Chân kích hoạt module (Standby) -> Nối **GPIO 4** (hoặc nối thẳng lên 3.3V)
- **BIN1**: Chân hướng 1 motor B -> Nối **GPIO 6**
- **BIN2**: Chân hướng 2 motor B -> Nối **GPIO 7**
- **PWMB**: Chân PWM tốc độ motor B -> Nối **GPIO 5**
- **GND**: Chân mass logic -> Nối **GND** ESP32-S3

#### Hàng bên phải (Chân nguồn và động cơ):
- **VM**: Nguồn cấp động cơ (5V - 12V nguồn pin/adapter ngoài; *không lấy từ chân 3.3V của ESP32*)
- **VCC**: Nguồn logic IC -> Nối **3.3V** của ESP32-S3
- **GND**: Nối chung mass với GND nguồn ngoài và GND ESP32
- **AO1 / AO2**: Ngõ ra nối vào 2 cực của Động cơ A
- **BO2 / BO1**: Ngõ ra nối vào 2 cực của Động cơ B
- **GND**: Nối mass
