#!/usr/bin/env bash
# =============================================================================
#  make_spiffs.sh - 由 LILYGO-T-Display-S3_Nav/data 產生並驗證 spiffs.bin
#
#  分割區位移與大小直接讀取 partitions.csv，確保與韌體使用同一份分割表：
#    spiffs, data, spiffs, 0x610000, 0x960000
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

BLOCK_SIZE="${BLOCK_SIZE:-4096}"
PAGE_SIZE="${PAGE_SIZE:-256}"

# --- 從 partitions.csv 讀出 spiffs 的 offset / size -------------------------
CSV="${PARTITION_CSV:-}"
if [[ -z "$CSV" ]]; then
  for candidate in "$ROOT/LILYGO-T-Display-S3_Nav/partitions.csv" "$ROOT/partitions.csv"; do
    if [[ -f "$candidate" ]]; then CSV="$candidate"; break; fi
  done
fi
if [[ -z "$CSV" || ! -f "$CSV" ]]; then
  echo "找不到 partitions.csv，無法決定 spiffs 分割區大小。" >&2
  exit 1
fi

mapfile -t _row < <(awk -F',' '
  { sub(/#.*/, "") }
  {
    for (i = 1; i <= 5; i++) { gsub(/^[ \t]+/, "", $i); gsub(/[ \t]+$/, "", $i) }
    if ($1 == "" || $1 == "Name" || $2 == "Type") next
    if ($1 == "spiffs" || $3 == "spiffs") { print $4; print $5; exit }
  }' "$CSV")
CSV_OFFSET="${_row[0]:-}"
CSV_SIZE="${_row[1]:-}"
if [[ -z "$CSV_SIZE" ]]; then
  echo "partitions.csv 內找不到 spiffs 分割區：$CSV" >&2
  exit 1
fi

SPIFFS_OFFSET="${SPIFFS_OFFSET:-$CSV_OFFSET}"
SPIFFS_SIZE="${SPIFFS_SIZE:-$CSV_SIZE}"

# --- 尋找 mkspiffs ----------------------------------------------------------
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
echo "分割區   : spiffs，偏移 $SPIFFS_OFFSET，大小 $SPIFFS_SIZE（block $BLOCK_SIZE / page $PAGE_SIZE）"
echo "分割表   : $CSV"
echo "輸出     : $OUT"

"$MKSPIFFS_BIN" -c "$DATA_DIR" -p "$PAGE_SIZE" -b "$BLOCK_SIZE" -s "$SPIFFS_SIZE" "$OUT"

# --- 驗證：大小、SPIFFS magic、檔案清單 ------------------------------------
EXPECTED=$((SPIFFS_SIZE))
ACTUAL=$(wc -c <"$OUT" | tr -d ' ')
fail=0

if [[ "$ACTUAL" -ne "$EXPECTED" ]]; then
  echo "✗ 映像大小 $ACTUAL bytes，與分割區 $EXPECTED bytes 不符" >&2
  fail=1
fi

if [[ "$(od -An -tu1 -N1 "$OUT" | tr -d ' ')" == "233" ]]; then
  echo "✗ 輸出檔開頭是 0xE9（韌體映像），不是 SPIFFS 映像" >&2
  fail=1
fi

BLOCKS=$((EXPECTED / BLOCK_SIZE))
for bix in 0 1 2 $((BLOCKS - 1)); do
  off=$((bix * BLOCK_SIZE + PAGE_SIZE - 4))
  got=$(od -An -tu2 -j "$off" -N2 "$OUT" | tr -d ' ')
  want=$(( (0x20140529 ^ PAGE_SIZE ^ (BLOCKS - bix)) & 0xFFFF ))
  if [[ "$got" != "$want" ]]; then
    printf '✗ block %d：SPIFFS magic %d != %d（page/block/分割區大小不符？）\n' \
      "$bix" "$got" "$want" >&2
    fail=1
  fi
done

img_list=$("$MKSPIFFS_BIN" -l -s "$SPIFFS_SIZE" -p "$PAGE_SIZE" -b "$BLOCK_SIZE" "$OUT" | tr -d "\r" | sort)
src_list=$(cd "$DATA_DIR" && find . -type f | while IFS= read -r f; do
  printf '%s\t%s\n' "$(wc -c <"$f" | tr -d ' ')" "${f#.}"
done | sort)
if [[ "$img_list" != "$src_list" ]]; then
  echo "✗ 映像內的檔案清單與 data/ 不一致：" >&2
  diff <(echo "$src_list") <(echo "$img_list") >&2 || true
  fail=1
fi

if [[ "$fail" -ne 0 ]]; then
  echo "驗證失敗，請勿燒錄這個 spiffs.bin。" >&2
  exit 1
fi

echo "完成並驗證通過：$OUT（$ACTUAL bytes）"
echo "USB 燒錄（整份映像，偏移 $SPIFFS_OFFSET）："
echo "  Windows：tools/flash_spiffs.bat"
echo "  macOS / Linux：esptool --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write-flash $SPIFFS_OFFSET \"$OUT\""
echo "也可由網頁「OTA 更新 > 檔案系統」上傳。"
