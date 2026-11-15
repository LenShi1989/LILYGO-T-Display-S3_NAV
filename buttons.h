#pragma once
// ============================================================================
//  buttons.h — GPIO0（選擇）/ GPIO14（確認）按鍵
// ============================================================================
#include <Arduino.h>

namespace Buttons {

enum class Event : uint8_t {
  None,
  Select,        // 選擇鍵：短按（長按會自動連發）
  Confirm,       // 確認鍵：短按
  ConfirmLong,   // 確認鍵：長按 >= 800ms（返回）
};

void begin();
Event poll();

}  // namespace Buttons
