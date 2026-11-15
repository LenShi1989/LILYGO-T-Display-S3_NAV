#include "app_state.h"
#include <esp_system.h>

AppState g_app;

String fmtBytes(uint32_t bytes) {
  char buf[24];
  if (bytes >= 1024UL * 1024UL) {
    snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.0);
  } else if (bytes >= 1024UL) {
    snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%u B", (unsigned)bytes);
  }
  return String(buf);
}

String fmtDuration(uint32_t seconds) {
  char buf[32];
  uint32_t h = seconds / 3600;
  uint32_t m = (seconds % 3600) / 60;
  if (h > 0) {
    snprintf(buf, sizeof(buf), "%u 時 %02u 分", (unsigned)h, (unsigned)m);
  } else if (m > 0) {
    snprintf(buf, sizeof(buf), "%u 分", (unsigned)m);
  } else {
    snprintf(buf, sizeof(buf), "%u 秒", (unsigned)seconds);
  }
  return String(buf);
}

String fmtClock(uint32_t seconds) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)(seconds / 3600),
           (unsigned)((seconds % 3600) / 60), (unsigned)(seconds % 60));
  return String(buf);
}

String fmtUptime(uint32_t seconds) {
  char buf[24];
  uint32_t d = seconds / 86400;
  uint32_t h = (seconds % 86400) / 3600;
  uint32_t m = (seconds % 3600) / 60;
  if (d > 0) {
    snprintf(buf, sizeof(buf), "%ud %02u:%02u", (unsigned)d, (unsigned)h, (unsigned)m);
  } else {
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned)h, (unsigned)m, (unsigned)(seconds % 60));
  }
  return String(buf);
}

const char* resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXT";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO:     return "SDIO";
    case ESP_RST_USB:      return "USB";
    default:               return "UNKNOWN";
  }
}

const char* wifiStateText() {
  if (g_app.wifi.staConnected) return "已連線";
  if (g_app.wifi.apActive)     return "AP 模式";
  return "未連線";
}
