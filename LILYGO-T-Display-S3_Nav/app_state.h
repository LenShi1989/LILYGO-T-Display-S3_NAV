#pragma once
// ============================================================================
//  app_state.h — 跨模組共用的即時狀態
// ============================================================================
#include <Arduino.h>

struct WifiSnapshot {
  bool     staConnected = false;
  bool     apActive     = false;
  String   ssid;
  String   ip;
  String   apSsid;
  String   apPass;
  String   apIp;
  int32_t  rssi         = 0;
  int32_t  channel      = 0;
  String   mac;
  String   host;
  uint32_t connectedAt  = 0;   // millis()，0 = 尚未連線
};

struct BatterySnapshot {
  bool     present   = false;
  float    voltage   = 0.0f;
  uint8_t  percent   = 0;
  uint32_t updatedAt = 0;
};

struct AppState {
  WifiSnapshot    wifi;
  BatterySnapshot battery;
  uint8_t         brightness = 80;
};

extern AppState g_app;

// ---- 文字工具 --------------------------------------------------------------
String fmtBytes(uint32_t bytes);              // "1.2 MB"
String fmtDuration(uint32_t seconds);         // "1 時 05 分" / "12 分"
String fmtClock(uint32_t seconds);            // "01:23:45"
String fmtUptime(uint32_t seconds);           // "1d 02:03"
const char* resetReasonText();                // "POWERON"
const char* wifiStateText();                  // "已連線" / "AP 模式" / "未連線"
