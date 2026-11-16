#include "nav_engine.h"
#include "json_compat.h"
#include "app_state.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <math.h>

namespace Nav {

static NavStatus s_nav;
static String    s_geometry;
static uint32_t  s_lastTick = 0;

// ---------------------------------------------------------------------------
// 地理計算
// ---------------------------------------------------------------------------
static double deg2rad(double d) { return d * 0.017453292519943295; }
static double rad2deg(double r) { return r * 57.29577951308232; }

static double distanceMeters(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371000.0;
  double p1 = deg2rad(lat1), p2 = deg2rad(lat2);
  double dp = deg2rad(lat2 - lat1);
  double dl = deg2rad(lon2 - lon1);
  double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  if (a < 0) a = 0;
  return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}

static float bearingTo(double lat1, double lon1, double lat2, double lon2) {
  double p1 = deg2rad(lat1), p2 = deg2rad(lat2);
  double dl = deg2rad(lon2 - lon1);
  double y = sin(dl) * cos(p2);
  double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double b = rad2deg(atan2(y, x));
  if (b < 0) b += 360.0;
  return (float)b;
}

static void copyUtf8(char *dst, size_t cap, const char *src) {
  if (!dst || cap == 0) return;
  if (!src) { dst[0] = 0; return; }
  const unsigned char *p = (const unsigned char *)src;
  size_t used = 0;
  while (*p) {
    int len = 1;
    if ((*p & 0xE0) == 0xC0) len = 2;
    else if ((*p & 0xF0) == 0xE0) len = 3;
    else if ((*p & 0xF8) == 0xF0) len = 4;
    if (used + (size_t)len >= cap) break;
    memcpy(dst + used, p, len);
    used += len;
    p += len;
  }
  dst[used] = 0;
}

// ---------------------------------------------------------------------------
// 轉彎提示文字
// ---------------------------------------------------------------------------
static const char *turnText(const char *modifier) {
  if (!modifier || !*modifier) return "直行";
  if (!strcmp(modifier, "left"))        return "左轉";
  if (!strcmp(modifier, "right"))       return "右轉";
  if (!strcmp(modifier, "slight left")) return "靠左轉";
  if (!strcmp(modifier, "slight right"))return "靠右轉";
  if (!strcmp(modifier, "sharp left"))  return "急左轉";
  if (!strcmp(modifier, "sharp right")) return "急右轉";
  if (!strcmp(modifier, "uturn"))       return "迴轉";
  if (!strcmp(modifier, "straight"))    return "直行";
  return "轉向";
}

static void buildInstruction(const NavStep &s, char *out, size_t n) {
  const char *action = "繼續前進";

  if (!strcmp(s.type, "arrive")) {
    snprintf(out, n, "抵達目的地");
    return;
  }
  if (!strcmp(s.type, "depart")) {
    action = "出發";
  } else if (!strcmp(s.type, "turn")) {
    action = turnText(s.modifier);
  } else if (!strcmp(s.type, "continue") || !strcmp(s.type, "new name")) {
    action = "繼續直行";
  } else if (!strcmp(s.type, "merge")) {
    action = "匯入車道";
  } else if (!strcmp(s.type, "fork")) {
    action = (s.modifier[0] == 'l') ? "靠左行駛" : "靠右行駛";
  } else if (!strcmp(s.type, "roundabout") || !strcmp(s.type, "rotary")) {
    action = "進入環島";
  } else if (!strcmp(s.type, "exit roundabout") || !strcmp(s.type, "exit rotary")) {
    action = "離開環島";
  } else if (!strcmp(s.type, "end of road")) {
    action = "路底轉向";
  } else if (!strcmp(s.type, "use lane")) {
    action = "依車道行駛";
  } else if (!strcmp(s.type, "on ramp")) {
    action = "上匝道";
  } else if (!strcmp(s.type, "off ramp")) {
    action = "下匝道";
  } else if (!strcmp(s.type, "maintain")) {
    action = "保持方向";
  } else if (!strcmp(s.type, "notification")) {
    action = "注意路況";
  } else if (!strcmp(s.type, "roundabout turn")) {
    action = turnText(s.modifier);
  }

  if (s.road[0]) {
    snprintf(out, n, "%s %s", action, s.road);
  } else {
    snprintf(out, n, "%s", action);
  }
}

// ---------------------------------------------------------------------------
// 重新計算目前方位 / 剩餘里程
// ---------------------------------------------------------------------------
static void recompute(bool force = false) {
  if (!s_nav.active || s_nav.stepCount == 0) return;

  uint8_t idx = s_nav.stepIndex;
  if (idx >= s_nav.stepCount) idx = s_nav.stepCount - 1;

  float d = (float)distanceMeters(s_nav.curLat, s_nav.curLon,
                                  s_nav.steps[idx].lat, s_nav.steps[idx].lon);

  // 已經通過這個動作點 -> 前進
  while (idx + 1 < s_nav.stepCount && d < NAV_ARRIVE_RADIUS_M) {
    idx++;
    d = (float)distanceMeters(s_nav.curLat, s_nav.curLon,
                              s_nav.steps[idx].lat, s_nav.steps[idx].lon);
  }

  // 若是「已經超過」下一個點（距離反而變遠）也前進
  if (idx + 1 < s_nav.stepCount) {
    float d2 = (float)distanceMeters(s_nav.curLat, s_nav.curLon,
                                     s_nav.steps[idx + 1].lat, s_nav.steps[idx + 1].lon);
    if (d2 + 8.0f < d) {
      idx++;
      d = d2;
    }
  }

  float bearing = bearingTo(s_nav.curLat, s_nav.curLon,
                            s_nav.steps[idx].lat, s_nav.steps[idx].lon);

  float remain = 0;
  uint32_t remainT = 0;
  for (uint8_t i = idx; i < s_nav.stepCount; i++) {
    remain += s_nav.steps[i].distance;
    remainT += s_nav.steps[i].duration;
  }

  bool arrived = (idx + 1 == s_nav.stepCount) && (d < 35.0f);

  char instr[sizeof(s_nav.instruction)];
  buildInstruction(s_nav.steps[idx], instr, sizeof(instr));

  bool changed = force || idx != s_nav.stepIndex || arrived != s_nav.arrived ||
                 fabsf(bearing - s_nav.bearing) > 1.5f ||
                 fabsf(d - s_nav.distToNext) > 5.0f ||
                 fabsf(remain - s_nav.remainingDist) > 10.0f ||
                 strcmp(instr, s_nav.instruction) != 0;

  s_nav.stepIndex    = idx;
  s_nav.bearing      = bearing;
  s_nav.distToNext   = d;
  s_nav.remainingDist = remain;
  s_nav.remainingTime = remainT;
  s_nav.arrived      = arrived;
  copyUtf8(s_nav.instruction, sizeof(s_nav.instruction), instr);
  copyUtf8(s_nav.road, sizeof(s_nav.road), s_nav.steps[idx].road);
  s_nav.updatedAt    = millis();

  if (changed) s_nav.rev++;
}

// ---------------------------------------------------------------------------
// 對外 API
// ---------------------------------------------------------------------------
void begin() {
  memset(&s_nav, 0, sizeof(s_nav));
  s_geometry = "";
}

void loop() {
  uint32_t now = millis();
  if (now - s_lastTick >= NAV_REFRESH_MS) {
    s_lastTick = now;
    if (s_nav.active) recompute(false);
  }
}

void clear() {
  memset(&s_nav, 0, sizeof(s_nav));
  s_geometry = "";
  s_nav.rev++;
}

void setPosition(double lat, double lon, bool hasHeading, float heading) {
  if (!s_nav.active) return;
  s_nav.curLat = lat;
  s_nav.curLon = lon;
  s_nav.hasHeading = hasHeading;
  s_nav.heading = hasHeading ? heading : 0;
  recompute(false);
}

// OSRM 主機名稱（由 OSRM_BASE 取得，未連線時用來判斷是否為 DNS 問題）
static String osrmHost() {
  String u = OSRM_BASE;
  int p = u.indexOf("://");
  if (p >= 0) u = u.substring(p + 3);
  int slash = u.indexOf('/');
  if (slash >= 0) u = u.substring(0, slash);
  int colon = u.indexOf(':');
  if (colon >= 0) u = u.substring(0, colon);
  return u;
}

bool fetchRoute(double slat, double slon, double dlat, double dlon,
                const char *name, String &err) {
  err = "";

  if (WiFi.status() != WL_CONNECTED) { err = "WiFi 未連線"; return false; }
  if (fabs(slat) > 90 || fabs(dlat) > 90 || fabs(slon) > 180 || fabs(dlon) > 180) {
    err = "座標格式錯誤";
    return false;
  }

  char url[300];
  snprintf(url, sizeof(url),
           "%s/route/v1/%s/%.6f,%.6f;%.6f,%.6f"
           "?overview=full&geometries=polyline&steps=true&alternatives=false&annotations=false",
           OSRM_BASE, OSRM_PROFILE, slon, slat, dlon, dlat);

  Serial.printf("[nav] 查詢路線：%s\n", url);

  WiFiClientSecure tls;
  tls.setInsecure();
  tls.setHandshakeTimeout(12);

  WiFiClient plain;

  HTTPClient http;
  http.setTimeout(15000);
  http.setConnectTimeout(9000);
  http.setReuse(false);

  // 先走 HTTPS；若連線建立失敗（-1）再退回 HTTP，可避開裝置端 TLS
  // 握手失敗（憑證 / 記憶體不足）造成的假性連線錯誤
  int  code      = -1;
  bool connected = false;
  for (int attempt = 0; attempt < 2 && !connected; attempt++) {
    bool   useTls = (attempt == 0);
    String u      = url;
    if (!useTls) u.replace("https://", "http://");

    if (!(useTls ? http.begin(tls, u) : http.begin(plain, u))) {
      http.end();
      continue;
    }
    http.useHTTP10(true);

    code = http.GET();
    if (code == 200) { connected = true; break; }

    Serial.printf("[nav] %s 失敗：HTTP %d（%s）\n", useTls ? "HTTPS" : "HTTP",
                  code, http.errorToString(code).c_str());
    http.end();

    if (code > 0) {  // 伺服器有回應但狀態碼不是 200，重試無益
      err = String("OSRM 回應 HTTP ") + code;
      return false;
    }
  }

  if (!connected) {
    IPAddress ip;
    if (WiFi.hostByName(osrmHost().c_str(), ip)) {
      err = String("無法連上 ") + osrmHost() + "（" + ip.toString() + "）";
    } else {
      err = String("DNS 解析失敗：") + osrmHost();
    }
    err += String("，HTTP ") + code;
    Serial.printf("[nav] OSRM 連線失敗：%s\n", err.c_str());
    return false;
  }

  NAV_JSON_DOC(filter, 4096);
  filter["code"] = true;
  filter["routes"][0]["distance"] = true;
  filter["routes"][0]["duration"] = true;
  filter["routes"][0]["geometry"] = true;
  filter["routes"][0]["legs"][0]["steps"][0]["distance"] = true;
  filter["routes"][0]["legs"][0]["steps"][0]["duration"] = true;
  filter["routes"][0]["legs"][0]["steps"][0]["name"] = true;
  filter["routes"][0]["legs"][0]["steps"][0]["maneuver"]["type"] = true;
  filter["routes"][0]["legs"][0]["steps"][0]["maneuver"]["modifier"] = true;
  filter["routes"][0]["legs"][0]["steps"][0]["maneuver"]["location"] = true;

  NAV_JSON_DOC(doc, 32768);
  DeserializationError e = deserializeJson(doc, http.getStream(),
                                           DeserializationOption::Filter(filter));
  http.end();

  if (e) {
    err = String("解析失敗：") + e.c_str();
    return false;
  }
  if (doc["code"] != "Ok") {
    err = String("OSRM 錯誤：") + (doc["code"] | "unknown");
    return false;
  }

  JsonArray routes = doc["routes"];
  if (routes.size() == 0) { err = "找不到可用路線"; return false; }

  JsonObject route = routes[0];
  JsonArray steps = route["legs"][0]["steps"];
  if (steps.size() == 0) { err = "路線沒有導航步驟"; return false; }

  static NavStatus fresh;
  memset(&fresh, 0, sizeof(fresh));
  fresh.active = true;
  fresh.startLat = slat; fresh.startLon = slon;
  fresh.destLat = dlat;  fresh.destLon = dlon;
  fresh.curLat = slat;   fresh.curLon = slon;
  fresh.routeDistance = route["distance"] | 0.0f;
  fresh.routeDuration = (uint32_t)(route["duration"] | 0u);
  copyUtf8(fresh.destName, sizeof(fresh.destName), name);

  uint8_t n = 0;
  for (JsonObject st : steps) {
    if (n >= NAV_MAX_STEPS) break;
    NavStep &s = fresh.steps[n];
    s.distance = st["distance"] | 0.0f;
    uint32_t dur = st["duration"] | 0u;
    s.duration = (dur > 60000) ? 60000 : (uint16_t)dur;
    copyUtf8(s.road, sizeof(s.road), st["name"] | "");
    copyUtf8(s.type, sizeof(s.type), st["maneuver"]["type"] | "");
    copyUtf8(s.modifier, sizeof(s.modifier), st["maneuver"]["modifier"] | "");
    JsonArray loc = st["maneuver"]["location"];
    if (loc.size() >= 2) {
      s.lon = loc[0] | 0.0;
      s.lat = loc[1] | 0.0;
    }
    n++;
  }
  fresh.stepCount = n;
  fresh.stepIndex = 0;
  fresh.rev = s_nav.rev + 1;

  s_geometry = (const char *)(route["geometry"] | "");

  s_nav = fresh;
  recompute(true);
  s_nav.rev++;

  Serial.printf("[nav] 路線 %u 步，%.0f m，%.0f 秒\n", (unsigned)n,
                s_nav.routeDistance, (double)s_nav.routeDuration);
  return true;
}

const NavStatus &status() { return s_nav; }

void dropGeometry() { s_geometry = ""; }

String toJson(bool withGeometry, bool withSteps) {
  NAV_JSON_DOC(doc, 32768);
  doc["ok"] = true;
  doc["active"] = s_nav.active;
  doc["arrived"] = s_nav.arrived;
  doc["instruction"] = s_nav.instruction;
  doc["road"] = s_nav.road;
  doc["bearing"] = s_nav.bearing;
  doc["distToNext"] = s_nav.distToNext;
  doc["remaining"] = s_nav.remainingDist;
  doc["remainingTime"] = s_nav.remainingTime;
  doc["step"] = (int)s_nav.stepIndex + 1;
  doc["steps"] = (int)s_nav.stepCount;
  doc["routeDistance"] = s_nav.routeDistance;
  doc["routeDuration"] = s_nav.routeDuration;
  doc["destName"] = s_nav.destName;
  doc["hasLocation"] = (s_nav.curLat != 0 || s_nav.curLon != 0);

  doc["start"]["lat"] = s_nav.startLat;
  doc["start"]["lon"] = s_nav.startLon;
  doc["dest"]["lat"] = s_nav.destLat;
  doc["dest"]["lon"] = s_nav.destLon;

  if (withGeometry && s_geometry.length() > 0) {
    doc["geometry"] = s_geometry;
  }

  if (withSteps) {
    JsonArray arr = doc.createNestedArray("list");
    for (uint8_t i = 0; i < s_nav.stepCount; i++) {
      JsonObject o = arr.createNestedObject();
      char instr[64];
      buildInstruction(s_nav.steps[i], instr, sizeof(instr));
      o["instruction"] = instr;
      o["road"] = s_nav.steps[i].road;
      o["distance"] = s_nav.steps[i].distance;
      o["lat"] = s_nav.steps[i].lat;
      o["lon"] = s_nav.steps[i].lon;
    }
  }

  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace Nav
