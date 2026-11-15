// =============================================================================
//  LILYGO T-Display-S3 NAV — ESP32-S3 導航機
//
//  ── 開發環境 ────────────────────────────────────────────────────────────────
//  Arduino IDE       : 1.8.19（亦相容 2.x）
//  ESP32 核心        : 2.0.17（建議）或 3.3.12（已測試）
//  開發板            : LILYGO T-Display-S3
//                      FQBN: esp32:esp32:lilygo_t_display_s3
//                      CDC On Boot: Enabled / USB Mode: Hardware CDC
//  分割表            : 專案內附 partitions.csv（3MB APP x2 + 9.375MB SPIFFS）
//                      ※ 請確認 Tools > Partition Scheme 使用本專案分割表
//
//  ── 必要函式庫（Tools > Manage Libraries 安裝）─────────────────────────────
//  TFT_eSPI          2.5.43   （sketch 內附 tft_setup.h，勿修改函式庫）
//  ArduinoJson       7.4.2    （6.x 亦可，json_compat.h 會自動相容）
//
//  ── 內建函式庫（核心附帶，無須安裝）────────────────────────────────────────
//  WiFi / WebServer / Update / SPIFFS / Preferences / DNSServer / ESPmDNS
//  HTTPClient / WiFiClientSecure
//
//  ── 上傳步驟 ────────────────────────────────────────────────────────────────
//  1. 以 Arduino IDE 上傳本 sketch（第一次需 USB 線）
//  2. 安裝 ESP32 Sketch Data Upload 工具，上傳 data/ 資料夾到 SPIFFS
//  3. 之後可從網頁「OTA 更新」上傳 firmware.bin / spiffs.bin
//
//  ── 操作 ────────────────────────────────────────────────────────────────────
//  GPIO0  （板載 BOOT）: 選擇 / 換頁 / 調整
//  GPIO14 （外接，GND）: 確認；長按 0.8 秒 = 返回
//  網頁                   : http://nav.local （或裝置 IP）
// =============================================================================

#include "app_state.h"
#include "buttons.h"
#include "config.h"
#include "display_ui.h"
#include "nav_engine.h"
#include "web_server.h"
#include "wifi_manager.h"

#include <WiFi.h>

// -----------------------------------------------------------------------------
// 開機
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(150);

  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F("  LILYGO T-Display-S3 NAV"));
  Serial.printf("  版本 %s (%s)\n", FW_VERSION, FW_BUILD);
  Serial.printf("  核心 %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR,
                ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  Serial.println(F("=============================================="));

  Ui::begin();
  Ui::splash("系統啟動中", "初始化硬體…", nullptr, 5);

  Buttons::begin();
  Nav::begin();

  Ui::splash("系統啟動中", "連線 WiFi…", nullptr, 25);
  Net::begin();
  WiFi.setSleep(false);

  Ui::splash("系統啟動中", "啟動網頁伺服器…", nullptr, 70);
  Web::begin();

  String line1;
  if (g_app.wifi.staConnected) {
    line1 = g_app.wifi.ip;
  } else {
    line1 = String("AP: ") + Net::apSsid();
  }
  Ui::splash("系統已就緒", line1.c_str(), "http://" MDNS_HOST ".local", 100);
  delay(900);

  Ui::bootReady();

  if (g_app.wifi.staConnected) {
    Ui::toast(g_app.wifi.ip.c_str(), C_GREEN);
  } else {
    Ui::toast("AP 模式：nav.local", C_ORANGE);
  }

  Serial.printf("[sys] 可用記憶體 %u bytes，PSRAM %u bytes\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getPsramSize());
}

// -----------------------------------------------------------------------------
// 主迴圈
// -----------------------------------------------------------------------------
void loop() {
  switch (Buttons::poll()) {
    case Buttons::Event::Select:
      Ui::onSelect();
      break;
    case Buttons::Event::Confirm:
      Ui::onConfirm();
      break;
    case Buttons::Event::ConfirmLong:
      Ui::onBack();
      break;
    default:
      break;
  }

  Net::loop();
  Web::loop();
  Nav::loop();
  Ui::loop();

  delay(2);
}
