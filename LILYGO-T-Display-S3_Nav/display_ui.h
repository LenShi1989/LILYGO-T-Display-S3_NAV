#pragma once
// ============================================================================
//  display_ui.h — TFT 畫面、選單、狀態列、導航箭頭
// ============================================================================
#include <Arduino.h>

namespace Ui {

enum class View : uint8_t {
  Menu = 0,
  WifiInfo,
  SysInfo,
  DispInfo,
  About,
  Nav,
};

void begin();
void loop();

// 按鍵事件（由 buttons 呼叫）
void onSelect();
void onConfirm();
void onBack();

void setView(View v);
View view();

void setBrightness(uint8_t percent, bool save = true);
uint8_t brightness();

// 覆蓋式提示
void toast(const char *msg, uint16_t color = 0xFFFF);
void modal(const char *title, const char *line1 = nullptr, const char *line2 = nullptr);
void closeModal();
void otaProgress(const char *title, uint32_t written, uint32_t total);
void otaResult(bool ok, const char *msg);

void redraw();

// 開機畫面
void splash(const char *title, const char *line1 = nullptr,
            const char *line2 = nullptr, int progress = -1);
void bootReady();

}  // namespace Ui
