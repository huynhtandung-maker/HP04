#pragma once

#include <Arduino.h>
#include <DHT.h>

/*
  ================================================================================
  HP04 - CONFIG CÔNG KHAI
  File này ĐƯỢC đưa lên GitHub.

  Chỉ chứa:
  - tên firmware
  - version
  - GPIO
  - ngưỡng cảm biến
  - thời gian reconnect
  - cấu hình OLED / buzzer / NeoPixel / OTA

  KHÔNG chứa:
  - WiFi password thật
  - ThingsBoard token thật
  ================================================================================
*/

// ================================================================================
// 1. THÔNG TIN PHIÊN BẢN - QUẢN LÝ OTA / GITHUB / THINGSBOARD
// ================================================================================

const char* APP_TITLE   = "HP04";
const char* FW_TITLE    = "HP04_VISITOR_COUNTER";
const char* FW_VERSION  = "v1.2.0-dev";
const char* FW_NOTE     = "Hiểu version và update OTA biến";

// Bật/tắt OTA qua GitHub URL do ThingsBoard gửi xuống.
// Nếu chưa dùng OTA, để false để tránh bấm nhầm từ xa.
#define ENABLE_GITHUB_OTA true

// GitHub raw thường dùng HTTPS.
// Giai đoạn thử nghiệm có thể dùng insecure TLS.
// Khi sản phẩm nghiêm túc hơn, nên thay bằng CA certificate.
#define OTA_ALLOW_INSECURE_TLS true

// ================================================================================
// 2. GPIO ESP32-C3 - GIỮ ĐÚNG THEO BỘ HP04 HIỆN TẠI
// ================================================================================

#define TRIG_PIN     2
#define ECHO_PIN     3

#define PIXEL_PIN    6
#define NUMPIXELS    12

#define BUZZER_PIN   10

#define DHTPIN       4
#define DHTTYPE      DHT11

#define I2C_SDA      8
#define I2C_SCL      9

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_RESET     -1

// ================================================================================
// 3. CẤU HÌNH CẢM BIẾN SIÊU ÂM / ĐẾM NGƯỜI
// ================================================================================

// Khoảng cách xa nhất còn xem là có vật thể phía trước.
// Vượt ngưỡng này: xem như đang scanning, tắt còi/đèn.
const int KHOANG_CACH_XA_NHAT_CM = 150;

// Khi người/vật đi vào gần hơn ngưỡng này thì được tính là 1 lượt.
const int KHOANG_CACH_DEM_CM = 40;

// Phải rời khỏi vùng đếm tới ngưỡng này mới cho phép đếm lượt tiếp theo.
// Nên lớn hơn KHOANG_CACH_DEM_CM để tránh đếm lặp.
const int KHOANG_CACH_NHA_RA_CM = 60;

// Thời gian khóa sau mỗi lần đếm, chống một người bị đếm nhiều lần.
const unsigned long THOI_GIAN_CHO_DEM_LAI_MS = 2500;

// Timeout pulseIn. Giảm timeout giúp loop ít bị đứng khi cảm biến lỗi.
// 25000us tương ứng khoảng 4m, dư cho ngưỡng 150cm.
const unsigned long SONAR_TIMEOUT_US = 25000;

// Giá trị trả về khi không đo được.
const long SONAR_NO_OBJECT_CM = 999;

// Độ mượt khoảng cách: số càng cao càng phản ứng nhanh nhưng dễ rung.
// 0.30 nghĩa là 30% giá trị mới + 70% giá trị cũ.
const float HE_SO_LAM_MUOT_KHOANG_CACH = 0.30;

// ================================================================================
// 4. CẤU HÌNH DHT11
// ================================================================================

// DHT11 không nên đọc quá dày. 10 giây/lần là an toàn.
const unsigned long DHT_READ_INTERVAL_MS = 10000;

// Chờ DHT ổn định sau khi bật nguồn.
const unsigned long DHT_STARTUP_DELAY_MS = 2000;

// ================================================================================
// 5. CẤU HÌNH ĐÈN NEOPIXEL & CÒI
// ================================================================================

const int PIXEL_BRIGHTNESS = 70;

// Màu LED khi có vật thể trong vùng quét.
// Dạng RGB: 0-255.
const uint8_t LED_R = 0;
const uint8_t LED_G = 180;
const uint8_t LED_B = 120;

// Bật/tắt còi. Khi cần chạy im lặng để test, đổi thành false.
const bool BUZZER_ENABLE = true;

// Nếu còi là active buzzer thường HIGH là kêu.
// Nếu đấu module ngược logic, đổi HIGH thành LOW.
#define BUZZER_ACTIVE_LEVEL HIGH

// Khoảng beep nhanh nhất khi rất gần.
const int BEEP_INTERVAL_MIN_MS = 100;

// Khoảng beep chậm nhất khi xa.
const int BEEP_INTERVAL_MAX_MS = 1000;

// ================================================================================
// 6. CẤU HÌNH OLED
// ================================================================================

// OLED refresh không nên quá nhanh, tránh nhấp nháy và tốn vòng lặp.
const unsigned long OLED_REFRESH_INTERVAL_MS = 700;

// Tự chuyển trang màn hình: trang chính + trang trạng thái mạng/thiết bị.
const bool OLED_AUTO_PAGE = true;
const unsigned long OLED_PAGE_INTERVAL_MS = 5000;

// ================================================================================
// 7. CẤU HÌNH WIFI / MQTT / THINGSBOARD
// ================================================================================

// Chu kỳ thử kết nối lại WiFi. Không dùng while chờ vô hạn.
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 8000;

// Chu kỳ thử kết nối lại MQTT. Không chặn cảm biến/màn hình.
const unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;

// Gửi dữ liệu định kỳ để ThingsBoard biết thiết bị vẫn sống.
const unsigned long MQTT_PERIODIC_INTERVAL_MS = 600000; // 10 phút

// Van an toàn chống spam ThingsBoard khi cảm biến rung.
const unsigned long MQTT_COOLDOWN_MS = 5000;

// MQTT buffer cần lớn hơn mặc định để chứa RPC/OTA JSON.
const int MQTT_BUFFER_SIZE = 1024;

// ================================================================================
// 8. CẤU HÌNH SERIAL / LOOP
// ================================================================================

const unsigned long SERIAL_LOG_INTERVAL_MS = 1000;

// Delay rất nhỏ để nhường CPU, không dùng delay dài.
const unsigned long LOOP_IDLE_DELAY_MS = 5;

// ================================================================================
// 9. CẤU HÌNH RUNTIME CONFIG / PROVISIONING - v1.2.0-dev
// ================================================================================

// Giai đoạn chuyển tiếp:
// 1 = vẫn cho phép lấy WiFi/token từ secrets.h nếu trong NVS chưa có cấu hình.
// 0 = firmware production không include secrets.h.
#define USE_LOCAL_SECRETS 1

// Namespace lưu WiFi/token trong Preferences/NVS.
#define CFG_NAMESPACE "hp04_cfg"

// ThingsBoard mặc định.
#define DEFAULT_TB_HOST "thingsboard.cloud"
#define DEFAULT_TB_PORT 1883

// Tên WiFi AP sau này dùng cho Setup Portal.
#define SETUP_AP_PREFIX "HP04-SETUP"

// Sau này nếu WiFi lỗi quá lâu thì mở Setup Portal.
const unsigned long WIFI_FAIL_TO_SETUP_MS = 60000; // 60 giây