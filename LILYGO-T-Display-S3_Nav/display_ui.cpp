#include "display_ui.h"

#include <math.h>

#include <Preferences.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>

#include "app_state.h"
#include "config.h"
#include "nav_engine.h"
#include "ui_text.h"
#include "wifi_manager.h"

namespace {

// ---------------------------------------------------------------------------
// 版面常數
// ---------------------------------------------------------------------------
constexpr int   MENU_Y      = 44;    // 選單第一列頂端
constexpr int   MENU_ROW_H  = 34;    // 選單列高
constexpr int   MENU_VIS    = 3;     // 一次可見列數
constexpr int   INFO_Y      = 46;    // 資訊頁第一列
constexpr int   INFO_ROW_H  = 20;
constexpr int   INFO_ROWS   = 5;     // 每欄列數
constexpr int   INFO_COLS   = 2;     // 橫向雙欄
constexpr int   DIAL_CX     = 58;
constexpr int   DIAL_CY     = 104;
constexpr int   DIAL_R      = 28;
constexpr float kDeg        = 0.01745329252f;  // PI / 180

TFT_eSPI    s_tft;
Preferences s_prefs;

bool     s_ready        = false;
bool     s_splashActive = false;
Ui::View s_view         = Ui::View::Menu;

// ---------------------------------------------------------------------------
// 選單結構
// ---------------------------------------------------------------------------
enum Act : uint8_t {
  ACT_SUB,       // 進入子選單
  ACT_NAV,
  ACT_ABOUT,
  ACT_WIFI,
  ACT_SYSINFO,
  ACT_DISP,
  ACT_BACK,      // 回上一層
};

struct MenuItem {
  const char *label;
  Act         act;
};

const MenuItem MAIN_ITEMS[] = {
    {"系統狀態", ACT_SUB},
    {"MAP導航", ACT_NAV},
    {"關於本機", ACT_ABOUT},
};

const MenuItem SYS_ITEMS[] = {
    {"WiFi 連線資訊", ACT_WIFI},
    {"系統狀態", ACT_SYSINFO},
    {"顯示設定", ACT_DISP},
    {"返回主選單", ACT_BACK},
};

constexpr uint8_t MAIN_COUNT = sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0]);
constexpr uint8_t SYS_COUNT  = sizeof(SYS_ITEMS) / sizeof(SYS_ITEMS[0]);

uint8_t s_level     = 0;          // 0 = 主選單, 1 = 系統狀態
uint8_t s_sel[2]    = {0, 0};
uint8_t s_page      = 0;
uint8_t s_pageCount = 1;

// ---------------------------------------------------------------------------
// 覆蓋層（提示 / 彈窗 / OTA）
// ---------------------------------------------------------------------------
enum Overlay : uint8_t { OV_NONE, OV_MODAL, OV_TOAST, OV_OTA };

Overlay  s_ov         = OV_NONE;
char     s_ovTitle[40] = {0};
char     s_ovL1[72]    = {0};
char     s_ovL2[56]    = {0};
uint16_t s_toastColor  = 0xFFFF;
uint32_t s_toastEnd    = 0;

bool     s_otaResult = false;
bool     s_otaOk     = false;
uint8_t  s_otaPct    = 0;
uint32_t s_otaDone   = 0;
uint32_t s_otaTotal  = 0;

// 開機畫面
char     s_splashTitle[40] = {0};
char     s_splashL1[72]    = {0};
char     s_splashL2[56]    = {0};
int      s_splashProg      = -1;

// 導航畫面
uint32_t s_navRev   = 0xFFFFFFFFu;
bool     s_navActive = false;
uint8_t  s_navSteps  = 0;

// 狀態列 / 電池
String   s_statusSig;
uint32_t s_lastBat = 0;
uint32_t s_lastStatus = 0;

// ---------------------------------------------------------------------------
// 共用小工具
// ---------------------------------------------------------------------------
void drawStatus();
void drawHeader(const char *title, const char *right);
void drawFooter(const char *hint);
void drawMenu();
void drawOverlay();
void drawNavFull();

String fmtDist(float meters) {
  char b[24];
  if (meters < 1000.0f) {
    snprintf(b, sizeof(b), "%.0f m", meters);
  } else if (meters < 10000.0f) {
    snprintf(b, sizeof(b), "%.2f km", meters / 1000.0f);
  } else {
    snprintf(b, sizeof(b), "%.1f km", meters / 1000.0f);
  }
  return String(b);
}

String fmtDistShort(float meters) {
  char b[16];
  if (meters < 1000.0f) {
    snprintf(b, sizeof(b), "%.0fm", meters);
  } else if (meters < 10000.0f) {
    snprintf(b, sizeof(b), "%.1fkm", meters / 1000.0f);
  } else {
    snprintf(b, sizeof(b), "%.0fkm", meters / 1000.0f);
  }
  return String(b);
}

String fmtDurShort(uint32_t seconds) {
  char b[20];
  uint32_t h = seconds / 3600;
  uint32_t m = (seconds % 3600) / 60;
  if (h > 0) {
    snprintf(b, sizeof(b), "%u時%u分", (unsigned)h, (unsigned)m);
  } else if (m > 0) {
    snprintf(b, sizeof(b), "%u分", (unsigned)m);
  } else {
    snprintf(b, sizeof(b), "%u秒", (unsigned)seconds);
  }
  return String(b);
}

const char *bearingName(float deg) {
  static const char *names[] = {"北", "東北", "東", "東南",
                                "南", "西南", "西", "西北"};
  int idx = (int)((deg + 22.5f) / 45.0f);
  if (idx < 0) idx += 8;
  return names[idx % 8];
}

const char *rssiText(int32_t rssi) {
  if (rssi > -60) return "極佳";
  if (rssi > -70) return "良好";
  if (rssi > -80) return "普通";
  return "微弱";
}

uint8_t batteryPercent(float v) {
  static const float pts[][2] = {
      {4.15f, 100}, {4.05f, 90}, {3.95f, 80}, {3.85f, 65}, {3.75f, 45},
      {3.68f, 30},  {3.60f, 15}, {3.50f, 7},  {3.40f, 3},  {3.30f, 0},
  };
  const int n = sizeof(pts) / sizeof(pts[0]);
  if (v >= pts[0][0]) return 100;
  if (v <= pts[n - 1][0]) return 0;
  for (int i = 0; i < n - 1; i++) {
    if (v <= pts[i][0] && v >= pts[i + 1][0]) {
      float span = pts[i][0] - pts[i + 1][0];
      float t    = (v - pts[i + 1][0]) / span;
      return (uint8_t)(pts[i + 1][1] + t * (pts[i][1] - pts[i + 1][1]) + 0.5f);
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
// 背光
// ---------------------------------------------------------------------------
void applyBrightness() {
  uint8_t pct = g_app.brightness;
  if (pct < 5) pct = 5;
  if (pct > 100) pct = 100;

  static bool ready = false;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!ready) {
    ledcAttach(PIN_LCD_BL, 5000, 8);
    ready = true;
  }
  ledcWrite(PIN_LCD_BL, (uint32_t)pct * 255 / 100);
#else
  if (!ready) {
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_LCD_BL, 0);
    ready = true;
  }
  ledcWrite(0, (uint32_t)pct * 255 / 100);
#endif
}

// ---------------------------------------------------------------------------
// 電池
// ---------------------------------------------------------------------------
void pollBattery(bool force = false) {
  uint32_t now = millis();
  if (!force && now - s_lastBat < BATTERY_READ_MS) return;
  s_lastBat = now;

  uint32_t mv = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    mv += analogReadMilliVolts(PIN_BAT_VOLT);
    delay(2);
  }
  mv /= BATTERY_SAMPLES;

  float v = (float)mv * 2.0f / 1000.0f;   // 分壓 1/2

  BatterySnapshot &b = g_app.battery;
  b.voltage   = v;
  b.present   = (v > 2.8f && v < 4.32f);
  b.percent   = b.present ? batteryPercent(v) : 0;
  b.updatedAt = now;
}

// ---------------------------------------------------------------------------
// 狀態列圖示
// ---------------------------------------------------------------------------
void drawWifiIcon(int x, int base, bool sta, int32_t rssi, bool ap) {
  int lvl = 0;
  if (sta) {
    lvl = (rssi > -60) ? 4 : (rssi > -68) ? 3 : (rssi > -76) ? 2 : 1;
  } else if (ap) {
    lvl = 2;
  }

  for (int i = 0; i < 4; i++) {
    int h  = 4 + i * 3;
    int bx = x + i * 4;
    int by = base - h;
    bool on = (sta || ap) && i < lvl;
    uint16_t c = on ? (ap ? C_ORANGE : C_ACCENT) : C_BORDER;
    if (on) {
      s_tft.fillRect(bx, by, 3, h, c);
    } else {
      s_tft.drawRect(bx, by, 3, h, c);
    }
  }

  if (!sta && !ap) {
    s_tft.drawLine(x, base, x + 15, base - 14, C_RED);
  }
}

void drawBatteryIcon(int x, int y, bool present, uint8_t pct) {
  const int w = 20, h = 10;
  s_tft.fillRect(x, y, w + 3, h, C_PANEL);
  s_tft.drawRect(x, y, w, h, C_MUTED);
  s_tft.fillRect(x + w, y + 3, 3, 4, C_MUTED);

  if (!present) {
    s_tft.drawLine(x + 3, y + h - 3, x + w - 3, y + 3, C_RED);
    return;
  }

  int inner = (w - 4) * pct / 100;
  uint16_t c = (pct > 50) ? C_GREEN : (pct > 20) ? C_YELLOW : C_RED;
  if (inner > 0) s_tft.fillRect(x + 2, y + 2, inner, h - 4, c);
}

String statusSignature() {
  const WifiSnapshot &w = g_app.wifi;
  const BatterySnapshot &b = g_app.battery;
  char buf[96];
  snprintf(buf, sizeof(buf), "%d|%d|%d|%u|%ld|%d|%.20s", w.staConnected ? 1 : 0,
           w.apActive ? 1 : 0, b.present ? 1 : 0, (unsigned)b.percent,
           (long)w.rssi, (int)w.channel, w.ssid.c_str());
  return String(buf);
}

void drawStatus() {
  const WifiSnapshot &w = g_app.wifi;
  const BatterySnapshot &b = g_app.battery;

  s_tft.fillRect(0, 0, SCR_W, STATUS_H, C_PANEL);
  s_tft.drawFastHLine(0, STATUS_H - 1, SCR_W, C_BORDER);

  String left;
  uint16_t lc;
  if (w.staConnected && w.ssid.length() > 0) {
    left = w.ssid;
    lc   = C_TEXT;
  } else if (w.apActive) {
    left = String("AP ") + w.apSsid;
    lc   = C_ORANGE;
  } else {
    left = "未連線";
    lc   = C_MUTED;
  }
  Txt::draw(6, 1, left.c_str(), lc, 180);

  drawWifiIcon(196, 15, w.staConnected, w.rssi, w.apActive);

  char pct[8];
  if (b.present) {
    snprintf(pct, sizeof(pct), "%u%%", (unsigned)b.percent);
  } else {
    snprintf(pct, sizeof(pct), "USB");
  }
  Txt::drawRight(284, 1, pct, b.present ? C_TEXT : C_MUTED, 40);

  drawBatteryIcon(290, 4, b.present, b.percent);

  s_statusSig = statusSignature();
}

void refreshStatus() {
  if (s_splashActive || s_ov == OV_OTA) return;
  uint32_t now = millis();
  if (now - s_lastStatus < UI_STATUS_MS) return;
  s_lastStatus = now;
  if (statusSignature() != s_statusSig) drawStatus();
}

// ---------------------------------------------------------------------------
// 標題 / 頁尾
// ---------------------------------------------------------------------------
void drawHeader(const char *title, const char *right) {
  s_tft.fillRect(0, HEADER_Y, SCR_W, HEADER_H, C_BG);
  Txt::draw(10, HEADER_Y + 3, title, C_WHITE);
  if (right && *right) Txt::drawRight(310, HEADER_Y + 3, right, C_MUTED, 120);
  s_tft.drawFastHLine(0, HEADER_Y + HEADER_H - 1, SCR_W, C_BORDER);
}

void drawFooter(const char *hint) {
  s_tft.fillRect(0, FOOTER_Y, SCR_W, SCR_H - FOOTER_Y, C_PANEL);
  s_tft.drawFastHLine(0, FOOTER_Y, SCR_W, C_BORDER);
  if (hint && *hint) Txt::drawCenter(160, FOOTER_Y + 2, hint, C_MUTED, 300);
}

// ---------------------------------------------------------------------------
// 選單
// ---------------------------------------------------------------------------
void drawMenu() {
  const MenuItem *items = (s_level == 0) ? MAIN_ITEMS : SYS_ITEMS;
  const uint8_t   count = (s_level == 0) ? MAIN_COUNT : SYS_COUNT;
  const uint8_t   sel   = s_sel[s_level];

  s_tft.fillRect(0, CONTENT_Y, SCR_W, FOOTER_Y - CONTENT_Y, C_BG);
  drawHeader(s_level == 0 ? "主選單" : "系統狀態", nullptr);

  int first = 0;
  if (count > MENU_VIS) {
    first = (int)sel - MENU_VIS / 2;
    if (first < 0) first = 0;
    if (first > (int)count - MENU_VIS) first = (int)count - MENU_VIS;
  }

  for (int row = 0; row < MENU_VIS && first + row < count; row++) {
    int idx = first + row;
    int y   = MENU_Y + row * MENU_ROW_H;
    bool on = (idx == sel);

    s_tft.fillRect(8, y + 2, 298, MENU_ROW_H - 6, on ? C_PANEL2 : C_PANEL);
    if (on) {
      s_tft.drawRect(8, y + 2, 298, MENU_ROW_H - 6, C_ACCENT);
      s_tft.fillRect(8, y + 2, 4, MENU_ROW_H - 6, C_ACCENT);
    } else {
      s_tft.drawFastHLine(12, y + MENU_ROW_H - 5, 290, C_BORDER);
    }

    Txt::draw(22, y + 8, items[idx].label, on ? C_WHITE : C_TEXT, 240);
    Txt::drawRight(298, y + 8, ">", on ? C_ACCENT : C_MUTED);
  }

  // 捲軸
  if (count > MENU_VIS) {
    int trackY = MENU_Y + 4;
    int trackH = MENU_VIS * MENU_ROW_H - 12;
    int thumbH = trackH * MENU_VIS / count;
    if (thumbH < 16) thumbH = 16;
    int thumbY = trackY + (trackH - thumbH) * first / (count - MENU_VIS);
    s_tft.fillRect(312, trackY, 3, trackH, C_BORDER);
    s_tft.fillRect(312, thumbY, 3, thumbH, C_ACCENT);
  }

  drawFooter("0:選擇   14:確認");
}

// ---------------------------------------------------------------------------
// 資訊頁（標籤 / 數值）
// ---------------------------------------------------------------------------
struct Row {
  const char *k;
  String      v;
};

void drawInfoPage(const char *title, const Row *rows, int n) {
  const int perPage = INFO_ROWS * INFO_COLS;
  s_pageCount = (uint8_t)((n + perPage - 1) / perPage);
  if (s_pageCount == 0) s_pageCount = 1;
  if (s_page >= s_pageCount) s_page = s_pageCount - 1;

  char pg[12];
  snprintf(pg, sizeof(pg), "%u/%u", (unsigned)s_page + 1, (unsigned)s_pageCount);

  s_tft.fillRect(0, CONTENT_Y, SCR_W, FOOTER_Y - CONTENT_Y, C_BG);
  drawHeader(title, s_pageCount > 1 ? pg : nullptr);

  const int start = (int)s_page * perPage;
  for (int i = 0; i < perPage && start + i < n; i++) {
    const Row &r = rows[start + i];
    const int col = i / INFO_ROWS;
    const int row = i % INFO_ROWS;
    const int cx  = 8 + col * 156;
    const int y   = INFO_Y + row * INFO_ROW_H;

    Txt::draw(cx, y + 1, r.k, C_MUTED, 62);

    int vx = cx + Txt::width(r.k) + 6;
    if (vx > cx + 70) vx = cx + 70;
    Txt::drawRight(cx + 146, y + 1, r.v.c_str(), C_TEXT, cx + 146 - vx);

    if (start + i + 1 < n && row + 1 < INFO_ROWS) {
      s_tft.drawFastHLine(cx, y + INFO_ROW_H - 2, 146, C_BORDER);
    }
  }

  drawFooter(s_pageCount > 1 ? "0:換頁   14:返回" : "14:返回");
}

// ---------------------------------------------------------------------------
// 各資訊頁資料
// ---------------------------------------------------------------------------
struct WifiField {
  const char *k;
  String      v;
  bool        wide;   // true = 標籤一行、數值另起一行（長 SSID 不截斷）
};

void drawWifiPage() {
  const WifiSnapshot &w = g_app.wifi;

  String ssid = w.staConnected ? w.ssid : Net::savedSsid();
  if (ssid.length() == 0) ssid = "—";

  String ip = w.staConnected ? w.ip : (w.apActive ? w.apIp : String("未取得"));

  String sig;
  if (w.staConnected) {
    char b[48];
    snprintf(b, sizeof(b), "%d dBm %s · CH %d", (int)w.rssi,
             rssiText(w.rssi), (int)w.channel);
    sig = b;
  } else {
    sig = "—";
  }

  String mac = w.mac.length() ? w.mac : Net::macString();
  mac.replace(":", "");

  // 分成兩頁：第 1 頁 STA 連線、第 2 頁熱點 / MAC。
  // SSID、熱點 SSID 使用 wide 欄位獨占一行，32 字元也能完整顯示。
  WifiField p1[4];
  WifiField p2[4];
  uint8_t n1 = 0, n2 = 0;

  p1[n1++] = {"狀態", wifiStateText(), false};
  p1[n1++] = {"SSID", ssid, true};
  p1[n1++] = {"IP", ip, false};
  p1[n1++] = {"mDNS", w.host, false};

  p2[n2++] = {"訊號", sig, false};
  p2[n2++] = {"MAC", mac, false};
  p2[n2++] = {"熱點", Net::apSsid(), true};
  p2[n2++] = {"熱點密碼", AP_PASS, false};

  s_pageCount = 2;
  if (s_page > 1) s_page = 1;

  const WifiField *fields = (s_page == 0) ? p1 : p2;
  const uint8_t count = (s_page == 0) ? n1 : n2;

  char pg[8];
  snprintf(pg, sizeof(pg), "%u/2", (unsigned)s_page + 1);

  s_tft.fillRect(0, CONTENT_Y, SCR_W, FOOTER_Y - CONTENT_Y, C_BG);
  drawHeader("WiFi 連線資訊", pg);

  int y = INFO_Y;
  for (uint8_t i = 0; i < count; i++) {
    const WifiField &f = fields[i];
    if (f.wide) {
      Txt::draw(8, y, f.k, C_MUTED);
      Txt::draw(16, y + 18, f.v.c_str(), C_TEXT, SCR_W - 28);
      y += 38;
    } else {
      Txt::draw(8, y + 1, f.k, C_MUTED, 80);
      int vx = 8 + Txt::width(f.k) + 6;
      if (vx > 8 + 80) vx = 8 + 80;
      Txt::drawRight(SCR_W - 8, y + 1, f.v.c_str(), C_TEXT, SCR_W - 16 - vx);
      y += 20;
    }
    if (i + 1 < count) s_tft.drawFastHLine(8, y - 2, SCR_W - 16, C_BORDER);
  }

  drawFooter("0:換頁   14:返回");
}

int buildSysRows(Row *out) {
  char b[48];
  int n = 0;

  out[n++] = {"韌體", String(FW_SHORT) + " " + FW_VERSION};
  out[n++] = {"建置", __DATE__};
  out[n++] = {"板子", "T-Display-S3"};
  out[n++] = {"晶片", ESP.getChipModel()};

  snprintf(b, sizeof(b), "%u MHz", (unsigned)ESP.getCpuFreqMHz());
  out[n++] = {"CPU", b};

  snprintf(b, sizeof(b), "%u MB", (unsigned)(ESP.getFlashChipSize() / 1048576UL));
  out[n++] = {"Flash", b};

  uint32_t ps = ESP.getPsramSize();
  out[n++] = {"PSRAM", ps ? fmtBytes(ps) : String("無")};

  out[n++] = {"儲存", fmtBytes(SPIFFS.usedBytes())};
  out[n++] = {"容量", fmtBytes(SPIFFS.totalBytes())};
  out[n++] = {"記憶", fmtBytes(ESP.getFreeHeap())};
  out[n++] = {"最少", fmtBytes(ESP.getMinFreeHeap())};
  out[n++] = {"時間", fmtUptime(millis() / 1000)};
  out[n++] = {"重置", resetReasonText()};

  snprintf(b, sizeof(b), "%u%%", (unsigned)g_app.brightness);
  out[n++] = {"亮度", b};

  if (g_app.battery.present) {
    snprintf(b, sizeof(b), "%.2f V", (double)g_app.battery.voltage);
    out[n++] = {"電池", b};
    snprintf(b, sizeof(b), "%u%%", (unsigned)g_app.battery.percent);
    out[n++] = {"電量", b};
  } else {
    out[n++] = {"電池", "USB 供電"};
  }
  return n;
}

int buildAboutRows(Row *out) {
  int n = 0;
  out[n++] = {"名稱", FW_NAME};
  out[n++] = {"版本", FW_VERSION};
  out[n++] = {"建置", __DATE__};
  out[n++] = {"開發板", "T-Display-S3"};
  out[n++] = {"字型", "Unifont 16px"};
  out[n++] = {"字型授權", "GPL2/OFL"};
  out[n++] = {"網頁", "nav.local"};
  return n;
}

// ---------------------------------------------------------------------------
// 顯示設定
// ---------------------------------------------------------------------------
void drawDispPage() {
  s_tft.fillRect(0, CONTENT_Y, SCR_W, FOOTER_Y - CONTENT_Y, C_BG);
  drawHeader("顯示設定", nullptr);

  Txt::drawCenter(160, 44, "背光亮度", C_MUTED);

  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", (unsigned)g_app.brightness);
  s_tft.setTextDatum(MC_DATUM);
  s_tft.setTextColor(C_ACCENT, C_BG);
  s_tft.setTextFont(4);
  s_tft.drawString(buf, 160, 74);

  int barX = 80, barY = 94, barW = 160, barH = 12;
  s_tft.drawRect(barX, barY, barW, barH, C_BORDER);
  int fill = (barW - 4) * g_app.brightness / 100;
  if (fill > 0) s_tft.fillRect(barX + 2, barY + 2, fill, barH - 4, C_ACCENT);

  // 10 段刻度
  for (int i = 1; i < 10; i++) {
    int x = barX + 2 + (barW - 4) * i / 10;
    s_tft.drawFastVLine(x, barY + 2, barH - 4, C_BG);
  }

  if (g_app.battery.present) {
    snprintf(buf, sizeof(buf), "%.2fV", (double)g_app.battery.voltage);
    String line = String("電池 ") + buf + " / " +
                  String((unsigned)g_app.battery.percent) + "%";
    Txt::drawCenter(160, 112, line.c_str(), C_TEXT, 300);
  } else {
    Txt::drawCenter(160, 112, "電池：USB 供電", C_TEXT, 300);
  }

  Txt::drawCenter(160, 132, "0 調整亮度 · 14長按返回", C_MUTED, 300);

  drawFooter("0:調整   14:返回");
}

// ---------------------------------------------------------------------------
// 開機畫面
// ---------------------------------------------------------------------------
void drawSplash() {
  s_tft.fillScreen(C_BG);
  s_tft.fillRect(0, 0, SCR_W, 4, C_ACCENT);

  Txt::drawCenter(160, 18, FW_SHORT, C_ACCENT);
  if (s_splashTitle[0]) Txt::drawCenter(160, 42, s_splashTitle, C_WHITE, 300);
  if (s_splashL1[0]) Txt::drawCenter(160, 66, s_splashL1, C_MUTED, 300);
  if (s_splashL2[0]) Txt::drawCenter(160, 86, s_splashL2, C_MUTED, 300);

  if (s_splashProg >= 0) {
    int p = s_splashProg;
    if (p > 100) p = 100;
    s_tft.drawRect(70, 108, 180, 10, C_BORDER);
    if (p > 0) s_tft.fillRect(71, 109, 178 * p / 100, 8, C_ACCENT);
    char b[8];
    snprintf(b, sizeof(b), "%d%%", p);
    Txt::drawCenter(160, 122, b, C_MUTED);
  }

  char ver[16];
  snprintf(ver, sizeof(ver), "v%s", FW_VERSION);
  Txt::drawCenter(160, 134, ver, C_MUTED);
  Txt::drawCenter(160, 152, FW_BOARD, C_MUTED, 300);
}

// ---------------------------------------------------------------------------
// 導航畫面
// ---------------------------------------------------------------------------
void navInstructionArea() {
  const NavStatus &n = Nav::status();
  s_tft.fillRect(100, CONTENT_Y, SCR_W - 100, 38, C_BG);
  Txt::drawWrapped(104, CONTENT_Y + 2, SCR_W - 112, 18, n.instruction,
                   C_WHITE, 2);
}

void drawCardinalLetter(float angleDeg, const char *label, bool primary) {
  const int cx = DIAL_CX, cy = DIAL_CY;
  float a  = angleDeg * kDeg;
  float sx = sinf(a), sy = -cosf(a);
  const float lr = DIAL_R + 9.0f;

  int lx = (int)(cx + sx * lr) - 4;   // ASCII 字寬 8
  int ly = (int)(cy + sy * lr) - 8;   // 字高 16

  Txt::draw(lx, ly, label, primary ? C_WHITE : C_MUTED);
}

void drawCardinalArrow(float angleDeg, const char *label, bool primary) {
  const int cx = DIAL_CX, cy = DIAL_CY;
  float a  = angleDeg * kDeg;
  float sx = sinf(a), sy = -cosf(a);
  float px = -sy, py = sx;

  const float tip  = DIAL_R - 1.0f;
  const float base = DIAL_R - 9.0f;
  const float hw   = 3.5f;
  uint16_t    c    = primary ? C_ACCENT : C_ACCENT_DIM;

  s_tft.fillTriangle((int)(cx + sx * tip), (int)(cy + sy * tip),
                     (int)(cx + sx * base + px * hw),
                     (int)(cy + sy * base + py * hw),
                     (int)(cx + sx * base - px * hw),
                     (int)(cy + sy * base - py * hw), c);
  drawCardinalLetter(angleDeg, label, primary);
}

void navCardinalMarks() {
  drawCardinalArrow(0.0f, "N", true);
  drawCardinalArrow(90.0f, "E", false);
  drawCardinalArrow(180.0f, "S", false);
  drawCardinalArrow(270.0f, "W", false);
}

void navDialFrame() {
  const int cx = DIAL_CX, cy = DIAL_CY, r = DIAL_R;

  s_tft.fillCircle(cx, cy, r, C_PANEL);
  s_tft.drawCircle(cx, cy, r, C_BORDER);

  for (int i = 0; i < 36; i++) {
    if (i % 9 == 0) continue;   // 四個正方位改畫箭頭
    float a = (float)i * 10.0f * kDeg;
    float sx = sinf(a), sy = -cosf(a);
    float r0 = (float)(r - 6);
    s_tft.drawLine((int)(cx + sx * r0), (int)(cy + sy * r0),
                   (int)(cx + sx * (r - 2)), (int)(cy + sy * (r - 2)),
                   C_BORDER);
  }

  // 東南西北：內圈箭頭 + 外圈 N/E/S/W
  navCardinalMarks();
}

void navDialNeedle() {
  const NavStatus &n = Nav::status();
  const int cx = DIAL_CX, cy = DIAL_CY;

  s_tft.fillCircle(cx, cy, DIAL_R - 9, C_PANEL);

  if (n.arrived) {
    for (int i = -2; i <= 2; i++) {
      s_tft.drawLine(cx - 10, cy + i, cx - 3, cy + 8 + i, C_GREEN);
      s_tft.drawLine(cx - 3, cy + 8 + i, cx + 12, cy - 9 + i, C_GREEN);
    }
    navCardinalMarks();
    return;
  }

  float rad = n.bearing * kDeg;
  float sx = sinf(rad), sy = -cosf(rad);
  float px = -sy, py = sx;

  const float tip = 18.0f, base = 3.0f, hw = 6.5f;
  s_tft.fillTriangle((int)(cx + sx * tip), (int)(cy + sy * tip),
                     (int)(cx + sx * base + px * hw),
                     (int)(cy + sy * base + py * hw),
                     (int)(cx + sx * base - px * hw),
                     (int)(cy + sy * base - py * hw),
                     C_ACCENT);

  // 尾翼
  s_tft.fillTriangle((int)(cx - sx * 14), (int)(cy - sy * 14),
                     (int)(cx + sx * 2 + px * 5),
                     (int)(cy + sy * 2 + py * 5),
                     (int)(cx + sx * 2 - px * 5),
                     (int)(cy + sy * 2 - py * 5),
                     C_ACCENT_DIM);

  s_tft.fillCircle(cx, cy, 4, C_BG);
  s_tft.drawCircle(cx, cy, 4, C_BORDER);

  // 指針重繪後，把四個方位字母補回來（畫在最上層）
  navCardinalMarks();
}

void navBottom() {
  const NavStatus &n = Nav::status();

  s_tft.fillRect(100, 78, SCR_W - 100, FOOTER_Y - 78, C_BG);

  String d = fmtDist(n.distToNext);
  s_tft.setTextDatum(MC_DATUM);
  s_tft.setTextColor(n.arrived ? C_GREEN : C_WHITE, C_BG);
  s_tft.setTextFont(4);
  s_tft.drawString(d, 206, 92);

  char line[72];
  snprintf(line, sizeof(line), "剩 %s / %s",
           fmtDistShort(n.remainingDist).c_str(),
           fmtDurShort(n.remainingTime).c_str());
  Txt::drawCenter(206, 110, line, C_MUTED, 204);

  Txt::drawCenter(206, 128, n.destName, C_TEXT, 204);
}

void navEmpty() {
  const int cx = 160;

  s_tft.drawCircle(cx, 48, 16, C_BORDER);
  s_tft.drawTriangle(cx, 38, cx - 8, 58, cx + 8, 58, C_ACCENT_DIM);
  s_tft.drawFastVLine(cx, 58, 6, C_ACCENT_DIM);

  Txt::drawCenter(cx, 72, "尚未規劃路線", C_TEXT, 300);
  Txt::drawWrapped(20, 94, 280, 18,
                   "請用手機或電腦開啟下列網址，於地圖設定目的地：",
                   C_MUTED, 2);
  Txt::drawCenter(cx, 130, "http://nav.local", C_ACCENT, 300);
}

void drawNavFull() {
  const NavStatus &n = Nav::status();

  s_tft.fillRect(0, CONTENT_Y, SCR_W, FOOTER_Y - CONTENT_Y, C_BG);

  if (!n.active) {
    navEmpty();
  } else {
    navInstructionArea();
    navDialFrame();
    navDialNeedle();
    navBottom();
  }

  char hint[72];
  if (!n.active && g_app.wifi.apActive) {
    snprintf(hint, sizeof(hint), "14長按返回   AP %s / %s",
             g_app.wifi.apSsid.c_str(), AP_PASS);
  } else if (n.active) {
    snprintf(hint, sizeof(hint), "步驟 %u/%u   %s %.0f°   14長按返回",
             (unsigned)n.stepIndex + 1, (unsigned)n.stepCount,
             bearingName(n.bearing), (double)n.bearing);
  } else {
    snprintf(hint, sizeof(hint), "14長按:返回");
  }
  drawFooter(hint);

  s_navActive = n.active;
  s_navSteps  = n.stepCount;
  s_navRev    = n.rev;
}

void updateNav() {
  if (s_ov != OV_NONE) return;

  const NavStatus &n = Nav::status();

  if (!n.active) {
    if (s_navActive || n.rev != s_navRev) drawNavFull();
    return;
  }
  if (!s_navActive || n.stepCount != s_navSteps) {
    drawNavFull();
    return;
  }
  if (n.rev == s_navRev) return;

  s_navRev = n.rev;
  navInstructionArea();
  navDialNeedle();
  navBottom();
}

// ---------------------------------------------------------------------------
// 覆蓋層
// ---------------------------------------------------------------------------
void overlayModal() {
  const int x = 40, y = 36, w = 240, h = 100;

  s_tft.fillRect(x, y, w, h, C_PANEL2);
  s_tft.drawRect(x, y, w, h, C_ACCENT);
  s_tft.drawFastHLine(x + 2, y + 2, w - 4, C_ACCENT_DIM);

  Txt::drawCenter(x + w / 2, y + 10, s_ovTitle, C_WHITE, w - 20);
  if (s_ovL1[0]) Txt::drawCenter(x + w / 2, y + 38, s_ovL1, C_TEXT, w - 20);
  if (s_ovL2[0]) Txt::drawCenter(x + w / 2, y + 62, s_ovL2, C_MUTED, w - 20);
}

void overlayToast() {
  const int x = 20, y = 108, w = 280, h = 30;

  s_tft.fillRect(x, y, w, h, C_PANEL2);
  s_tft.drawRect(x, y, w, h, s_toastColor);
  Txt::drawCenter(x + w / 2, y + 7, s_ovL1, s_toastColor, w - 20);
}

void overlayOta() {
  s_tft.fillScreen(C_BG);
  s_tft.fillRect(0, 0, SCR_W, 4, C_ACCENT);

  Txt::drawCenter(160, 14, s_ovTitle, C_WHITE, 300);

  if (s_otaResult) {
    if (s_otaOk) {
      for (int i = -3; i <= 3; i++) {
        s_tft.drawLine(142, 62 + i, 158, 80 + i, C_GREEN);
        s_tft.drawLine(158, 80 + i, 190, 46 + i, C_GREEN);
      }
    } else {
      for (int i = -3; i <= 3; i++) {
        s_tft.drawLine(144, 48 + i, 184, 88 + i, C_RED);
        s_tft.drawLine(184, 48 + i, 144, 88 + i, C_RED);
      }
    }
    if (s_ovL1[0]) Txt::drawCenter(160, 108, s_ovL1, C_TEXT, 300);
    if (s_ovL2[0]) Txt::drawCenter(160, 132, s_ovL2, C_MUTED, 300);
    return;
  }

  char pct[8];
  snprintf(pct, sizeof(pct), "%u%%", (unsigned)s_otaPct);
  s_tft.setTextDatum(MC_DATUM);
  s_tft.setTextColor(C_ACCENT, C_BG);
  s_tft.setTextFont(4);
  s_tft.drawString(pct, 160, 54);

  const int barX = 80, barY = 82, barW = 160, barH = 14;
  s_tft.drawRect(barX, barY, barW, barH, C_BORDER);
  int fill = (barW - 4) * s_otaPct / 100;
  if (fill > 0) s_tft.fillRect(barX + 2, barY + 2, fill, barH - 4, C_ACCENT);

  char bytes[48];
  snprintf(bytes, sizeof(bytes), "%s / %s", fmtBytes(s_otaDone).c_str(),
           fmtBytes(s_otaTotal).c_str());
  Txt::drawCenter(160, 108, bytes, C_MUTED, 300);

  Txt::drawCenter(160, 128, "更新中，請勿斷電", C_RED, 300);
  Txt::drawCenter(160, 148, "完成後自動重新啟動", C_MUTED, 300);
}

void drawOverlay() {
  switch (s_ov) {
    case OV_MODAL: overlayModal(); break;
    case OV_TOAST: overlayToast(); break;
    case OV_OTA:   overlayOta(); break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// 公用：文字複製（UTF-8 安全）
// ---------------------------------------------------------------------------
void copyLine(char *dst, size_t cap, const char *src, int maxW) {
  if (!dst || cap == 0) return;
  if (!src) {
    dst[0] = 0;
    return;
  }
  Txt::ellipsize(src, maxW, dst, cap);
}

// ---------------------------------------------------------------------------
// 畫面重繪
// ---------------------------------------------------------------------------
void drawBase() {
  switch (s_view) {
    case Ui::View::Menu:
      drawMenu();
      break;

    case Ui::View::WifiInfo:
      drawWifiPage();
      break;

    case Ui::View::SysInfo: {
      Row rows[18];
      drawInfoPage("系統狀態", rows, buildSysRows(rows));
      break;
    }

    case Ui::View::About: {
      Row rows[10];
      drawInfoPage("關於本機", rows, buildAboutRows(rows));
      break;
    }

    case Ui::View::DispInfo:
      drawDispPage();
      break;

    case Ui::View::Nav:
      drawNavFull();
      break;
  }
}

void goBack() {
  s_view  = Ui::View::Menu;
  s_page  = 0;
  s_ov    = (s_ov == OV_OTA) ? OV_OTA : OV_NONE;
  if (s_ov == OV_NONE) drawBase();
  drawOverlay();
}

void activate(Act act) {
  switch (act) {
    case ACT_SUB:
      s_level = 1;
      s_sel[1] = 0;
      break;
    case ACT_BACK:
      s_level = 0;
      break;
    case ACT_NAV:
      Ui::setView(Ui::View::Nav);
      return;
    case ACT_ABOUT:
      Ui::setView(Ui::View::About);
      return;
    case ACT_WIFI:
      Ui::setView(Ui::View::WifiInfo);
      return;
    case ACT_SYSINFO:
      Ui::setView(Ui::View::SysInfo);
      return;
    case ACT_DISP:
      Ui::setView(Ui::View::DispInfo);
      return;
  }
  drawMenu();
}

}  // namespace

// ===========================================================================
//  對外 API
// ===========================================================================
namespace Ui {

void begin() {
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  s_prefs.begin("ui", false);
  uint8_t bl = s_prefs.getUChar("bl", 80);
  if (bl < 10 || bl > 100) bl = 80;
  g_app.brightness = bl;

  s_tft.init();
  s_tft.setRotation(1);
  s_tft.fillScreen(C_BG);
  s_tft.setTextDatum(TL_DATUM);
  s_tft.setTextFont(1);

  Txt::begin(&s_tft);
  s_ready = true;

  applyBrightness();

  pinMode(PIN_BAT_VOLT, INPUT);
  analogSetPinAttenuation(PIN_BAT_VOLT, ADC_11db);
  pollBattery(true);

  s_view      = View::Menu;
  s_statusSig = "";
  drawStatus();
}

void loop() {
  if (!s_ready) return;

  pollBattery();
  if (s_splashActive) return;

  refreshStatus();

  if (s_view == View::Nav) updateNav();

  if (s_ov == OV_TOAST && (int32_t)(millis() - s_toastEnd) >= 0) {
    s_ov = OV_NONE;
    redraw();
  }
}

void redraw() {
  if (!s_ready) return;

  if (s_splashActive) {
    drawSplash();
    return;
  }

  drawBase();
  drawOverlay();
}

void onSelect() {
  if (!s_ready || s_splashActive) return;
  if (s_ov == OV_OTA) return;
  if (s_ov == OV_MODAL) {
    s_ov = OV_NONE;
  }

  switch (s_view) {
    case View::Menu: {
      uint8_t count = (s_level == 0) ? MAIN_COUNT : SYS_COUNT;
      s_sel[s_level] = (uint8_t)((s_sel[s_level] + 1) % count);
      drawMenu();
      break;
    }
    case View::WifiInfo:
    case View::SysInfo:
    case View::About:
      if (s_pageCount > 1) {
        s_page = (uint8_t)((s_page + 1) % s_pageCount);
        redraw();
      }
      break;
    case View::DispInfo: {
      uint16_t next = g_app.brightness + 10;
      if (next > 100) next = 10;
      setBrightness((uint8_t)next);
      drawDispPage();
      break;
    }
    case View::Nav:
      break;
  }
}

void onConfirm() {
  if (!s_ready || s_splashActive) return;
  if (s_ov == OV_OTA) return;
  if (s_ov == OV_MODAL) {
    s_ov = OV_NONE;
  }

  switch (s_view) {
    case View::Menu: {
      const MenuItem *items = (s_level == 0) ? MAIN_ITEMS : SYS_ITEMS;
      activate(items[s_sel[s_level]].act);
      break;
    }
    case View::WifiInfo:
    case View::SysInfo:
    case View::About:
    case View::DispInfo:
      goBack();
      break;
    case View::Nav:
      break;
  }
}

void onBack() {
  if (!s_ready || s_splashActive) return;

  if (s_ov == OV_OTA) {
    if (s_otaResult) {
      s_ov = OV_NONE;
      redraw();
    }
    return;
  }

  if (s_view == View::Menu) {
    if (s_level == 1) {
      s_level = 0;
      s_ov = OV_NONE;
      drawMenu();
    }
    return;
  }

  goBack();
}

void setView(View v) {
  s_view = v;
  s_page = 0;
  s_navRev = 0xFFFFFFFFu;
  s_ov = OV_NONE;
  redraw();
}

View view() { return s_view; }

void setBrightness(uint8_t percent, bool save) {
  if (percent < 5) percent = 5;
  if (percent > 100) percent = 100;
  g_app.brightness = percent;
  applyBrightness();
  if (save) s_prefs.putUChar("bl", percent);
}

uint8_t brightness() { return g_app.brightness; }

void toast(const char *msg, uint16_t color) {
  if (!s_ready || s_splashActive) return;
  if (s_ov == OV_OTA) return;

  copyLine(s_ovL1, sizeof(s_ovL1), msg, 140);
  s_toastColor = color;
  s_toastEnd  = millis() + 2500;
  s_ov        = OV_TOAST;
  redraw();
}

void modal(const char *title, const char *line1, const char *line2) {
  if (!s_ready) return;

  copyLine(s_ovTitle, sizeof(s_ovTitle), title, 140);
  copyLine(s_ovL1, sizeof(s_ovL1), line1, 140);
  copyLine(s_ovL2, sizeof(s_ovL2), line2, 140);
  s_ov = OV_MODAL;

  if (s_splashActive) {
    drawSplash();
  } else {
    redraw();
  }
}

void closeModal() {
  if (s_ov != OV_MODAL) return;
  s_ov = OV_NONE;
  if (s_splashActive) {
    drawSplash();
  } else {
    redraw();
  }
}

void otaProgress(const char *title, uint32_t written, uint32_t total) {
  if (!s_ready) return;

  int pct = total ? (int)((uint64_t)written * 100ULL / total) : 0;
  if (pct > 100) pct = 100;

  if (s_ov == OV_OTA && !s_otaResult && s_otaPct == (uint8_t)pct &&
      !strncmp(s_ovTitle, title, sizeof(s_ovTitle) - 1)) {
    return;
  }

  copyLine(s_ovTitle, sizeof(s_ovTitle), title, 140);
  s_otaResult = false;
  s_otaPct    = (uint8_t)pct;
  s_otaDone   = written;
  s_otaTotal  = total;
  s_ov        = OV_OTA;
  overlayOta();
}

void otaResult(bool ok, const char *msg) {
  if (!s_ready) return;
  s_otaResult = true;
  s_otaOk     = ok;
  copyLine(s_ovTitle, sizeof(s_ovTitle), "OTA 更新", 140);
  copyLine(s_ovL1, sizeof(s_ovL1), msg, 140);
  copyLine(s_ovL2, sizeof(s_ovL2),
           ok ? "裝置即將重新啟動" : "請檢查檔案後再試一次", 140);
  s_ov = OV_OTA;
  overlayOta();
}

void splash(const char *title, const char *line1, const char *line2,
            int progress) {
  if (!s_ready) return;

  copyLine(s_splashTitle, sizeof(s_splashTitle), title, 154);
  copyLine(s_splashL1, sizeof(s_splashL1), line1, 154);
  copyLine(s_splashL2, sizeof(s_splashL2), line2, 154);
  s_splashProg = progress;
  s_splashActive = true;
  drawSplash();
}

void bootReady() {
  if (!s_ready) return;
  s_splashActive = false;
  s_level = 0;
  s_sel[0] = s_sel[1] = 0;
  s_view = View::Menu;
  s_ov = OV_NONE;
  drawStatus();
  drawMenu();
}

}  // namespace Ui
