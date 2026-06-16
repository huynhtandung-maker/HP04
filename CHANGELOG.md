# Changelog

Tất cả thay đổi quan trọng của HP04 sẽ được ghi lại tại đây.

Dự án sử dụng quy ước version:

```text
MAJOR.MINOR.PATCH
```

Ví dụ:

```text
v1.1.0
```

* `MAJOR`: thay đổi lớn về kiến trúc hoặc cách triển khai.

* `MINOR`: thêm tính năng mới nhưng vẫn giữ tương thích.

* `PATCH`: sửa lỗi nhỏ, tối ưu ổn định, không đổi hành vi chính.

***

## v1.1.0 - 2026-06-16

### Added

* Bổ sung firmware metadata:

  * `APP_TITLE`

  * `FW_TITLE`

  * `FW_VERSION`

  * `FW_NOTE`

* Bổ sung ThingsBoard device attributes để quản lý thiết bị.

* Bổ sung ThingsBoard RPC:

  * `otaUpdate`

  * `updateFirmware`

  * `resetCounter`

  * `setCounter`

  * `restart`

  * `getStatus`

* Bổ sung OTA từ GitHub raw firmware URL thông qua ThingsBoard RPC.

* Bổ sung OLED main page và status page.

* Bổ sung telemetry định kỳ để ThingsBoard biết thiết bị còn hoạt động.

* Bổ sung `bootCount` và `resetReason`.

### Changed

* Tách cấu hình công khai sang `config.h`.

* Tách WiFi/password/ThingsBoard token sang `secrets.h`.

* Thêm `secrets.example.h` làm file mẫu an toàn.

* Thêm `.gitignore` để loại trừ `secrets.h`.

* WiFi reconnect không còn chặn vòng lặp chính.

* MQTT reconnect không còn chặn vòng lặp chính.

* OLED không còn bị kẹt ở màn hình `System Starting`.

### Security

* Không đưa WiFi password thật lên GitHub.

* Không đưa ThingsBoard token thật lên GitHub.

* Chỉ commit `secrets.example.h`, không commit `secrets.h`.

### Known limitations

* Firmware `.bin` build từ máy local vẫn có thể nhúng secret nếu đang dùng `secrets.h`.

* Chưa có WiFi setup portal.

* Chưa có provisioning flow cho nhiều thiết bị.

* Chưa có production-grade OTA release pipeline.
