#include "web_server.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <Update.h>
#include <esp_partition.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>

#include "app_state.h"
#include "config.h"
#include "display_ui.h"
#include "json_compat.h"
#include "nav_engine.h"
#include "wifi_manager.h"

namespace {

WebServer server(80);

uint32_t s_rebootAt   = 0;
bool     s_rebooting  = false;

size_t   s_otaTotal   = 0;
size_t   s_otaWritten = 0;
bool     s_otaOk      = false;
bool     s_otaIsFs    = false;
char     s_otaErr[96] = "";

// ---------------------------------------------------------------------------
// 回應工具
// ---------------------------------------------------------------------------
void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json; charset=utf-8", body);
}

template <typename TDoc>
void sendJsonDoc(TDoc &doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  sendJson(code, out);
}

void sendError(int code, const char *msg) {
  NAV_JSON_DOC(doc, 256);
  doc["ok"]  = false;
  doc["msg"] = msg;
  sendJsonDoc(doc, code);
}

String jsonBody() {
  String body = server.arg("plain");
  return body;
}

// ---------------------------------------------------------------------------
// /api/geocode — Nominatim 位址搜尋代理
//
// 由裝置（STA 端）代為查詢，手機只連 AP 時也能搜尋地址。
// 針對台灣地址做兩段降級：
//   1. 完整地址（含門牌）
//   2. 去掉門牌（16號 / 16之1號）
//   3. 再去掉巷弄（16巷 / 5弄）
// 門牌未建檔時仍會回傳最近的道路 / 地區。
// ---------------------------------------------------------------------------
String urlEncode(const String &in) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(in.length() * 3 + 8);

  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

bool nominatimSearch(const String &query, String &body, String &err) {
  String url =
      "https://nominatim.openstreetmap.org/search?format=jsonv2"
      "&addressdetails=0&limit=6&accept-language=zh-TW"
      "&countrycodes=tw&q=";
  url += urlEncode(query);

  WiFiClientSecure tls;
  tls.setInsecure();
  tls.setHandshakeTimeout(10);

  HTTPClient http;
  http.setTimeout(12000);
  http.setConnectTimeout(9000);
  http.setReuse(false);
  http.setUserAgent("LILYGO-T-Display-S3-NAV/1.0 (+http://nav.local)");

  if (!http.begin(tls, url)) {
    err = "無法建立 HTTPS 連線";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = String("Nominatim HTTP ") + code;
    http.end();
    return false;
  }

  body = http.getString();
  http.end();
  return body.length() > 0;
}

// 從 suffix 位置往前跳過門牌數字（支援 16、16-1、16之1）
int skipNumberBack(const String &s, int i) {
  while (i > 0) {
    char c = s[i - 1];
    if ((c >= '0' && c <= '9') || c == '-') {
      i--;
      continue;
    }
    // UTF-8「之」(E4 B9 8B)
    if (i >= 3 && (uint8_t)s[i - 3] == 0xE4 && (uint8_t)s[i - 2] == 0xB9 &&
        (uint8_t)s[i - 1] == 0x8B) {
      i -= 3;
      continue;
    }
    break;
  }
  return i;
}

// 移除結尾的門牌：桃園市平鎮區中正路16號 -> 桃園市平鎮區中正路
String stripHouseNumber(const String &src) {
  int pos = src.lastIndexOf("號");
  if (pos <= 0) return "";

  int i = skipNumberBack(src, pos);
  if (i >= pos) return "";

  String out = src.substring(0, i);
  out.trim();
  return (out.length() >= 4) ? out : String("");
}

// 移除結尾的巷 / 弄：中正路16巷5弄 -> 中正路16巷
String stripLane(const String &src) {
  int pos = src.lastIndexOf("巷");
  int pos2 = src.lastIndexOf("弄");
  if (pos2 > pos) pos = pos2;
  if (pos <= 0) return "";

  int i = skipNumberBack(src, pos);
  if (i >= pos) return "";

  String out = src.substring(0, i);
  out.trim();
  return (out.length() >= 4) ? out : String("");
}

void handleGeocode() {
  String raw = server.arg("q");
  raw.trim();
  if (raw.length() < 2 || raw.length() > 120) {
    sendError(400, "請輸入 2–120 字元的地址");
    return;
  }

  String queries[3];
  uint8_t nq = 0;
  auto addQuery = [&](const String &s) {
    String t = s;
    t.trim();
    if (t.length() < 2) return;
    for (uint8_t i = 0; i < nq; i++) {
      if (queries[i] == t) return;
    }
    if (nq < 3) queries[nq++] = t;
  };

  addQuery(raw);
  String s1 = stripHouseNumber(raw);
  addQuery(s1);
  String s2 = stripLane(s1.length() ? s1 : raw);
  addQuery(s2);
  addQuery(stripLane(s2));

  String used, body, err;
  bool got = false;

  for (uint8_t i = 0; i < nq; i++) {
    if (i > 0) delay(1100);   // Nominatim 使用政策：最多 1 次 / 秒

    if (!nominatimSearch(queries[i], body, err)) {
      sendError(502, err.c_str());
      return;
    }
    if (body.indexOf("\"lat\"") >= 0) {
      used = queries[i];
      got = true;
      break;
    }
  }

  NAV_JSON_DOC(doc, 8192);
  doc["ok"]    = true;
  doc["query"] = raw;
  doc["used"]  = got ? used : queries[nq - 1];
  doc["approx"] = got ? (used != raw) : true;
  JsonArray arr = doc.createNestedArray("results");

  if (got) {
    NAV_JSON_DOC(filter, 384);
    filter[0]["display_name"] = true;
    filter[0]["name"]         = true;
    filter[0]["type"]         = true;
    filter[0]["lat"]          = true;
    filter[0]["lon"]          = true;

    NAV_JSON_DOC(list, 8192);
    DeserializationError e =
        deserializeJson(list, body, DeserializationOption::Filter(filter));
    if (e) {
      sendError(502, "Nominatim 回應解析失敗");
      return;
    }

    for (JsonObject o : list.as<JsonArray>()) {
      JsonObject r = arr.createNestedObject();
      r["display"] = (const char *)(o["display_name"] | "");
      r["name"]    = (const char *)(o["name"] | "");
      r["type"]    = (const char *)(o["type"] | "");
      r["lat"]     = (const char *)(o["lat"] | "");
      r["lon"]     = (const char *)(o["lon"] | "");
    }
  }

  doc["count"] = arr.size();
  sendJsonDoc(doc);
}

// ---------------------------------------------------------------------------
// /api/status
// ---------------------------------------------------------------------------
void handleStatus() {
  NAV_JSON_DOC(doc, 4096);

  doc["ok"]        = true;
  doc["uptime"]    = millis() / 1000;
  doc["brightness"] = g_app.brightness;

  JsonObject fw = doc.createNestedObject("fw");
  fw["name"]     = FW_NAME;
  fw["version"]  = FW_VERSION;
  fw["build"]    = FW_BUILD;
  fw["board"]    = FW_BOARD;
  fw["arduino"]  = "1.8.19+";
  char core[16];
  snprintf(core, sizeof(core), "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR,
           ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  fw["core"] = core;

  JsonObject mem = doc.createNestedObject("heap");
  mem["free"]   = ESP.getFreeHeap();
  mem["min"]    = ESP.getMinFreeHeap();
  mem["psram"]  = ESP.getPsramSize();
  mem["flash"]  = ESP.getFlashChipSize();
  mem["chip"]   = ESP.getChipModel();
  mem["cpu"]    = ESP.getCpuFreqMHz();
  mem["reset"]  = resetReasonText();

  JsonObject fs = doc.createNestedObject("spiffs");
  fs["used"]  = SPIFFS.usedBytes();
  fs["total"] = SPIFFS.totalBytes();

  const WifiSnapshot &w = g_app.wifi;
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["state"]     = wifiStateText();
  wifi["connected"] = w.staConnected;
  wifi["ssid"]      = w.ssid;
  wifi["ip"]        = w.ip;
  wifi["rssi"]      = w.rssi;
  wifi["channel"]   = w.channel;
  wifi["mac"]       = w.mac.length() ? w.mac : Net::macString();
  wifi["apActive"]  = w.apActive;
  wifi["apSsid"]    = w.apSsid;
  wifi["apPass"]    = w.apPass;
  wifi["apIp"]      = w.apIp;
  wifi["host"]      = w.host;
  wifi["savedSsid"] = Net::savedSsid();

  JsonObject bat = doc.createNestedObject("battery");
  bat["present"] = g_app.battery.present;
  bat["voltage"] = g_app.battery.voltage;
  bat["percent"] = g_app.battery.percent;

  const NavStatus &n = Nav::status();
  JsonObject nav = doc.createNestedObject("nav");
  nav["active"]      = n.active;
  nav["arrived"]     = n.arrived;
  nav["instruction"] = n.instruction;
  nav["road"]        = n.road;
  nav["destName"]    = n.destName;
  nav["distToNext"]  = n.distToNext;
  nav["remaining"]   = n.remainingDist;
  nav["remainingTime"] = n.remainingTime;
  nav["bearing"]     = n.bearing;
  nav["step"]        = (int)n.stepIndex + 1;
  nav["steps"]       = (int)n.stepCount;

  sendJsonDoc(doc);
}

// ---------------------------------------------------------------------------
// /api/spiffs
// ---------------------------------------------------------------------------
void walkDir(const String &path, JsonArray arr, int depth) {
  if (depth > 5) return;

  File dir = SPIFFS.open(path);
  if (!dir || !dir.isDirectory()) return;

  File f = dir.openNextFile();
  while (f) {
    String name = f.name();
    if (!name.startsWith("/")) {
      name = (path == "/") ? String("/") + name : path + name;
    }
    if (f.isDirectory()) {
      String sub = name;
      if (!sub.endsWith("/")) sub += "/";
      walkDir(sub, arr, depth + 1);
    } else {
      JsonObject o = arr.createNestedObject();
      o["path"] = name;
      o["size"] = f.size();
    }
    f = dir.openNextFile();
  }
  dir.close();
}

void handleSpiffs() {
  NAV_JSON_DOC(doc, 8192);
  doc["ok"]    = true;
  doc["used"]  = SPIFFS.usedBytes();
  doc["total"] = SPIFFS.totalBytes();

  JsonArray arr = doc.createNestedArray("files");
  walkDir("/", arr, 0);

  sendJsonDoc(doc);
}

// ---------------------------------------------------------------------------
// /api/wifi/scan
// ---------------------------------------------------------------------------
const char *encName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:         return "開放";
    case WIFI_AUTH_WEP:          return "WEP";
    case WIFI_AUTH_WPA_PSK:      return "WPA";
    case WIFI_AUTH_WPA2_PSK:     return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-企業";
    case WIFI_AUTH_WPA3_PSK:     return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:return "WPA2/WPA3";
    default:                     return "其他";
  }
}

void handleWifiScan() {
  Ui::modal("掃描 WiFi", "正在搜尋附近網路…", "約需數秒");

  int n = WiFi.scanNetworks(false, true);
  if (n < 0) n = 0;

  NAV_JSON_DOC(doc, 16384);
  doc["ok"] = true;
  JsonArray arr = doc.createNestedArray("networks");

  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    JsonObject o = arr.createNestedObject();
    o["ssid"]    = ssid;
    o["rssi"]    = WiFi.RSSI(i);
    o["channel"] = WiFi.channel(i);
    o["secure"]  = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    o["enc"]     = encName(WiFi.encryptionType(i));
  }
  WiFi.scanDelete();

  Ui::closeModal();
  sendJsonDoc(doc);
}

// ---------------------------------------------------------------------------
// /api/wifi  POST / DELETE
// ---------------------------------------------------------------------------
void handleWifiSave() {
  String body = jsonBody();
  NAV_JSON_DOC(in, 512);
  DeserializationError err = deserializeJson(in, body);

  String ssid, pass;
  if (!err) {
    ssid = (const char *)(in["ssid"] | "");
    pass = (const char *)(in["pass"] | "");
  } else {
    ssid = server.arg("ssid");
    pass = server.arg("pass");
  }

  String msg;
  bool ok = Net::connect(ssid, pass, msg);

  NAV_JSON_DOC(doc, 512);
  doc["ok"]  = ok;
  doc["msg"] = ok ? "已連線" : msg;
  doc["ip"]  = ok ? Net::ip() : String("");
  doc["ssid"] = ok ? Net::savedSsid() : ssid;

  if (ok) {
    Ui::toast("WiFi 已連線", C_GREEN);
  } else {
    Ui::toast(msg.c_str(), C_RED);
  }
  sendJsonDoc(doc, ok ? 200 : 400);
}

void handleWifiForget() {
  Net::forget();
  Ui::toast("已清除 WiFi 設定", C_ORANGE);

  NAV_JSON_DOC(doc, 256);
  doc["ok"]  = true;
  doc["msg"] = "已清除，裝置將重新啟動";
  sendJsonDoc(doc);

  s_rebootAt = millis() + 1800;
  s_rebooting = true;
}

// ---------------------------------------------------------------------------
// /api/nav
// ---------------------------------------------------------------------------
void handleNavRoute() {
  String body = jsonBody();
  NAV_JSON_DOC(in, 1024);
  DeserializationError err = deserializeJson(in, body);
  if (err) {
    sendError(400, "路線參數格式錯誤");
    return;
  }

  double slat = in["start"]["lat"] | 999.0;
  double slon = in["start"]["lon"] | 999.0;
  double dlat = in["dest"]["lat"] | 999.0;
  double dlon = in["dest"]["lon"] | 999.0;
  const char *name = in["name"] | "";

  if (fabs(slat) > 90 || fabs(dlat) > 90 || fabs(slon) > 180 ||
      fabs(dlon) > 180) {
    sendError(400, "座標超出範圍");
    return;
  }

  String msg;
  bool ok = Nav::fetchRoute(slat, slon, dlat, dlon, name, msg);

  if (!ok) {
    Ui::toast(msg.c_str(), C_RED);
    sendError(502, msg.c_str());
    return;
  }

  Ui::setView(Ui::View::Nav);
  Ui::toast("路線已建立", C_GREEN);
  sendJson(200, Nav::toJson(true, true));
}

void handleNavPosition() {
  String body = jsonBody();
  NAV_JSON_DOC(in, 512);
  DeserializationError err = deserializeJson(in, body);
  if (err) {
    sendError(400, "位置格式錯誤");
    return;
  }

  double lat = in["lat"] | 999.0;
  double lon = in["lon"] | 999.0;
  if (fabs(lat) > 90 || fabs(lon) > 180) {
    sendError(400, "座標超出範圍");
    return;
  }

  bool hasHeading = !in["heading"].isNull();
  float heading = in["heading"] | 0.0f;

  Nav::setPosition(lat, lon, hasHeading, heading);

  NAV_JSON_DOC(doc, 320);
  doc["ok"]      = true;
  doc["active"]  = Nav::status().active;
  doc["distToNext"] = Nav::status().distToNext;
  doc["remaining"]  = Nav::status().remainingDist;
  doc["instruction"] = Nav::status().instruction;
  sendJsonDoc(doc);
}

void handleNavGet() {
  sendJson(200, Nav::toJson(true, true));
}

void handleNavClear() {
  Nav::clear();
  if (Ui::view() == Ui::View::Nav) Ui::setView(Ui::View::Menu);
  Ui::toast("已清除導航路線", C_ORANGE);

  NAV_JSON_DOC(doc, 256);
  doc["ok"]  = true;
  doc["msg"] = "已清除";
  sendJsonDoc(doc);
}

// ---------------------------------------------------------------------------
// OTA 上傳（Update 函式庫）
// ---------------------------------------------------------------------------
void otaSetError(const char *msg) {
  snprintf(s_otaErr, sizeof(s_otaErr), "%s", (msg && msg[0]) ? msg : "更新失敗");
}

void otaUpload() {
  HTTPUpload &up = server.upload();
  const char *title = s_otaIsFs ? "網頁檔案更新" : "韌體更新";

  switch (up.status) {
    case UPLOAD_FILE_START: {
      s_otaWritten = 0;
      s_otaOk      = false;
      s_otaTotal   = 0;
      s_otaErr[0]  = '\0';
      // 前端以 ?size= 帶入原始檔大小（multipart 的 totalSize 在此核心版本為 0）
      if (server.hasArg("size")) {
        s_otaTotal = (size_t)strtoul(server.arg("size").c_str(), nullptr, 10);
      }
      Serial.printf("[ota] %s：%s（%u bytes）\n", title, up.filename.c_str(),
                    (unsigned)s_otaTotal);
      Ui::otaProgress(title, 0, s_otaTotal);

      // 先檢查分割區與檔案大小，不要等上傳完 9.8 MB 才失敗
      if (s_otaIsFs) {
        const esp_partition_t *part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
        if (!part) {
          otaSetError("找不到 SPIFFS 分割區");
          Serial.printf("[ota] %s\n", s_otaErr);
          Ui::otaResult(false, s_otaErr);
          break;  // 未呼叫 Update.begin()，後續資料直接丟棄
        }
        if (s_otaTotal > 0 && s_otaTotal != part->size) {
          snprintf(s_otaErr, sizeof(s_otaErr), "檔案 %u bytes，分割區 %u bytes，大小不符",
                   (unsigned)s_otaTotal, (unsigned)part->size);
          Serial.printf("[ota] %s\n", s_otaErr);
          Ui::otaResult(false, s_otaErr);
          break;
        }
      }

      if (s_otaIsFs) SPIFFS.end();
      if (!Update.begin(s_otaTotal > 0 ? s_otaTotal : UPDATE_SIZE_UNKNOWN,
                        s_otaIsFs ? U_SPIFFS : U_FLASH)) {
        otaSetError(Update.errorString());
        Serial.printf("[ota] 無法開始：%s\n", s_otaErr);
        Ui::otaResult(false, s_otaErr);
        if (s_otaIsFs) SPIFFS.begin(true);
      }
      break;
    }

    case UPLOAD_FILE_WRITE:
      if (!Update.isRunning()) break;
      // 第一個封包確認檔案類型，避免韌體／檔案系統上傳到錯的欄位
      if (s_otaWritten == 0 && up.currentSize > 0) {
        if (!s_otaIsFs && up.buf[0] != 0xE9) {
          otaSetError("這不是韌體檔（開頭應為 0xE9），請改用「檔案系統更新」");
          Update.abort();
          Serial.printf("[ota] %s\n", s_otaErr);
          Ui::otaResult(false, s_otaErr);
          break;
        }
        if (s_otaIsFs && up.buf[0] == 0xE9) {
          otaSetError("這是韌體檔，請改用「韌體更新」");
          Update.abort();
          SPIFFS.begin(true);
          Serial.printf("[ota] %s\n", s_otaErr);
          Ui::otaResult(false, s_otaErr);
          break;
        }
      }
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        otaSetError(Update.errorString());
        Serial.printf("[ota] 寫入失敗：%s\n", s_otaErr);
        Ui::otaResult(false, s_otaErr);
        break;
      }
      s_otaWritten += up.currentSize;
      Ui::otaProgress(title, s_otaWritten, s_otaTotal);
      break;

    case UPLOAD_FILE_END:
      if (!Update.isRunning()) break;
      // WebServer 在 FILE_WRITE 階段就會送出全部資料（含最後一個不滿
      // HTTP_UPLOAD_BUFLEN 的區塊）；FILE_END 的 currentSize 仍是該區塊，
      // 若再寫一次就等於重複寫入尾巴。spiffs.bin 剛好等於整個分割區大小，
      // 重複的尾巴會超出分割區，Update.write 失敗，OTA 因此中斷。
      if (s_otaTotal == 0) s_otaTotal = s_otaWritten;
      if (s_otaWritten != s_otaTotal) {
        otaSetError("上傳長度與檔案大小不符");
        Serial.printf("[ota] %s（%u / %u）\n", s_otaErr,
                      (unsigned)s_otaWritten, (unsigned)s_otaTotal);
        Update.abort();
        Ui::otaResult(false, s_otaErr);
        if (s_otaIsFs) SPIFFS.begin(true);
        break;
      }
      s_otaOk = Update.end(true);
      if (s_otaOk) {
        Serial.printf("[ota] %s 完成（%u bytes）\n", title, (unsigned)s_otaWritten);
        Ui::otaResult(true, s_otaIsFs ? "網頁檔案更新完成" : "韌體更新完成");
        s_rebootAt  = millis() + 2500;
        s_rebooting = true;
      } else {
        otaSetError(Update.errorString());
        Serial.printf("[ota] 結束失敗：%s\n", s_otaErr);
        Ui::otaResult(false, s_otaErr);
        if (s_otaIsFs) SPIFFS.begin(true);
      }
      break;

    case UPLOAD_FILE_ABORTED:
      Update.abort();
      if (s_otaIsFs) SPIFFS.begin(true);
      otaSetError("上傳已中斷");
      Ui::otaResult(false, s_otaErr);
      break;
  }
}

void otaUploadFirmware() {
  s_otaIsFs = false;
  otaUpload();
}

void otaUploadSpiffs() {
  s_otaIsFs = true;
  otaUpload();
}

void handleOtaDone() {
  NAV_JSON_DOC(doc, 640);
  const char *msg = s_otaOk ? "更新完成，裝置重新啟動中"
                            : (s_otaErr[0] ? (const char *)s_otaErr : "更新失敗");
  doc["ok"]      = s_otaOk;
  doc["written"] = (uint32_t)s_otaWritten;
  doc["msg"]     = msg;
  sendJsonDoc(doc, s_otaOk ? 200 : 500);
}

// ---------------------------------------------------------------------------
// /api/system/reboot
// ---------------------------------------------------------------------------
void handleReboot() {

  NAV_JSON_DOC(doc, 128);
  doc["ok"]  = true;
  doc["msg"] = "重新啟動中";
  sendJsonDoc(doc);
  s_rebootAt  = millis() + 700;
  s_rebooting = true;
}

// ---------------------------------------------------------------------------
// 靜態檔案（SPIFFS）
// ---------------------------------------------------------------------------
String mimeFor(const String &path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css"))  return "text/css; charset=utf-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".svg"))  return "image/svg+xml";
  if (path.endsWith(".png"))  return "image/png";
  if (path.endsWith(".ico"))  return "image/x-icon";
  if (path.endsWith(".woff2"))return "font/woff2";
  return "text/plain; charset=utf-8";
}

bool serveFile(String path) {
  if (path.endsWith("/")) path += "index.html";

  File f = SPIFFS.open(path, "r");
  if (!f || f.isDirectory()) {
    if (f) f.close();
    return false;
  }

  if (!path.endsWith(".html")) {
    server.sendHeader("Cache-Control", "public, max-age=600");
  } else {
    server.sendHeader("Cache-Control", "no-store");
  }

  server.streamFile(f, mimeFor(path));
  f.close();
  return true;
}

void handleRoot() {
  if (!serveFile("/index.html")) {
    server.send(500, "text/html; charset=utf-8",
                "<meta charset='utf-8'><h2>SPIFFS 尚未上傳網頁檔案</h2>"
                "<p>請使用 Arduino IDE 的 ESP32 Sketch Data Upload 上傳 "
                "data/ 資料夾，或透過序列埠重新燒錄。</p>");
  }
}

void handleNotFound() {
  String uri = server.uri();

  if (uri.indexOf("..") >= 0) {
    server.send(400, "text/plain; charset=utf-8", "Bad request");
    return;
  }

  if (serveFile(uri)) return;

  if (uri.startsWith("/api/")) {
    sendError(404, "找不到 API");
    return;
  }

  if (Net::apActive()) {
    // 入口網頁 / 捕獲任意網址
    String loc = String("http://") + WiFi.softAPIP().toString() + "/";
    server.sendHeader("Location", loc, true);
    server.send(302, "text/plain", "redirect");
    return;
  }

  server.send(404, "text/plain; charset=utf-8", "404 Not Found");
}

}  // namespace

// ===========================================================================
//  對外 API
// ===========================================================================
namespace Web {

void begin() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[web] SPIFFS 掛載失敗，靜態網頁將無法使用");
  } else {
    Serial.printf("[web] SPIFFS 已使用 %u / %u bytes\n",
                  (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
  }

  server.on("/", HTTP_GET, handleRoot);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/spiffs", HTTP_GET, handleSpiffs);
  server.on("/api/geocode", HTTP_GET, handleGeocode);

  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/wifi", HTTP_DELETE, handleWifiForget);

  server.on("/api/nav", HTTP_GET, handleNavGet);
  server.on("/api/nav", HTTP_DELETE, handleNavClear);
  server.on("/api/nav/route", HTTP_POST, handleNavRoute);
  server.on("/api/nav/position", HTTP_POST, handleNavPosition);

  server.on("/api/ota/firmware", HTTP_POST, handleOtaDone, otaUploadFirmware);
  server.on("/api/ota/spiffs", HTTP_POST, handleOtaDone, otaUploadSpiffs);
  server.on("/api/system/reboot", HTTP_POST, handleReboot);

  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("[web] 伺服器已啟動：http://" MDNS_HOST ".local");
}

void loop() {
  server.handleClient();

  if (s_rebooting && (int32_t)(millis() - s_rebootAt) >= 0) {
    Serial.println("[sys] 重新啟動…");
    Serial.flush();
    delay(80);
    ESP.restart();
  }
}

}  // namespace Web
