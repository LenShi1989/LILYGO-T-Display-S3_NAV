#pragma once
// ============================================================================
//  wifi_manager.h — WiFi 連線 / AP 模式 / 認證儲存
// ============================================================================
#include <Arduino.h>
#include "app_state.h"

namespace Net {

void begin();
void loop();
void snapshot();

bool   staConnected();
bool   apActive();
String ip();
String hostname();
String macString();

// AP 熱點 SSID：ESP32-S3_NAV-<MAC 後 4 碼>
String apSsid();

// 連線到指定 AP（會阻塞最多 WIFI_CONNECT_MS），成功才寫入 NVS
bool connect(const String &ssid, const String &pass, String &err);

// 清除已儲存的帳密（之後請重新啟動）
void forget();

// 目前儲存的 SSID（可能為空）
String savedSsid();

}  // namespace Net
