#!/usr/bin/env bash
# =============================================================================
#  make_spiffs.sh - 由 LILYGO-T-Display-S3_Nav/data 產生 spiffs.bin
#
#  參數需與 partitions.csv 的 spiffs 分割區一致：
#    block 4096 / page 256 / size 0x960000（9.375 MB）
#
#  用法（於專案根目錄）：
#    bash tools/make_spiffs.sh            # 輸出 ./spiffs.bin
#    bash tools/make_spiffs.sh out.bin    # 指定輸出檔名
#
#  mkspiffs 尋找順序：
#    1. 環境變數 MKSPIFFS 指定的路徑
#    2. PATH 中的 mkspiffs
#    3. Arduino15 內 ESP32 核心附帶的 mkspiffs
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="$ROOT/LILYGO-T-Display-S3_Nav/data"
OUT="${1:-$ROOT/spiffs.bin}"

SPIFFS_SIZE="${SPIFFS_SIZE:-0x960000}"   # 需與 partitions.csv 相符
BLOCK_SIZE="${BLOCK_SIZE:-4096}"
PAGE_SIZE="${PAGE_SIZE:-256}"

find_mkspiffs() {
  if [[ -n "${MKSPIFFS:-}" && -x "${MKSPIFFS}" ]]; then
    echo "$MKSPIFFS"
    return 0
  fi
  if command -v mkspiffs >/dev/null 2>&1; then
    command -v mkspiffs
    return 0
  fi
  local base found
  for base in "${ARDUINO15:-}" "$HOME/AppData/Local/Arduino15" \
              "$LOCALAPPDATA/Arduino15" "$HOME/.arduino15"; do
    [[ -n "$base" && -d "$base/packages/esp32/tools/mkspiffs" ]] || continue
    found=$(find "$base/packages/esp32/tools/mkspiffs" -type f \
              \( -name mkspiffs -o -name mkspiffs.exe \) 2>/dev/null | sort -V | tail -1)
    if [[ -n "$found" ]]; then
      echo "$found"
      return 0
    fi
  done
  return 1
}

if ! MKSPIFFS_BIN="$(find_mkspiffs)"; then
  echo "找不到 mkspiffs。請先安裝 ESP32 開發板核心，或設定 MKSPIFFS=/path/to/mkspiffs。" >&2
  exit 1
fi
if [[ ! -d "$DATA_DIR" ]]; then
  echo "找不到資料夾：$DATA_DIR" >&2
  exit 1
fi

echo "mkspiffs : $MKSPIFFS_BIN"
echo "來源     : $DATA_DIR"
echo "映像大小 : $SPIFFS_SIZE（block $BLOCK_SIZE / page $PAGE_SIZE）"
echo "輸出     : $OUT"

"$MKSPIFFS_BIN" -c "$DATA_DIR" -p "$PAGE_SIZE" -b "$BLOCK_SIZE" -s "$SPIFFS_SIZE" "$OUT"

echo "完成：$OUT（$(wc -c <"$OUT" | tr -d ' ') bytes）"
echo "可由網頁「OTA 更新 > 檔案系統」上傳。"
