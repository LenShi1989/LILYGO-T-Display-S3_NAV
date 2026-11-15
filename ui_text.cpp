#include "ui_text.h"
#include "config.h"
#include "cjk_font.h"

namespace Txt {

static TFT_eSPI *s_tft = nullptr;

void begin(TFT_eSPI *tft) { s_tft = tft; }

// ---------------------------------------------------------------------------
// UTF-8 解碼：回傳碼點並前進指標；錯誤時回傳 0xFFFD
// ---------------------------------------------------------------------------
static uint32_t decode(const char *&p) {
  const uint8_t *s = (const uint8_t *)p;
  uint8_t c = s[0];
  if (c < 0x80) {
    p += 1;
    return c;
  }
  auto cont = [](uint8_t b) { return (b & 0xC0) == 0x80; };
  if ((c & 0xE0) == 0xC0 && cont(s[1])) {
    uint32_t cp = ((c & 0x1F) << 6) | (s[1] & 0x3F);
    p += 2;
    return cp;
  }
  if ((c & 0xF0) == 0xE0 && cont(s[1]) && cont(s[2])) {
    uint32_t cp = ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    p += 3;
    return cp;
  }
  if ((c & 0xF8) == 0xF0 && cont(s[1]) && cont(s[2]) && cont(s[3])) {
    uint32_t cp = ((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                  ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    p += 4;
    return cp;
  }
  p += 1;
  return 0xFFFD;
}

static int advance(uint32_t cp) {
  return (cp < 0x7F) ? UI_FONT_ASCII_W : UI_FONT_W;
}

static int cjkIndex(uint32_t cp) {
  int lo = 0, hi = UI_FONT_CJK_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    uint32_t v = ui_cjk_cp[mid];
    if (v == cp) return mid;
    if (v < cp) lo = mid + 1; else hi = mid - 1;
  }
  return -1;
}

static void drawGlyph(int x, int y, uint32_t cp, uint16_t fg) {
  const uint8_t *rows = nullptr;
  int w = UI_FONT_W;

  if (cp >= 0x20 && cp < 0x7F) {
    rows = ui_ascii_bitmap[cp - UI_FONT_ASCII_FIRST];
    w = UI_FONT_ASCII_W;
  } else {
    int idx = cjkIndex(cp);
    if (idx < 0) {
      // 找不到字圖：畫一個空心方塊代替
      s_tft->drawRect(x + 2, y + 2, 12, 12, fg);
      return;
    }
    rows = ui_cjk_bitmap[idx];
  }

  for (int row = 0; row < UI_FONT_H; row++) {
    uint16_t bits = ((uint16_t)rows[row * 2] << 8) | rows[row * 2 + 1];
    int run = -1;
    for (int c = 0; c <= w; c++) {
      bool on = (c < w) && (bits & (0x8000u >> c));
      if (on && run < 0) {
        run = c;
      } else if (!on && run >= 0) {
        s_tft->drawFastHLine(x + run, y + row, c - run, fg);
        run = -1;
      }
    }
  }
}

int width(const char *utf8) {
  int w = 0;
  const char *p = utf8;
  while (*p) {
    uint32_t cp = decode(p);
    w += advance(cp);
  }
  return w;
}

int width(const String &s) { return width(s.c_str()); }

// 不限制寬度
static int drawRaw(int x, int y, const char *utf8, uint16_t fg) {
  int cx = x;
  const char *p = utf8;
  s_tft->startWrite();
  while (*p) {
    uint32_t cp = decode(p);
    if (cp != '\n') drawGlyph(cx, y, cp, fg);
    cx += advance(cp);
  }
  s_tft->endWrite();
  return cx - x;
}

int draw(int x, int y, const char *utf8, uint16_t fg, int maxW) {
  if (!utf8) return 0;
  if (maxW <= 0) return drawRaw(x, y, utf8, fg);

  if (width(utf8) <= maxW) return drawRaw(x, y, utf8, fg);

  // 截斷 + "…"
  int limit = maxW - UI_FONT_W;
  if (limit < 0) limit = 0;
  int cx = x;
  const char *p = utf8;
  s_tft->startWrite();
  while (*p) {
    const char *save = p;
    uint32_t cp = decode(p);
    int adv = advance(cp);
    if (cx - x + adv > limit) { p = save; break; }
    drawGlyph(cx, y, cp, fg);
    cx += adv;
  }
  drawGlyph(cx, y, 0x2026, fg);  // …
  s_tft->endWrite();
  return cx - x + UI_FONT_W;
}

int drawCenter(int cx, int y, const char *utf8, uint16_t fg, int maxW) {
  if (!utf8) return 0;
  int w = width(utf8);
  if (maxW > 0 && w > maxW) w = maxW;
  return draw(cx - w / 2, y, utf8, fg, maxW);
}

int drawRight(int rx, int y, const char *utf8, uint16_t fg, int maxW) {
  if (!utf8) return 0;
  int w = width(utf8);
  if (maxW > 0 && w > maxW) w = maxW;
  return draw(rx - w, y, utf8, fg, maxW);
}

int drawWrapped(int x, int y, int maxW, int lineH, const char *utf8,
                uint16_t fg, int maxLines) {
  if (!utf8) return 0;
  int lines = 0;
  const char *p = utf8;

  while (*p && lines < maxLines) {
    while (*p == ' ') p++;
    if (!*p) break;

    const char *start = p;
    const char *lastSpace = nullptr;
    int w = 0;

    while (*p) {
      const char *save = p;
      uint32_t cp = decode(p);
      int adv = advance(cp);
      if (w + adv > maxW) {
        p = save;
        break;
      }
      if (cp == ' ') lastSpace = save;
      w += adv;
    }

    // 拉丁字盡量不拆開
    if (*p && lastSpace && lastSpace > start) p = lastSpace;

    lines++;
    bool more = (*p != 0) && (lines == maxLines);
    if (more) {
      // 最後一行：截斷並加省略號
      char tmp[128];
      size_t n = (size_t)(p - start);
      if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
      memcpy(tmp, start, n);
      tmp[n] = 0;
      draw(x, y, tmp, fg, maxW - UI_FONT_W);
      draw(x + width(tmp), y, "…", fg);
    } else {
      char tmp[128];
      size_t n = (size_t)(p - start);
      if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
      memcpy(tmp, start, n);
      tmp[n] = 0;
      // 去尾端空白
      while (n > 0 && tmp[n - 1] == ' ') tmp[--n] = 0;
      draw(x, y, tmp, fg);
    }

    y += lineH;
  }
  return lines;
}

bool ellipsize(const char *utf8, int maxW, char *out, size_t outSize) {
  if (!out || outSize == 0) return false;
  out[0] = 0;
  if (!utf8) return false;

  if (width(utf8) <= maxW) {
    strncpy(out, utf8, outSize - 1);
    out[outSize - 1] = 0;
    return false;
  }

  int limit = maxW - UI_FONT_W;
  int w = 0;
  size_t used = 0;
  const char *p = utf8;
  while (*p) {
    const char *save = p;
    uint32_t cp = decode(p);
    int adv = advance(cp);
    if (w + adv > limit) { p = save; break; }
    size_t len = (size_t)(p - save);
    if (used + len + 4 >= outSize) break;
    memcpy(out + used, save, len);
    used += len;
    w += adv;
  }
  memcpy(out + used, "…", 4);  // 含結尾 NUL
  return true;
}

}  // namespace Txt
