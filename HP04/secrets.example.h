#pragma once

/*
  ================================================================================
  HP04 - SECRETS MẪU
  File này ĐƯỢC đưa lên GitHub.

  Cách dùng:
  1. Copy file này.
  2. Đổi tên bản copy thành secrets.h.
  3. Điền WiFi/password/token thật vào secrets.h.
  4. KHÔNG commit secrets.h lên GitHub.
  ================================================================================
*/

// ================================================================================
// WIFI MẪU
// ================================================================================

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ================================================================================
// THINGSBOARD MẪU
// ================================================================================

const char* TB_HOST       = "thingsboard.cloud";
const int   TB_PORT       = 1883;
const char* TB_TOKEN      = "YOUR_THINGSBOARD_DEVICE_TOKEN";