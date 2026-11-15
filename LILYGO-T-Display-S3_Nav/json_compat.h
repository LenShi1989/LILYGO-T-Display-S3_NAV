#pragma once
// ============================================================================
//  json_compat.h — 同時支援 ArduinoJson 6.x / 7.x 的宣告巨集
// ============================================================================
#include <ArduinoJson.h>

#if ARDUINOJSON_VERSION_MAJOR >= 7
  #define NAV_JSON_DOC(name, capacity) JsonDocument name
#else
  #define NAV_JSON_DOC(name, capacity) DynamicJsonDocument name(capacity)
#endif
