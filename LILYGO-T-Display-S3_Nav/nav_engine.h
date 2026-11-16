#pragma once
// ============================================================================
//  nav_engine.h — OSRM 路線查詢 / 方位角 / 轉彎提示
// ============================================================================
#include <Arduino.h>
#include "config.h"

struct NavStep {
  float    lat = 0, lon = 0;
  float    distance = 0;      // 此段長度（到下一路口）
  uint16_t duration = 0;      // 秒
  char     type[14] = {0};
  char     modifier[14] = {0};
  char     road[48] = {0};
};

struct NavStatus {
  bool     active     = false;
  bool     arrived    = false;
  bool     fetchError = false;
  bool     hasHeading = false;
  float    heading    = 0;

  double   curLat = 0, curLon = 0;
  double   startLat = 0, startLon = 0;
  double   destLat = 0, destLon = 0;
  char     destName[48] = {0};

  float    routeDistance = 0;
  uint32_t routeDuration = 0;

  uint8_t  stepCount = 0;
  uint8_t  stepIndex = 0;

  float    bearing = 0;          // 目前位置 -> 下一動作點（正北 0°）
  float    distToNext = 0;       // 直線距離（公尺）
  float    remainingDist = 0;    // 剩餘里程（公尺）
  uint32_t remainingTime = 0;    // 剩餘時間（秒）

  char     instruction[64] = {0};
  char     road[48] = {0};

  uint32_t updatedAt = 0;
  uint32_t rev = 0;              // 畫面重繪用版本號

  NavStep  steps[NAV_MAX_STEPS];
};

namespace Nav {

void begin();
void loop();

// 向 OSRM 查詢路線（阻塞）。成功回傳 true，geometry 可用於前端畫線。
bool fetchRoute(double slat, double slon, double dlat, double dlon,
                const char *name, String &err);

void clear();
void setPosition(double lat, double lon, bool hasHeading, float heading);

const NavStatus &status();
String toJson(bool withGeometry, bool withSteps = false);
void dropGeometry();

// 路線幾何（已抽樣，供裝置端 HUD 繪製）
uint16_t routePointCount();
void     routePoint(uint16_t i, float &lat, float &lon);

}  // namespace Nav
