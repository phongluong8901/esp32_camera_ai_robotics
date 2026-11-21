# --- wifi scan

# --- folder ml
pip install -r ml/requirements.txt

- Mo giao dien Studio (thu du lieu, train, test realtime bang nut bam):
python ml/studio.py <IP_ESP32>

# Hoac dung dong lenh thu cong:
<!-- - thu data:
python ml/collect.py <IP_ESP32>

- train:
python ml/train.py

- chay realtime + map 3d:
python ml/live.py <IP_ESP32> -->

pip install -r ml/requirements.txt

- studio
python ml/studio.py 192.168.1.12

- link python train
http://127.0.0.1:8000/
- link show
http://192.168.1.12/