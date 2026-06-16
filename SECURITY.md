# Security Policy

HP04 là firmware IoT có liên quan đến WiFi credentials, ThingsBoard access token, MQTT connection và OTA firmware update. Vì vậy repository này áp dụng nguyên tắc: **không đưa secret thật lên GitHub**.

## 1. Không commit secret

Các file sau không được commit:

```text
secrets.h
credentials.h
config.local.h
.env
*.env
```

Các thông tin sau không được đưa lên GitHub:

```text
WiFi SSID/password thật
ThingsBoard device token thật
MQTT credentials thật
GitHub token
Private OTA URL nếu có chứa thông tin nhạy cảm
Firmware .bin đã build kèm secret thật
```

## 2. File an toàn được phép commit

Các file sau được phép đưa lên GitHub:

```text
HP04.ino
config.h
secrets.example.h
README.md
CHANGELOG.md
SECURITY.md
.gitignore
```

Ý nghĩa:

```text
config.h            chứa cấu hình kỹ thuật công khai
secrets.example.h   chứa mẫu cấu trúc secret, không chứa thông tin thật
secrets.h           chứa thông tin thật, chỉ nằm trên máy local
.gitignore          chặn secrets.h và build output khỏi GitHub
```

## 3. Nếu lỡ lộ secret

Nếu WiFi password hoặc ThingsBoard token từng bị commit lên GitHub:

```text
1. Dừng sử dụng token cũ.
2. Tạo lại ThingsBoard device token.
3. Cập nhật token mới vào secrets.h trên máy local.
4. Nếu WiFi password từng public, cân nhắc đổi WiFi password hoặc tách mạng IoT riêng.
5. Không chỉ xóa file khỏi repo, vì Git history vẫn có thể còn dấu vết.
```

## 4. Firmware binary

Không public file `.bin` nếu firmware đó được build từ máy local có `secrets.h` thật.

Lý do:

```text
Source code có thể không chứa secret,
nhưng file .bin build ra vẫn có thể nhúng WiFi password hoặc ThingsBoard token.
```

Giai đoạn hiện tại, HP04 chỉ nên public source code. Firmware binary dùng cho OTA cần được xử lý cẩn trọng.

## 5. OTA safety

Chỉ kích hoạt OTA khi:

```text
ESP32 dùng partition scheme có hỗ trợ OTA
Firmware build đúng board ESP32-C3
Firmware URL đáng tin cậy
Target version khác version hiện tại
Thiết bị có báo otaStatus về ThingsBoard
```

Không upload firmware `.bin` public nếu chưa chuyển sang mô hình firmware sạch secret.

## 6. Production direction

Hướng production đúng cho HP04:

```text
Firmware không chứa WiFi/password/token thật
WiFi/token được cấu hình qua setup portal
ESP32 lưu cấu hình trong Preferences/NVS
Mỗi thiết bị có device_id/token riêng
ThingsBoard quản lý fleet/device attributes/OTA
GitHub quản lý source code, version, changelog và release note
```

## 7. Current limitation

Phiên bản hiện tại vẫn dùng `secrets.h` local khi build firmware. Đây là cách phù hợp cho prototype/pilot, nhưng chưa phải mô hình production cho hàng loạt thiết bị.

Mục tiêu tiếp theo:

```text
v1.2.0
- Thêm WiFi Setup Portal
- Lưu WiFi/token vào Preferences/NVS
- Thêm nút reset cấu hình
- Chuẩn hóa provisioning cho nhiều thiết bị
- Tách secret khỏi firmware binary
```
