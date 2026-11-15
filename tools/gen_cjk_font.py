#!/usr/bin/env python3
# =============================================================================
#  gen_cjk_font.py — 由 GNU Unifont 16x16 點陣字產生 cjk_font.h
#
#  用法：
#    python tools/gen_cjk_font.py --download          # 自動下載 unifont.hex.gz
#    python tools/gen_cjk_font.py --hex path\to\unifont.hex
#    python tools/gen_cjk_font.py --pkl %TEMP%\unifont.pkl
#
#  字集：ASCII (0x20-0x7E) + Big5 全部 + GB2312 全部 + 原始碼中用到的字元
#  輸出：LILYGO-T-Display-S3_Nav/cjk_font.h（自動尋找含 .ino 的資料夾）
#
#  字型來源：GNU Unifont（https://unifoundry.com/unifont/）
#  授權：GPL-2.0-or-later 搭配字型嵌入例外條款，或 SIL OFL 1.1
# =============================================================================
import argparse
import gzip
import os
import pickle
import sys
import tempfile
import urllib.request

URL = "https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/unifont-16.0.04.hex.gz"

TEMP = os.environ.get("TEMP", tempfile.gettempdir())
DEFAULT_HEX = os.path.join(TEMP, "unifont.hex.gz")
DEFAULT_PKL = os.path.join(TEMP, "unifont.pkl")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_sketch_dir():
    """回傳包含 .ino 的 sketch 資料夾；找不到時回退 ROOT"""
    try:
        names = sorted(os.listdir(ROOT))
    except OSError:
        return ROOT
    for name in names:
        path = os.path.join(ROOT, name)
        if not os.path.isdir(path):
            continue
        try:
            if any(f.endswith(".ino") for f in os.listdir(path)):
                return path
        except OSError:
            continue
    return ROOT


SKETCH = find_sketch_dir()

ASCII_FIRST = 0x20
ASCII_LAST = 0x7E

# UI / 導航一定會用到、但不見得落在 Big5 或 GB2312 的字元
EXTRA = (
    "…‥→←↑↓↔·•：，。！？、；（）【】「」『』〈〉《》％＋－×÷＝"
    "～｜／＼　℃°±©®™←↑→↓⇧⇩✓✗✔✘"
)


def load_glyphs(args):
    """回傳 dict: codepoint -> 十六進位字串"""
    if args.pkl and os.path.exists(args.pkl):
        print(f"[font] 讀取 {args.pkl}")
        with open(args.pkl, "rb") as f:
            return pickle.load(f)

    path = args.hex or DEFAULT_HEX
    if not os.path.exists(path):
        if not args.download:
            sys.exit(f"[font] 找不到 {path}，請加 --download 或 --hex")
        print(f"[font] 下載 {URL}")
        urllib.request.urlretrieve(URL, path)

    print(f"[font] 讀取 {path}")
    opener = gzip.open if path.endswith(".gz") else open
    glyphs = {}
    with opener(path, "rt", encoding="ascii", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            cp_s, hex_s = line.split(":", 1)
            glyphs[int(cp_s, 16)] = hex_s.lower()
    return glyphs


def big5_chars():
    for hi in range(0xA1, 0xFA):
        for lo in list(range(0x40, 0x7F)) + list(range(0xA1, 0xFF)):
            try:
                ch = bytes([hi, lo]).decode("big5")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                yield ch


def gb_chars():
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1:
                yield ch


def source_chars():
    """遞迴掃描 sketch 資料夾內的 C/C++ 檔案，收集所有非 ASCII 字元"""
    chars = set()
    for root, dirs, files in os.walk(SKETCH):
        dirs[:] = [d for d in dirs if d not in ("build", "__pycache__", ".git")]
        for name in files:
            if not name.endswith((".ino", ".h", ".cpp", ".c")):
                continue
            path = os.path.join(root, name)
            try:
                text = open(path, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            for ch in text:
                if ord(ch) > 0x7F:
                    chars.add(ch)
    return chars


def rows_of(hex_s):
    """把 Unifont hex 轉成 [(row_bits, width), ...]"""
    bits = len(hex_s) * 4
    if bits % 16 or bits < 128 or bits > 512:
        return None
    width = bits // 16
    if width not in (8, 16, 24, 32):
        return None
    rows = []
    step = width // 4
    for y in range(16):
        rows.append(int(hex_s[y * step:(y + 1) * step], 16))
    return rows, width


def pack(rows, width):
    """輸出 32 bytes：每列 16 bits（<16 寬的字型靠左對齊高位）"""
    out = bytearray()
    for value in rows:
        if width == 8:
            out += bytes([value & 0xFF, 0])
        elif width == 16:
            out += bytes([(value >> 8) & 0xFF, value & 0xFF])
        elif width == 24:
            v = value >> 8
            out += bytes([(v >> 8) & 0xFF, v & 0xFF])
        else:
            v = value >> 16
            out += bytes([(v >> 8) & 0xFF, v & 0xFF])
    return bytes(out)


def build_set(glyphs, verbose=True):
    cps = set()
    missing = set()

    for cp in range(ASCII_FIRST, ASCII_LAST + 1):
        if cp in glyphs:
            cps.add(cp)

    candidates = set()
    for ch in big5_chars():
        candidates.add(ch)
    for ch in gb_chars():
        candidates.add(ch)
    for ch in EXTRA:
        candidates.add(ch)
    scanned = source_chars()
    candidates |= scanned

    for ch in candidates:
        cp = ord(ch)
        if cp < 0x80:
            continue
        if cp in glyphs:
            cps.add(cp)
        else:
            missing.add(ch)

    if verbose:
        print(f"[font] 原始碼字元 {len(scanned)}，候選 {len(candidates)}，"
              f"實際字圖 {len(cps)}，缺字 {len(missing)}")
        if missing:
            print("[font] 缺字：" + "".join(sorted(missing)))
    return cps, missing


def emit(cps, glyphs, out_path):
    cps = sorted(cps)
    ascii_list = [cp for cp in cps if cp <= ASCII_LAST]
    cjk_list = [cp for cp in cps if cp > ASCII_LAST]

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEADER)
        f.write(f"#define UI_FONT_W           16\n")
        f.write(f"#define UI_FONT_H           16\n")
        f.write(f"#define UI_FONT_ASCII_W      8\n")
        f.write(f"#define UI_FONT_ASCII_FIRST  0x{ASCII_FIRST:02X}\n")
        f.write(f"#define UI_FONT_CJK_COUNT    {len(cjk_list)}\n\n")

        f.write("// ASCII 0x20-0x7E，每字 16 列 x 2 bytes（高位對齊，右 8 bit 為 0）\n")
        f.write("static const uint8_t ui_ascii_bitmap[95][32] = {\n")
        for cp in range(ASCII_FIRST, ASCII_LAST + 1):
            data = pack(*rows_of(glyphs[cp]))
            body = ",".join(f"0x{b:02X}" for b in data)
            f.write(f"  {{ {body} }},  // '{chr(cp)}' 0x{cp:02X}\n")
        f.write("};\n\n")

        f.write("// 碼點表（遞增排序，供二分搜尋）\n")
        f.write("static const uint32_t ui_cjk_cp[] = {\n")
        for i in range(0, len(cjk_list), 8):
            chunk = ", ".join(f"0x{cp:04X}" for cp in cjk_list[i:i + 8])
            f.write(f"  {chunk},\n")
        f.write("};\n\n")

        f.write("// 16x16 點陣，每字 16 列 x 2 bytes（big-endian）\n")
        f.write("static const uint8_t ui_cjk_bitmap[][32] = {\n")
        for i, cp in enumerate(cjk_list):
            data = pack(*rows_of(glyphs[cp]))
            body = ",".join(f"0x{b:02X}" for b in data)
            f.write(f"  {{ {body} }},  // U+{cp:04X}\n")
        f.write("};\n")

    size = os.path.getsize(out_path)
    print(f"[font] 已寫出 {out_path}（{len(cjk_list)} 字，{size / 1048576.0:.2f} MB）")


HEADER = """// =============================================================================
//  cjk_font.h — 16px 點陣字型（自動產生，請勿手動編輯）
//
//  產生指令：python tools/gen_cjk_font.py --download
//  字型來源：GNU Unifont 16.0.04（https://unifoundry.com/unifont/）
//
//  GNU Unifont 授權（二選一）：
//    1. GNU GPL v2 或更新版本，附「字型嵌入例外條款」(font embedding exception)
//    2. SIL Open Font License 1.1
//  本檔案為點陣化後的衍生資料，隨本專案一併散布；詳見 README。
// =============================================================================
#pragma once
#include <stdint.h>

"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--hex", help="unifont .hex 或 .hex.gz 路徑")
    ap.add_argument("--pkl", default=DEFAULT_PKL, help="已快取的 pickle")
    ap.add_argument("--download", action="store_true", help="找不到時自動下載")
    ap.add_argument("--out", default=os.path.join(SKETCH, "cjk_font.h"))
    ap.add_argument("--no-source", action="store_true", help="不掃描原始碼")
    args = ap.parse_args()

    glyphs = load_glyphs(args)
    cps, missing = build_set(glyphs)
    if missing:
        print("[font] 警告：上述字元沒有字圖，畫面會顯示空心方塊")
    emit(cps, glyphs, args.out)


if __name__ == "__main__":
    main()
