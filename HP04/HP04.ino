/*
  ================================================================================
  HP04 - PEOPLE COUNTER / VISITOR COUNTER
  Board   : ESP32-C3
  Sensor  : HC-SR04 + DHT11
  Display : OLED SH1106 1.3 inch I2C
  Cloud   : ThingsBoard MQTT
  OTA     : ThingsBoard RPC -> ESP32 tải firmware .bin từ GitHub raw URL

  PHIÊN BẢN:
  - v1.1.0
  - Giữ logic gốc: siêu âm đếm người, DHT11, NeoPixel, buzzer, OLED, Preferences.
  - Nâng cấp:
    1. Không còn chờ WiFi vô hạn trong setup.
    2. WiFi/MQTT tự reconnect không chặn loop.
    3. Màn hình không kẹt ở "System Starting".
    4. Bổ sung cấu hình tập trung đầu code, có chú thích tiếng Việt.
    5. Bổ sung version/device attributes để quản lý trên ThingsBoard.
    6. Bổ sung RPC: otaUpdate, resetCounter, setCounter, restart, getStatus.
    7. OTA từ GitHub raw .bin bằng lệnh ThingsBoard RPC.

  LƯU Ý OTA:
  - Arduino IDE: Tools -> Partition Scheme phải chọn loại có OTA.
  - Firmware .bin phải build đúng board ESP32-C3.
  - Không đưa mật khẩu WiFi / ThingsBoard token lên GitHub.
  ================================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <Preferences.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include "esp_system.h"

#include "config.h"
#if USE_LOCAL_SECRETS
  #include "secrets.h"
#endif


// ================================================================================
// 10. OBJECTS
// ================================================================================

Preferences preferences;

// Preferences riêng để lưu cấu hình runtime: WiFi + ThingsBoard.
// Khác với preferences đang dùng để lưu peopleCount, bootCount.
Preferences configPrefs;

// Cấu hình WiFi/ThingsBoard được dùng khi thiết bị chạy.
// Ban đầu rỗng, sau đó sẽ được nạp từ NVS hoặc secrets.h.
String cfgWifiSsid = "";
String cfgWifiPassword = "";

String cfgTbHost = DEFAULT_TB_HOST;
int cfgTbPort = DEFAULT_TB_PORT;
String cfgTbToken = "";

// Có cấu hình hợp lệ hay chưa.
bool hasRuntimeConfig = false;

// Sau này dùng cho Setup Portal.
bool setupPortalActive = false;

// Ghi nhận thời điểm bắt đầu thử kết nối WiFi.
unsigned long wifiConnectStartedAt = 0;
WiFiClient espClient;
PubSubClient client(espClient);
WebServer setupServer(SETUP_PORTAL_PORT);


Adafruit_NeoPixel pixels(NUMPIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
DHT dht(DHTPIN, DHTTYPE);

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

// ================================================================================
// 11. BIẾN TRẠNG THÁI HỆ THỐNG
// ================================================================================

int peopleCount = 0;
uint32_t bootCount = 0;

bool isPersonPresent = false;
bool trangThaiCoi = false;

float khoangCachMuot = 200.0;
int distanceStableCm = 200;

float currentTemp = 0.0;
float currentHum  = 0.0;
bool dhtValid = false;

bool needInstantSync = true;

unsigned long thoiGianDemLanCuoi = 0;
unsigned long thoiGianBeep = 0;
unsigned long thoiGianSerial = 0;
unsigned long thoiGianDocDHT = 0;
unsigned long thoiGianOLED = 0;

unsigned long lastWifiAttempt = 0;
unsigned long lastMqttAttempt = 0;
unsigned long lastMqttSend = 0;

unsigned long scheduledRestartAt = 0;

// OTA pending do RPC đặt cờ, không chạy trực tiếp trong callback MQTT.
bool otaPending = false;
bool otaRunning = false;
String otaPendingUrl = "";
String otaPendingVersion = "";

// ================================================================================
// 12. KHAI BÁO HÀM
// ================================================================================

void showBootScreen(const String& line1, const String& line2);
void maintainWiFi(unsigned long now);
void maintainMqtt(unsigned long now);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void loadRuntimeConfig();
bool isRuntimeConfigValid();
void printRuntimeConfigStatus();

String getSetupApName();
void startSetupPortal();
void maintainSetupPortal();
void handleSetupRoot();
void handleSetupSave();

long readDistanceCm();
void readDhtIfDue(unsigned long now);
void updateCounterLogic(long rawDistance, unsigned long now);
void updateLedAndBuzzer(unsigned long now);
void sendTelemetryIfDue(unsigned long now);
void publishDeviceAttributes();
void publishOtaStatus(const String& status, const String& version, const String& detail);

void updateOLED(unsigned long now);
void drawMainPage();
void drawStatusPage();
void drawCenteredText(const String& text, int y, uint8_t size);

void sendRpcResponse(const String& requestId, const String& response);
String extractRequestId(const String& topic);
String jsonEscape(String s);
String resetReasonToText();

bool performOtaUpdate(const String& url, const String& targetVersion);

void setBuzzer(bool on);


// ================================================================================
// 12A. RUNTIME CONFIG - WIFI / THINGSBOARD
// ================================================================================

bool isRuntimeConfigValid() {
  return cfgWifiSsid.length() > 0 && cfgTbToken.length() > 0;
}

void loadRuntimeConfig() {
  configPrefs.begin(CFG_NAMESPACE, false);

  // 1. Thử đọc cấu hình đã lưu trong Preferences/NVS.
  cfgWifiSsid     = configPrefs.getString("wifi_ssid", "");
  cfgWifiPassword = configPrefs.getString("wifi_pass", "");
  cfgTbHost       = configPrefs.getString("tb_host", DEFAULT_TB_HOST);
  cfgTbPort       = configPrefs.getInt("tb_port", DEFAULT_TB_PORT);
  cfgTbToken      = configPrefs.getString("tb_token", "");

#if USE_LOCAL_SECRETS
  // 2. Giai đoạn chuyển tiếp:
  // Nếu NVS chưa có WiFi/token, dùng secrets.h làm nguồn dự phòng.
  // Cách này giúp code vẫn chạy trong lúc ta chưa làm Setup Portal.
  if (!isRuntimeConfigValid()) {
    cfgWifiSsid     = WIFI_SSID;
    cfgWifiPassword = WIFI_PASSWORD;
    cfgTbHost       = TB_HOST;
    cfgTbPort       = TB_PORT;
    cfgTbToken      = TB_TOKEN;
  }
#endif

  hasRuntimeConfig = isRuntimeConfigValid();
}

void printRuntimeConfigStatus() {
  Serial.println(F("[CFG] Runtime config status"));

  Serial.print(F("[CFG] WiFi SSID: "));
  if (cfgWifiSsid.length() > 0) {
    Serial.println(cfgWifiSsid);
  } else {
    Serial.println(F("(empty)"));
  }

  Serial.print(F("[CFG] ThingsBoard host: "));
  Serial.println(cfgTbHost);

  Serial.print(F("[CFG] ThingsBoard port: "));
  Serial.println(cfgTbPort);

  Serial.print(F("[CFG] ThingsBoard token: "));
  Serial.println(cfgTbToken.length() > 0 ? F("SET") : F("EMPTY"));

  Serial.print(F("[CFG] Has runtime config: "));
  Serial.println(hasRuntimeConfig ? F("YES") : F("NO"));
}

// ================================================================================
// 12B. SETUP PORTAL - WIFI / THINGSBOARD CONFIG
// ================================================================================

String getSetupApName() {
  uint32_t chipId = (uint32_t)ESP.getEfuseMac();
  String suffix = String(chipId, HEX);
  suffix.toUpperCase();

  if (suffix.length() > 4) {
    suffix = suffix.substring(suffix.length() - 4);
  }

  return String(SETUP_AP_PREFIX) + "-" + suffix;
}

void startSetupPortal() {
  if (setupPortalActive) return;

  String apName = getSetupApName();

  Serial.println(F("[SETUP] Bat dau Setup Portal"));
  Serial.print(F("[SETUP] AP SSID: "));
  Serial.println(apName);
  Serial.println(F("[SETUP] Mo trinh duyet: http://192.168.4.1"));

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apName.c_str(), SETUP_AP_PASSWORD);

  setupServer.on("/", HTTP_GET, handleSetupRoot);
  setupServer.on("/save", HTTP_POST, handleSetupSave);
  setupServer.begin();

  setupPortalActive = true;

  if (oledReady) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("HP04 SETUP MODE"));
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    display.setCursor(0, 18);
    display.print(F("AP: "));
    display.println(apName);
    display.setCursor(0, 34);
    display.println(F("Pass: hp04setup"));
    display.setCursor(0, 50);
    display.println(F("IP: 192.168.4.1"));
    display.display();
  }
}

void maintainSetupPortal() {
  if (setupPortalActive) {
    setupServer.handleClient();
  }
}

void handleSetupRoot() {
  String html = "";

  html += "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>HP04 Setup</title>";
  html += "<style>";
  html += "body{font-family:Arial;background:#0f172a;color:#fff;margin:0;padding:24px;}";
  html += ".card{max-width:520px;margin:auto;background:#111827;border:1px solid #334155;border-radius:18px;padding:24px;}";
  html += "h1{margin-top:0;font-size:26px;}";
  html += "label{display:block;margin-top:14px;color:#cbd5e1;font-weight:bold;}";
  html += "input{width:100%;box-sizing:border-box;margin-top:6px;padding:12px;border-radius:10px;border:1px solid #475569;background:#020617;color:#fff;font-size:16px;}";
  html += "button{width:100%;margin-top:22px;padding:14px;border:0;border-radius:12px;background:#22c55e;color:#052e16;font-size:17px;font-weight:bold;}";
  html += ".note{color:#94a3b8;font-size:13px;line-height:1.5;margin-top:12px;}";
  html += "</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>HP04 Setup Portal</h1>";
  html += "<div class='note'>Nhap WiFi va ThingsBoard token cho thiet bi. Thong tin se duoc luu trong Preferences/NVS.</div>";
  html += "<form method='POST' action='/save'>";

  html += "<label>WiFi SSID</label>";
  html += "<input name='wifi_ssid' placeholder='Ten WiFi' required>";

  html += "<label>WiFi Password</label>";
  html += "<input name='wifi_pass' type='password' placeholder='Mat khau WiFi'>";

  html += "<label>ThingsBoard Host</label>";
  html += "<input name='tb_host' value='thingsboard.cloud' required>";

  html += "<label>ThingsBoard Port</label>";
  html += "<input name='tb_port' value='1883' required>";

  html += "<label>ThingsBoard Device Token</label>";
  html += "<input name='tb_token' placeholder='Access token cua thiet bi' required>";

  html += "<button type='submit'>Save & Restart HP04</button>";
  html += "</form>";
  html += "</div></body></html>";

  setupServer.send(200, "text/html", html);
}

void handleSetupSave() {
  String ssid = setupServer.arg("wifi_ssid");
  String pass = setupServer.arg("wifi_pass");
  String host = setupServer.arg("tb_host");
  String portText = setupServer.arg("tb_port");
  String token = setupServer.arg("tb_token");

  ssid.trim();
  pass.trim();
  host.trim();
  portText.trim();
  token.trim();

  int port = portText.toInt();
  if (port <= 0) port = DEFAULT_TB_PORT;

  if (ssid.length() == 0 || token.length() < 5) {
    setupServer.send(400, "text/plain", "Missing WiFi SSID or ThingsBoard token");
    return;
  }

  configPrefs.putString("wifi_ssid", ssid);
  configPrefs.putString("wifi_pass", pass);
  configPrefs.putString("tb_host", host.length() > 0 ? host : DEFAULT_TB_HOST);
  configPrefs.putInt("tb_port", port);
  configPrefs.putString("tb_token", token);

  setupServer.send(
    200,
    "text/html",
    "<html><body style='font-family:Arial;padding:24px;'>"
    "<h2>HP04 saved configuration</h2>"
    "<p>Device will restart now.</p>"
    "</body></html>"
  );

  Serial.println(F("[SETUP] Da luu WiFi/ThingsBoard config vao NVS. Restart..."));

  delay(1200);
  ESP.restart();
}


// ================================================================================
// 13. SETUP
// ================================================================================

void setup() {
  Serial.begin(115200);
  delay(200);

  randomSeed(esp_random());

  preferences.begin("hp04_data", false);
  peopleCount = preferences.getInt("peopleCount", 0);
  bootCount = preferences.getUInt("bootCount", 0) + 1;
  preferences.putUInt("bootCount", bootCount);
  loadRuntimeConfig();
  printRuntimeConfigStatus();

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);

  pixels.begin();
  pixels.setBrightness(PIXEL_BRIGHTNESS);
  pixels.clear();
  pixels.show();

  dht.begin();

  Wire.begin(I2C_SDA, I2C_SCL);

  oledReady = display.begin(0x3C, true);
  if (oledReady) {
    display.setTextWrap(false);
    showBootScreen("HP04 STARTING", String(FW_VERSION));
  } else {
    Serial.println(F("[OLED] Khong tim thay OLED SH1106 tai dia chi 0x3C"));
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  if (hasRuntimeConfig) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPassword.c_str());
  wifiConnectStartedAt = millis();
  lastWifiAttempt = wifiConnectStartedAt;
} else {
  startSetupPortal();
}

  client.setServer(cfgTbHost.c_str(), cfgTbPort);
  client.setCallback(mqttCallback);
  client.setBufferSize(MQTT_BUFFER_SIZE);
  client.setKeepAlive(30);
  client.setSocketTimeout(3);

  Serial.println();
  Serial.println(F("======================================"));
  Serial.print(F("[BOOT] App: ")); Serial.println(APP_TITLE);
  Serial.print(F("[BOOT] Firmware: ")); Serial.println(FW_VERSION);
  Serial.print(F("[BOOT] Reset reason: ")); Serial.println(resetReasonToText());
  Serial.print(F("[BOOT] Saved peopleCount: ")); Serial.println(peopleCount);
  Serial.print(F("[BOOT] Boot count: ")); Serial.println(bootCount);
  Serial.println(F("======================================"));
}

// ================================================================================
// 14. LOOP
// ================================================================================

void loop() {
  unsigned long now = millis();

  maintainSetupPortal();

  maintainWiFi(now);
  maintainMqtt(now);

  if (scheduledRestartAt > 0 && now >= scheduledRestartAt) {
    ESP.restart();
  }

  if (otaPending && !otaRunning) {
    if (WiFi.status() == WL_CONNECTED) {
      String url = otaPendingUrl;
      String version = otaPendingVersion;
      otaPending = false;
      performOtaUpdate(url, version);
    }
  }

  readDhtIfDue(now);

  long rawDistance = readDistanceCm();

  if (rawDistance != SONAR_NO_OBJECT_CM) {
    khoangCachMuot = (khoangCachMuot * (1.0 - HE_SO_LAM_MUOT_KHOANG_CACH)) +
                     (rawDistance * HE_SO_LAM_MUOT_KHOANG_CACH);
  } else {
    khoangCachMuot = (khoangCachMuot * 0.90) + (200.0 * 0.10);
  }

  distanceStableCm = round(khoangCachMuot);

  updateCounterLogic(rawDistance, now);
  updateLedAndBuzzer(now);
  sendTelemetryIfDue(now);
  updateOLED(now);

  if (now - thoiGianSerial >= SERIAL_LOG_INTERVAL_MS) {
    thoiGianSerial = now;

    String sonarStatus = (distanceStableCm <= KHOANG_CACH_XA_NHAT_CM)
                         ? String(distanceStableCm) + "cm"
                         : "Scanning";

    Serial.printf("[STATUS] WiFi:%s | MQTT:%s | T:%.1fC | H:%.1f%% | Sonar:%s | Count:%d | FW:%s\n",
                  WiFi.status() == WL_CONNECTED ? "OK" : "NO",
                  client.connected() ? "OK" : "NO",
                  currentTemp,
                  currentHum,
                  sonarStatus.c_str(),
                  peopleCount,
                  FW_VERSION);
  }

  delay(LOOP_IDLE_DELAY_MS);
}

// ================================================================================
// 15. WIFI / MQTT KHÔNG CHẶN
// ================================================================================

void maintainWiFi(unsigned long now) {
  if (!hasRuntimeConfig) return;
  if (WiFi.status() == WL_CONNECTED) return;

  if (now - lastWifiAttempt >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiAttempt = now;
    Serial.println(F("[WiFi] Mat ket noi. Thu ket noi lai..."));

    WiFi.disconnect(false, false);
    WiFi.begin(cfgWifiSsid.c_str(), cfgWifiPassword.c_str());
  }
}

void maintainMqtt(unsigned long now) {
  if (!hasRuntimeConfig) return;
  if (WiFi.status() != WL_CONNECTED) return;

  if (client.connected()) {
    client.loop();
    return;
  }

  if (now - lastMqttAttempt < MQTT_RECONNECT_INTERVAL_MS) return;

  lastMqttAttempt = now;

  String clientId = String(FW_TITLE) + "_" + String((uint32_t)ESP.getEfuseMac(), HEX) + "_" + String(random(0xffff), HEX);

  Serial.println(F("[MQTT] Thu ket noi ThingsBoard..."));

  if (client.connect(clientId.c_str(), cfgTbToken.c_str(), "")) {
    Serial.println(F("[MQTT] Connected ThingsBoard"));

    client.subscribe("v1/devices/me/rpc/request/+");

    publishDeviceAttributes();
    needInstantSync = true;
  } else {
    Serial.print(F("[MQTT] Loi ket noi, state="));
    Serial.println(client.state());
  }
}

// ================================================================================
// 16. MQTT RPC CALLBACK
// ================================================================================

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String requestId = extractRequestId(topicStr);

  String message;
  message.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print(F("[RPC] Topic: "));
  Serial.println(topicStr);
  Serial.print(F("[RPC] Payload: "));
  Serial.println(message);

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, message);

  if (err) {
    sendRpcResponse(requestId, "{\"ok\":false,\"error\":\"json_parse_failed\"}");
    return;
  }

  String method = doc["method"] | "";
  JsonVariant params = doc["params"];

  // --------------------------------------------------------------------------
  // RPC: otaUpdate
  // Payload gợi ý:
  // {
  //   "method": "otaUpdate",
  //   "params": {
  //     "url": "https://raw.githubusercontent.com/USER/REPO/main/firmware/HP04_v1.1.1.bin",
  //     "version": "v1.1.1"
  //   }
  // }
  // --------------------------------------------------------------------------
  if (method == "otaUpdate" || method == "updateFirmware") {
    if (!ENABLE_GITHUB_OTA) {
      sendRpcResponse(requestId, "{\"ok\":false,\"error\":\"ota_disabled_in_firmware\"}");
      return;
    }

    String url = "";
    String version = "";

    if (params.is<JsonObject>()) {
      url = params["url"] | "";
      version = params["version"] | "";
    } else if (params.is<const char*>()) {
      url = params.as<String>();
    }

    if (url.length() < 10) {
      sendRpcResponse(requestId, "{\"ok\":false,\"error\":\"missing_firmware_url\"}");
      return;
    }

    otaPendingUrl = url;
    otaPendingVersion = version;
    otaPending = true;

    String resp = String("{\"ok\":true,\"message\":\"ota_scheduled\",\"current_fw\":\"") +
                  FW_VERSION + "\",\"target_fw\":\"" + jsonEscape(version) + "\"}";
    sendRpcResponse(requestId, resp);

    publishOtaStatus("SCHEDULED", version, "OTA scheduled by ThingsBoard RPC");
    return;
  }

  // --------------------------------------------------------------------------
  // RPC: resetCounter
  // Payload:
  // {"method":"resetCounter","params":{}}
  // --------------------------------------------------------------------------
  if (method == "resetCounter") {
    peopleCount = 0;
    preferences.putInt("peopleCount", peopleCount);
    needInstantSync = true;

    sendRpcResponse(requestId, "{\"ok\":true,\"peopleCount\":0}");
    return;
  }

  // --------------------------------------------------------------------------
  // RPC: setCounter
  // Payload:
  // {"method":"setCounter","params":{"value":25}}
  // hoặc:
  // {"method":"setCounter","params":25}
  // --------------------------------------------------------------------------
  if (method == "setCounter") {
    long value = peopleCount;

    if (params.is<long>()) {
      value = params.as<long>();
    } else if (params.is<JsonObject>()) {
      value = params["value"] | peopleCount;
    }

    if (value < 0) value = 0;
    if (value > 1000000) value = 1000000;

    peopleCount = (int)value;
    preferences.putInt("peopleCount", peopleCount);
    needInstantSync = true;

    String resp = String("{\"ok\":true,\"peopleCount\":") + String(peopleCount) + "}";
    sendRpcResponse(requestId, resp);
    return;
  }

  // --------------------------------------------------------------------------
  // RPC: restart
  // Payload:
  // {"method":"restart","params":{}}
  // --------------------------------------------------------------------------
  if (method == "restart") {
    sendRpcResponse(requestId, "{\"ok\":true,\"message\":\"restart_scheduled\"}");
    scheduledRestartAt = millis() + 1500;
    return;
  }

  // --------------------------------------------------------------------------
  // RPC: getStatus
  // Payload:
  // {"method":"getStatus","params":{}}
  // --------------------------------------------------------------------------
  if (method == "getStatus") {
    String resp = "{";
    resp += "\"ok\":true";
    resp += ",\"fw_version\":\"" + String(FW_VERSION) + "\"";
    resp += ",\"peopleCount\":" + String(peopleCount);
    resp += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
    resp += ",\"mqtt\":" + String(client.connected() ? "true" : "false");
    resp += ",\"distanceCm\":" + String(distanceStableCm);
    resp += ",\"temperature\":" + String(currentTemp, 1);
    resp += ",\"humidity\":" + String(currentHum, 1);
    resp += ",\"uptimeSec\":" + String(millis() / 1000);
    resp += "}";

    sendRpcResponse(requestId, resp);
    return;
  }

  sendRpcResponse(requestId, "{\"ok\":false,\"error\":\"unknown_method\"}");
}

// ================================================================================
// 17. ĐỌC CẢM BIẾN
// ================================================================================

long readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, SONAR_TIMEOUT_US);

  if (duration == 0) return SONAR_NO_OBJECT_CM;

  long distance = duration * 0.034 / 2;

  if (distance <= 0 || distance > 450) return SONAR_NO_OBJECT_CM;

  return distance;
}

void readDhtIfDue(unsigned long now) {
  if (now < DHT_STARTUP_DELAY_MS) return;

  if (thoiGianDocDHT == 0 || now - thoiGianDocDHT >= DHT_READ_INTERVAL_MS) {
    thoiGianDocDHT = now;

    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      currentTemp = t;
      currentHum = h;
      dhtValid = true;
    } else {
      dhtValid = false;
      Serial.println(F("[DHT] Doc DHT11 that bai. Giu gia tri cu."));
    }
  }
}

// ================================================================================
// 18. LOGIC ĐẾM NGƯỜI - GIỮ TINH THẦN CODE GỐC
// ================================================================================

void updateCounterLogic(long rawDistance, unsigned long now) {
  if (rawDistance != SONAR_NO_OBJECT_CM && rawDistance <= KHOANG_CACH_DEM_CM) {
    if (!isPersonPresent && (now - thoiGianDemLanCuoi > THOI_GIAN_CHO_DEM_LAI_MS)) {
      isPersonPresent = true;
      thoiGianDemLanCuoi = now;

      peopleCount++;
      preferences.putInt("peopleCount", peopleCount);

      needInstantSync = true;

      Serial.println();
      Serial.println(F("===================================="));
      Serial.print(F(">>> CHOT DEM! TONG SO KHACH: "));
      Serial.print(peopleCount);
      Serial.println(F(" <<<"));
      Serial.println(F("===================================="));
      Serial.println();
    }
  } else if (rawDistance == SONAR_NO_OBJECT_CM || rawDistance >= KHOANG_CACH_NHA_RA_CM) {
    isPersonPresent = false;
  }
}

// ================================================================================
// 19. ĐÈN & CÒI - GIỮ LOGIC GẦN NHƯ GỐC, CÓ THAM SỐ TÙY CHỈNH
// ================================================================================

void updateLedAndBuzzer(unsigned long now) {
  if (distanceStableCm > KHOANG_CACH_XA_NHAT_CM) {
    pixels.clear();
    pixels.show();

    trangThaiCoi = false;
    setBuzzer(false);
    return;
  }

  int interval = map(
    constrain(distanceStableCm, KHOANG_CACH_DEM_CM, KHOANG_CACH_XA_NHAT_CM),
    KHOANG_CACH_DEM_CM,
    KHOANG_CACH_XA_NHAT_CM,
    BEEP_INTERVAL_MIN_MS,
    BEEP_INTERVAL_MAX_MS
  );

  if (BUZZER_ENABLE && now - thoiGianBeep >= (unsigned long)interval) {
    thoiGianBeep = now;
    trangThaiCoi = !trangThaiCoi;
    setBuzzer(trangThaiCoi);
  }

  pixels.clear();

  int soBong = map(
    constrain(distanceStableCm, KHOANG_CACH_DEM_CM, KHOANG_CACH_XA_NHAT_CM),
    KHOANG_CACH_XA_NHAT_CM,
    KHOANG_CACH_DEM_CM,
    1,
    NUMPIXELS
  );

  for (int i = 0; i < soBong; i++) {
    pixels.setPixelColor(i, pixels.Color(LED_R, LED_G, LED_B));
  }

  pixels.show();
}

void setBuzzer(bool on) {
  if (!BUZZER_ENABLE) {
    digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LEVEL == HIGH ? LOW : HIGH);
    return;
  }

  digitalWrite(BUZZER_PIN, on ? BUZZER_ACTIVE_LEVEL : (BUZZER_ACTIVE_LEVEL == HIGH ? LOW : HIGH));
}

// ================================================================================
// 20. GỬI THINGSBOARD
// ================================================================================

void sendTelemetryIfDue(unsigned long now) {
  if (!client.connected()) return;

  bool firstSend = (lastMqttSend == 0);
  bool duePeriodic = (now - lastMqttSend >= MQTT_PERIODIC_INTERVAL_MS);
  bool passedCooldown = firstSend || (now - lastMqttSend >= MQTT_COOLDOWN_MS);

  if ((needInstantSync || duePeriodic || firstSend) && passedCooldown) {
    String payload = "{";

    payload += "\"peopleCount\":" + String(peopleCount);
    payload += ",\"distanceCm\":" + String(distanceStableCm);
    payload += ",\"wifiRssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    payload += ",\"uptimeSec\":" + String(now / 1000);
    payload += ",\"bootCount\":" + String(bootCount);

    if (dhtValid) {
      payload += ",\"temperature\":" + String(currentTemp, 1);
      payload += ",\"humidity\":" + String(currentHum, 1);
    }

    payload += "}";

    bool ok = client.publish("v1/devices/me/telemetry", payload.c_str());

    if (ok) {
      if (needInstantSync) {
        Serial.println(F("[TB] Instant sync da gui"));
      } else {
        Serial.println(F("[TB] Periodic sync da gui"));
      }

      lastMqttSend = now;
      needInstantSync = false;
    } else {
      Serial.println(F("[TB] Gui telemetry that bai"));
    }
  }
}

void publishDeviceAttributes() {
  if (!client.connected()) return;

  String payload = "{";
  payload += "\"app_title\":\"" + String(APP_TITLE) + "\"";
  payload += ",\"fw_title\":\"" + String(FW_TITLE) + "\"";
  payload += ",\"fw_version\":\"" + String(FW_VERSION) + "\"";
  payload += ",\"fw_note\":\"" + jsonEscape(String(FW_NOTE)) + "\"";
  payload += ",\"device_model\":\"ESP32-C3_HP04\"";
  payload += ",\"bootCount\":" + String(bootCount);
  payload += ",\"resetReason\":\"" + jsonEscape(resetReasonToText()) + "\"";

  if (WiFi.status() == WL_CONNECTED) {
    payload += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    payload += ",\"mac\":\"" + WiFi.macAddress() + "\"";
  }

  payload += "}";

  client.publish("v1/devices/me/attributes", payload.c_str());
}

void publishOtaStatus(const String& status, const String& version, const String& detail) {
  if (!client.connected()) return;

  String payload = "{";
  payload += "\"otaStatus\":\"" + jsonEscape(status) + "\"";
  payload += ",\"otaTargetVersion\":\"" + jsonEscape(version) + "\"";
  payload += ",\"otaDetail\":\"" + jsonEscape(detail) + "\"";
  payload += ",\"fw_version\":\"" + String(FW_VERSION) + "\"";
  payload += "}";

  client.publish("v1/devices/me/attributes", payload.c_str());
}

// ================================================================================
// 21. OTA TỪ GITHUB RAW URL
// ================================================================================

bool performOtaUpdate(const String& url, const String& targetVersion) {
  otaRunning = true;

  Serial.println(F("[OTA] Bat dau OTA tu URL"));
  Serial.println(url);

  publishOtaStatus("STARTED", targetVersion, "Starting OTA");

  if (!ENABLE_GITHUB_OTA) {
    publishOtaStatus("FAILED", targetVersion, "OTA disabled");
    otaRunning = false;
    return false;
  }

  if (targetVersion.length() > 0 && targetVersion == String(FW_VERSION)) {
    publishOtaStatus("SKIPPED", targetVersion, "Target version equals current version");
    otaRunning = false;
    return false;
  }

  if (WiFi.status() != WL_CONNECTED) {
    publishOtaStatus("FAILED", targetVersion, "WiFi not connected");
    otaRunning = false;
    return false;
  }

  if (oledReady) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("OTA UPDATE"));
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    display.setCursor(0, 18);
    display.print(F("Current: "));
    display.println(FW_VERSION);
    display.setCursor(0, 30);
    display.print(F("Target : "));
    display.println(targetVersion);
    display.setCursor(0, 48);
    display.println(F("Downloading..."));
    display.display();
  }

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;

  bool beginOk = false;

  if (url.startsWith("https://")) {
#if OTA_ALLOW_INSECURE_TLS
    secureClient.setInsecure();
#endif
    beginOk = http.begin(secureClient, url);
  } else {
    beginOk = http.begin(plainClient, url);
  }

  if (!beginOk) {
    publishOtaStatus("FAILED", targetVersion, "HTTP begin failed");
    otaRunning = false;
    return false;
  }

  http.setConnectTimeout(15000);
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    String detail = "HTTP GET failed, code=" + String(httpCode);
    publishOtaStatus("FAILED", targetVersion, detail);
    http.end();
    otaRunning = false;
    return false;
  }

  int contentLength = http.getSize();

  if (contentLength <= 0) {
    Serial.println(F("[OTA] Khong co Content-Length, dung UPDATE_SIZE_UNKNOWN"));
  } else {
    Serial.print(F("[OTA] Firmware size: "));
    Serial.println(contentLength);
  }

  bool canBegin = Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN);

  if (!canBegin) {
    String detail = "Update.begin failed: " + String(Update.errorString());
    publishOtaStatus("FAILED", targetVersion, detail);
    http.end();
    otaRunning = false;
    return false;
  }

  publishOtaStatus("DOWNLOADING", targetVersion, "Writing firmware to OTA partition");

  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);

  if (contentLength > 0 && written != (size_t)contentLength) {
    String detail = "Written size mismatch: " + String(written) + "/" + String(contentLength);
    publishOtaStatus("FAILED", targetVersion, detail);
    Update.abort();
    http.end();
    otaRunning = false;
    return false;
  }

  if (!Update.end()) {
    String detail = "Update.end failed: " + String(Update.errorString());
    publishOtaStatus("FAILED", targetVersion, detail);
    http.end();
    otaRunning = false;
    return false;
  }

  if (!Update.isFinished()) {
    publishOtaStatus("FAILED", targetVersion, "Update not finished");
    http.end();
    otaRunning = false;
    return false;
  }

  http.end();

  preferences.putString("lastOtaTarget", targetVersion);

  publishOtaStatus("SUCCESS", targetVersion, "OTA success, restarting");

  if (oledReady) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("OTA SUCCESS"));
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    display.setCursor(0, 24);
    display.print(F("New FW: "));
    display.println(targetVersion);
    display.setCursor(0, 48);
    display.println(F("Restarting..."));
    display.display();
  }

  delay(1200);
  ESP.restart();

  return true;
}

// ================================================================================
// 22. OLED UI
// ================================================================================

void updateOLED(unsigned long now) {
  if (!oledReady) return;

  if (now - thoiGianOLED < OLED_REFRESH_INTERVAL_MS) return;
  thoiGianOLED = now;

  if (otaRunning) return;

  uint8_t page = 0;

  if (OLED_AUTO_PAGE) {
    page = (now / OLED_PAGE_INTERVAL_MS) % 2;
  }

  if (page == 0) {
    drawMainPage();
  } else {
    drawStatusPage();
  }

  display.display();
}

void drawMainPage() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("HP04 "));
  display.print(FW_VERSION);

  display.setCursor(80, 0);
  display.print(WiFi.status() == WL_CONNECTED ? F("W+") : F("W-"));
  display.print(F(" "));
  display.print(client.connected() ? F("M+") : F("M-"));

  display.drawLine(0, 10, 128, 10, SH110X_WHITE);

  display.fillRoundRect(0, 14, 128, 34, 5, SH110X_WHITE);

  display.setTextColor(SH110X_BLACK);
  drawCenteredText("TOTAL GUESTS", 17, 1);
  drawCenteredText(String(peopleCount), 27, 2);

  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 53);
  if (dhtValid) {
    display.print(F("T:"));
    display.print(currentTemp, 1);
    display.print(F("C "));
    display.print(F("H:"));
    display.print(currentHum, 0);
    display.print(F("%"));
  } else {
    display.print(F("T/H: --"));
  }

  display.setCursor(82, 53);
  if (distanceStableCm <= KHOANG_CACH_XA_NHAT_CM) {
    display.print(F("D:"));
    display.print(distanceStableCm);
    display.print(F("cm"));
  } else {
    display.print(F("SCAN"));
  }
}

void drawStatusPage() {
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("HP04 DEVICE STATUS"));
  display.drawLine(0, 10, 128, 10, SH110X_WHITE);

  display.setCursor(0, 14);
  display.print(F("WiFi : "));
  if (WiFi.status() == WL_CONNECTED) {
    display.print(F("OK "));
    display.print(WiFi.RSSI());
    display.println(F("dBm"));
  } else {
    display.println(F("NO"));
  }

  display.setCursor(0, 24);
  display.print(F("MQTT : "));
  display.println(client.connected() ? F("OK") : F("NO"));

  display.setCursor(0, 34);
  display.print(F("FW   : "));
  display.println(FW_VERSION);

  display.setCursor(0, 44);
  display.print(F("UP   : "));
  display.print(millis() / 60000);
  display.print(F("m  B:"));
  display.println(bootCount);

  display.setCursor(0, 54);
  if (WiFi.status() == WL_CONNECTED) {
    display.print(WiFi.localIP());
  } else {
    display.print(F("Network reconnecting..."));
  }
}

void drawCenteredText(const String& text, int y, uint8_t size) {
  display.setTextSize(size);

  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH - w) / 2;
  if (x < 0) x = 0;

  display.setCursor(x, y);
  display.print(text);
}

void showBootScreen(const String& line1, const String& line2) {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.drawRoundRect(4, 12, 120, 40, 6, SH110X_WHITE);

  drawCenteredText(line1, 22, 1);
  drawCenteredText(line2, 35, 1);

  display.display();
}

// ================================================================================
// 23. TIỆN ÍCH RPC / JSON / RESET REASON
// ================================================================================

void sendRpcResponse(const String& requestId, const String& response) {
  if (!client.connected()) return;

  String responseTopic = "v1/devices/me/rpc/response/" + requestId;
  client.publish(responseTopic.c_str(), response.c_str());
}

String extractRequestId(const String& topic) {
  int slash = topic.lastIndexOf('/');
  if (slash < 0) return "0";
  return topic.substring(slash + 1);
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", " ");
  s.replace("\r", " ");
  return s;
}

String resetReasonToText() {
  esp_reset_reason_t reason = esp_reset_reason();

  switch (reason) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXTERNAL";
    case ESP_RST_SW:       return "SOFTWARE";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "UNKNOWN";
  }
}