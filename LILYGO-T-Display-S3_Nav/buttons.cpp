#include "buttons.h"
#include "config.h"

namespace Buttons {

struct Key {
  uint8_t  pin;
  bool     raw;           // 原始電位（true = 放開）
  bool     down;          // 去彈跳後（true = 按下）
  uint32_t rawChanged;
  uint32_t downAt;
  uint32_t nextRepeat;
  bool     longFired;
};

static Key s_sel = {PIN_BTN_SELECT, true, false, 0, 0, 0, false};
static Key s_con = {PIN_BTN_CONFIRM, true, false, 0, 0, 0, false};

static const uint32_t DEBOUNCE_MS = 25;
static const uint32_t LONG_MS     = 800;
static const uint32_t REPEAT_MS   = 450;
static const uint32_t REPEAT_RATE = 140;

void begin() {
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  pinMode(PIN_BTN_CONFIRM, INPUT_PULLUP);

  s_sel.raw = digitalRead(s_sel.pin);
  s_con.raw = digitalRead(s_con.pin);
  s_sel.down = !s_sel.raw;
  s_con.down = !s_con.raw;
  s_sel.rawChanged = s_con.rawChanged = millis();
  s_sel.downAt = s_con.downAt = millis();
}

static void update(Key &k, uint32_t now) {
  bool raw = digitalRead(k.pin);
  if (raw != k.raw) {
    k.raw = raw;
    k.rawChanged = now;
    return;
  }
  if (now - k.rawChanged < DEBOUNCE_MS) return;

  bool down = !raw;
  if (down == k.down) return;

  k.down = down;
  if (down) {
    k.downAt = now;
    k.nextRepeat = now + REPEAT_MS;
    k.longFired = false;
  }
}

Event poll() {
  uint32_t now = millis();
  update(s_sel, now);
  update(s_con, now);

  static bool selWasDown = false;
  static bool conWasDown = false;

  Event ev = Event::None;

  // 確認鍵長按（到達門檻立即觸發，且放開時不再算短按）
  if (s_con.down && !s_con.longFired && now - s_con.downAt >= LONG_MS) {
    s_con.longFired = true;
    ev = Event::ConfirmLong;
  } else if (!s_con.down && conWasDown && !s_con.longFired) {
    ev = Event::Confirm;
  }

  // 選擇鍵：按下瞬間 + 長按連發
  if (s_sel.down) {
    if (!selWasDown) {
      ev = Event::Select;
    } else if (now >= s_sel.nextRepeat) {
      s_sel.nextRepeat = now + REPEAT_RATE;
      ev = Event::Select;
    }
  }

  selWasDown = s_sel.down;
  conWasDown = s_con.down;
  return ev;
}

}  // namespace Buttons
