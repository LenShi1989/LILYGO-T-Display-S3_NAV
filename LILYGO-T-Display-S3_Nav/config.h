#pragma once
// ============================================================================
//  config.h — 全專案共用設定
// ============================================================================
#include <Arduino.h>

// 舊版核心可能沒有版本巨集
#ifndef ESP_ARDUINO_VERSION_MAJOR
  #define ESP_ARDUINO_VERSION_MAJOR 2
  #define ESP_ARDUINO_VERSION_MINOR 0
  #define ESP_ARDUINO_VERSION_PATCH 0
#endif

// 顏色工具（RGB888 -> RGB565）
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// ---------------------------------------------------------------------------
// 版本 / 建置資訊
// ---------------------------------------------------------------------------
#define FW_NAME       "LILYGO T-Display-S3 NAV"
#define FW_SHORT      "NAV"
#define FW_VERSION    "1.0.0"
#define FW_BUILD      __DATE__ " " __TIME__
#define FW_BOARD      "LILYGO T-Display-S3"

// ---------------------------------------------------------------------------
// 硬體腳位（LILYGO T-Display-S3）
// ---------------------------------------------------------------------------
#define PIN_BTN_SELECT   0    // GPIO0  = 板載 BOOT 鍵，選單「選擇」
#define PIN_BTN_CONFIRM  14   // GPIO14 = 外接按鍵（另一端接 GND），「確認」
#define PIN_POWER_ON     15   // 必須拉高，否則 USB 斷電時面板不亮
#define PIN_BAT_VOLT     4    // 電池電壓 ADC（1/2 分壓）
#define PIN_LCD_BL       38   // 背光

// ---------------------------------------------------------------------------
// 螢幕
// ---------------------------------------------------------------------------
#define SCR_W            170
#define SCR_H            320
#define STATUS_H         24
#define FOOTER_Y         296
#define C_BG             RGB565(11, 18, 32)
#define C_PANEL          RGB565(19, 28, 46)
#define C_PANEL2         RGB565(27, 39, 61)
#define C_BORDER         RGB565(45, 60, 86)
#define C_TEXT           RGB565(229, 231, 235)
#define C_MUTED          RGB565(148, 163, 184)
#define C_ACCENT         RGB565(34, 211, 238)
#define C_ACCENT_DIM     RGB565(14, 116, 144)
#define C_GREEN          RGB565(52, 211, 153)
#define C_YELLOW         RGB565(250, 204, 21)
#define C_RED            RGB565(248, 113, 113)
#define C_ORANGE         RGB565(251, 146, 60)
#define C_WHITE          0xFFFF
#define C_BLACK          0x0000

// ---------------------------------------------------------------------------
// WiFi / AP
// ---------------------------------------------------------------------------
#define AP_SSID              "T-Display-S3-NAV"
#define AP_PASS              "12345678"       // 至少 8 碼
#define MDNS_HOST            "nav"            // http://nav.local
#define WIFI_CONNECT_MS      15000            // 單次連線等待
#define WIFI_RETRY_MS        20000            // STA 斷線重試間隔

// ---------------------------------------------------------------------------
// 導航
// ---------------------------------------------------------------------------
#define OSRM_BASE            "https://router.project-osrm.org"
#define OSRM_PROFILE         "driving"
#define NAV_MAX_STEPS        64
#define NAV_ARRIVE_RADIUS_M  25.0f            // 距離路徑點多近視為通過
#define NAV_REFRESH_MS       1000

// ---------------------------------------------------------------------------
// 更新 / 整理
// ---------------------------------------------------------------------------
#define UI_STATUS_MS         1000
#define UI_PAGE_MS           1500
#define BATTERY_READ_MS      5000
#define BATTERY_SAMPLES      8
