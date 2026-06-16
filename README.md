# HP04 - ESP32-C3 Visitor Counter IoT

HP04 là firmware IoT cho thiết bị đếm lượt khách/người đi qua, sử dụng ESP32-C3, cảm biến siêu âm HC-SR04, DHT11, OLED SH1106, NeoPixel, buzzer và ThingsBoard MQTT.

Thiết bị được thiết kế theo hướng không chặn vòng lặp chính: WiFi/MQTT tự reconnect, màn hình không bị kẹt ở trạng thái khởi động, dữ liệu được gửi định kỳ về ThingsBoard, đồng thời hỗ trợ RPC để reset bộ đếm, đặt lại số đếm, restart thiết bị, lấy trạng thái và kích hoạt OTA.

## 1. Tính năng chính

* Đếm lượt người bằng cảm biến siêu âm HC-SR04.
* Đọc nhiệt độ và độ ẩm bằng DHT11.
* Hiển thị trạng thái trên OLED SH1106 1.3 inch I2C.
* Hiển thị phản hồi bằng NeoPixel 12 LED.
* Cảnh báo bằng buzzer.
* Lưu bộ đếm và số lần boot bằng Preferences.
* Gửi telemetry lên ThingsBoard qua MQTT.
* Gửi device attributes gồm firmware version, boot count, reset reason, IP, MAC.
* Hỗ trợ RPC ThingsBoard:

  * `otaUpdate`
  * `updateFirmware`
  * `resetCounter`
  * `setCounter`
  * `restart`
  * `getStatus`
* Hỗ trợ OTA qua firmware URL do ThingsBoard RPC gửi xuống.

## 2. Phần cứng

| Thành phần           | Mô tả                    |
| -------------------- | ------------------------ |
| Board                | ESP32-C3 Dev Module      |
| Distance Sensor      | HC-SR04                  |
| Temperature/Humidity | DHT11                    |
| Display              | OLED SH1106 1.3 inch I2C |
| LED                  | NeoPixel 12 LEDs         |
| Buzzer               | Active buzzer            |
| Cloud                | ThingsBoard MQTT         |

## 3. GPIO mặc định

| Chức năng    |    GPIO |
| ------------ | ------: |
| HC-SR04 TRIG |  GPIO 2 |
| HC-SR04 ECHO |  GPIO 3 |
| DHT11        |  GPIO 4 |
| NeoPixel     |  GPIO 6 |
| I2C SDA      |  GPIO 8 |
| I2C SCL      |  GPIO 9 |
| Buzzer       | GPIO 10 |

Các thông số này nằm trong `config.h` và có thể thay đổi tùy theo mạch thực tế.

## 4. Cấu trúc thư mục

```text
HP04/
├── HP04.ino
├── config.h
├── secrets.example.h
├── secrets.h
└── .gitignore
```

Ý nghĩa:

```text
HP04.ino            logic vận hành chính
config.h            cấu hình công khai, được đưa lên GitHub
secrets.example.h   file mẫu cho WiFi/token, được đưa lên GitHub
secrets.h           WiFi/token thật, không đưa lên GitHub
.gitignore          chặn secret và build output
```

## 5. Cách cấu hình secret

Dự án không đưa WiFi password hoặc ThingsBoard token thật lên GitHub.

Tạo file `secrets.h` từ file mẫu:

```text
Copy secrets.example.h
Rename thành secrets.h
Điền WiFi và ThingsBoard token thật
```

Ví dụ cấu trúc `secrets.h`:

```cpp
#pragma once

const char* WIFI_SSID     = "YOUR_REAL_WIFI";
const char* WIFI_PASSWORD = "YOUR_REAL_WIFI_PASSWORD";

const char* TB_HOST       = "thingsboard.cloud";
const int   TB_PORT       = 1883;
const char* TB_TOKEN      = "YOUR_REAL_THINGSBOARD_DEVICE_TOKEN";
```

Không commit `secrets.h`.

## 6. ThingsBoard telemetry

Firmware gửi telemetry gồm:

```text
peopleCount
distanceCm
wifiRssi
uptimeSec
bootCount
temperature
humidity
```

## 7. ThingsBoard device attributes

Firmware gửi attributes gồm:

```text
app_title
fw_title
fw_version
fw_note
device_model
bootCount
resetReason
ip
mac
otaStatus
otaTargetVersion
otaDetail
```

## 8. RPC hỗ trợ

### resetCounter

```json
{
  "method": "resetCounter",
  "params": {}
}
```

### setCounter

```json
{
  "method": "setCounter",
  "params": {
    "value": 25
  }
}
```

### restart

```json
{
  "method": "restart",
  "params": {}
}
```

### getStatus

```json
{
  "method": "getStatus",
  "params": {}
}
```

### otaUpdate

```json
{
  "method": "otaUpdate",
  "params": {
    "url": "https://raw.githubusercontent.com/USER/REPO/main/firmware/HP04_v1.1.1.bin",
    "version": "v1.1.1"
  }
}
```

## 9. Lưu ý OTA

ESP32 phải dùng partition scheme có hỗ trợ OTA.

Trong Arduino IDE, chọn board ESP32-C3 phù hợp và chọn partition scheme có OTA trước khi upload firmware.

Không nên public file `.bin` nếu firmware đó được build kèm WiFi password hoặc ThingsBoard token thật.

## 10. Version hiện tại

```text
Firmware: v1.1.0
Model: ESP32-C3_HP04
Project: HP04 Visitor Counter
```

## 11. Trạng thái phát triển

HP04 hiện đang ở giai đoạn prototype/pilot.

Mục tiêu tiếp theo:

```text
- Tách WiFi/token khỏi firmware build
- Thêm WiFi setup portal
- Lưu cấu hình thiết bị bằng Preferences/NVS
- Chuẩn hóa OTA production
- Chuẩn hóa ThingsBoard fleet management
```
