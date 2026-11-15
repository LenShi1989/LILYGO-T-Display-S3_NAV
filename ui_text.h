#pragma once
// ============================================================================
//  ui_text.h — 16px 點陣字型繪製（ASCII 半寬 + CJK 全寬）
// ============================================================================
#include <Arduino.h>
#include <TFT_eSPI.h>

namespace Txt {

void begin(TFT_eSPI *tft);

int  width(const char *utf8);
int  width(const String &s);

// 由左上角開始畫；maxW <= 0 表示不限制。回傳實際寬度。
int  draw(int x, int y, const char *utf8, uint16_t fg, int maxW = 0);
int  drawCenter(int cx, int y, const char *utf8, uint16_t fg, int maxW = 0);
int  drawRight(int rx, int y, const char *utf8, uint16_t fg, int maxW = 0);

// 自動換行，回傳使用行數
int  drawWrapped(int x, int y, int maxW, int lineH, const char *utf8,
                 uint16_t fg, int maxLines);

// 依寬度截斷（UTF-8 安全），不足時補 "…"
bool ellipsize(const char *utf8, int maxW, char *out, size_t outSize);

}  // namespace Txt
