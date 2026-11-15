#include "wifi_manager.h"
#include "config.h"
#include "display_ui.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

namespace Net {

static Preferences s_prefs;
static DNSServer   s_dns;

static String   s_ssid;
static String   s_pass;
static String   s_apSsid;
static bool     s_hasCreds    = false;
static bool     s_dnsActive   = false;
static bool     s_mdnsStarted = false;
static uint32_t s_lastAttempt = 0;
static uint8_t  s_retries     = 0;
static uint32_t s_lastSnap    = 0;

// 晶片出廠 MAC（不依賴 WiFi netif 是否已啟動；macAddress() 在介面
// 尚未啟動時會回傳全零，導致 AP SSID 尾碼變成 0000）
static String hwMac() {
  uint64_t v = ESP.getEfuseMac();
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           (unsigned)(v & 0xFF), (unsigned)((v >> 8) & 0xFF),
           (unsigned)((v >> 16) & 0xFF), (unsigned)((v >> 24) & 0xFF),
           (unsigned)((v >> 32) & 0xFF), (unsigned)((v >> 40) & 0xFF));
  return String(buf);
}

// AP SSID = 前綴 + MAC 後 4 碼（大寫十六進位，不含冒號）
static String makeApSsid() {
  String mac = hwMac();
  mac.replace(":", "");
  return String(AP_SSID_PREFIX) + "-" + mac.substring(8);
}

static void startAp() {
  if (s_dnsActive) return;
  WiFi.mode(WIFI_AP_STA);
  if (s_apSsid.length() == 0) s_apSsid = makeApSsid();
  WiFi.softAP(s_apSsid.c_str(), AP_PASS);
  WiFi.softAPsetHostname(MDNS_HOST);
  delay(120);
  s_dns.start(53, "*", WiFi.softAPIP());
  s_dnsActive = true;
  Serial.printf("[wifi] AP 模式：%s / %s / %s\n", s_apSsid.c_str(), AP_PASS,
                WiFi.softAPIP().toString().c_str());
}

static void ensureMdns() {
  if (s_mdnsStarted || WiFi.status() != WL_CONNECTED) return;
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    s_mdnsStarted = true;
    Serial.printf("[wifi] mDNS：http://%s.local\n", MDNS_HOST);
  }
}

static void loadCreds() {
  s_ssid = s_prefs.getString("ssid", "");
  s_pass = s_prefs.getString("pass", "");
  s_hasCreds = s_ssid.length() > 0;
}

void begin() {
  s_prefs.begin("nav", false);
  loadCreds();

  WiFi.persistent(false);
  WiFi.setHostname(MDNS_HOST);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);
  s_apSsid = makeApSsid();

  if (!s_hasCreds) {
    startAp();
    snapshot();
    return;
  }

  Ui::modal("WiFi 連線中", s_ssid.c_str(), "請稍候…");
  WiFi.begin(s_ssid.c_str(), s_pass.c_str());

  uint32_t start = millis();
  while (millis() - start < WIFI_CONNECT_MS && WiFi.status() != WL_CONNECTED) {
    delay(120);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] 已連線 %s (%s)\n", s_ssid.c_str(),
                  WiFi.localIP().toString().c_str());
    ensureMdns();
  } else {
    Serial.println("[wifi] 連線失敗，啟動 AP 模式");
    startAp();
  }
  Ui::closeModal();
  snapshot();
}

void loop() {
  if (s_dnsActive) s_dns.processNextRequest();

  uint32_t now = millis();
  if (now - s_lastSnap >= 1000) {
    s_lastSnap = now;
    snapshot();
  }

  if (WiFi.status() == WL_CONNECTED) {
    ensureMdns();
    s_retries = 0;
    return;
  }

  if (!s_hasCreds) return;

  // 中斷後自動重試（前 5 次 20 秒，之後 60 秒）
  uint32_t interval = (s_retries < 5) ? WIFI_RETRY_MS : 60000;
  if (now - s_lastAttempt >= interval) {
    s_lastAttempt = now;
    s_retries++;
    Serial.printf("[wifi] 重新連線（第 %u 次）\n", (unsigned)s_retries);
    WiFi.begin(s_ssid.c_str(), s_pass.c_str());
  }
}

void snapshot() {
  WifiSnapshot &w = g_app.wifi;
  bool wasConnected = w.staConnected;

  w.staConnected = (WiFi.status() == WL_CONNECTED);
  w.apActive     = s_dnsActive;
  w.apSsid       = apSsid();
  w.apPass       = AP_PASS;
  w.host         = String(MDNS_HOST) + ".local";

  if (w.staConnected) {
    w.ssid    = WiFi.SSID();
    w.ip      = WiFi.localIP().toString();
    w.rssi    = WiFi.RSSI();
    w.channel = WiFi.channel();
    w.mac     = hwMac();
    if (!wasConnected || w.connectedAt == 0) w.connectedAt = millis();
  } else {
    w.ssid = s_ssid;
    w.ip   = "";
    w.rssi = 0;
    w.channel = 0;
    w.mac  = hwMac();
    w.connectedAt = 0;
  }

  if (w.apActive) w.apIp = WiFi.softAPIP().toString();
}

bool staConnected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return s_dnsActive; }

String ip() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (s_dnsActive) return WiFi.softAPIP().toString();
  return String("");
}

String hostname() { return String(MDNS_HOST) + ".local"; }
String macString() { return hwMac(); }

String apSsid() {
  if (s_apSsid.length() == 0) s_apSsid = makeApSsid();
  return s_apSsid;
}
String savedSsid() { return s_ssid; }

bool connect(const String &ssid, const String &pass, String &err) {
  err = "";
  if (ssid.length() == 0) {
    err = "SSID 不可為空";
    return false;
  }

  Ui::modal("WiFi 連線中", ssid.c_str(), "最多等待 15 秒");

  if (!s_dnsActive) WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t start = millis();
  while (millis() - start < WIFI_CONNECT_MS) {
    if (WiFi.status() == WL_CONNECTED) break;
    delay(120);
  }

  if (WiFi.status() != WL_CONNECTED) {
    err = "連線逾時，請確認密碼或訊號強度";
    WiFi.disconnect(false, false);
    if (!s_dnsActive) startAp();
    Ui::closeModal();
    return false;
  }

  s_ssid = ssid;
  s_pass = pass;
  s_hasCreds = true;
  s_retries = 0;
  s_lastAttempt = millis();
  s_prefs.putString("ssid", s_ssid);
  s_prefs.putString("pass", s_pass);

  ensureMdns();
  snapshot();

  Serial.printf("[wifi] 設定成功：%s (%s)\n", s_ssid.c_str(),
                WiFi.localIP().toString().c_str());
  Ui::closeModal();
  return true;
}

void forget() {
  s_prefs.remove("ssid");
  s_prefs.remove("pass");
  s_ssid = "";
  s_pass = "";
  s_hasCreds = false;
  WiFi.disconnect(true, true);
  Serial.println("[wifi] 已清除連線設定");
}

}  // namespace Net
