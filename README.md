# LILYGO T-Display-S3 NAV

ESP32-S3 導航機：1.9 吋 TFT 捲動選單、電池 / WiFi 狀態列、網頁控制台、OSRM 路線導航與 OTA 更新。

- 開發板：**LILYGO T-Display-S3**（ESP32-S3，16MB Flash / 8MB PSRAM，ST7789 170×320 8-bit 並列）
- 操作：**GPIO0**（板載 BOOT）＝選擇、**GPIO14**（外接按鍵對 GND）＝確認，長按 0.8 秒返回
- 網頁：`http://nav.local`，AP 模式 `http://192.168.4.1`（SSID `ESP32-S3_NAV-<MAC後4碼>`、預設密碼 `12345678`）

---

## 功能

| 區域 | 內容 |
| --- | --- |
| 狀態列 | 左上：WiFi SSID / 狀態；右上：WiFi 訊號圖示、電量百分比、電池圖示（USB 供電時顯示 USB） |
| 主選單 | 系統狀態、MAP導航、關於本機（清單可捲動，最多同時顯示 5 列） |
| 系統狀態 | WiFi 連線資訊、系統狀態（兩頁）、顯示設定、關於本機 |
| MAP導航 | 北向朝上的方位箭頭轉盤、轉彎指示、距下一動作點距離、剩餘里程 / 時間、目的地、步驟進度 |
| 網頁 | 系統狀態（含 SPIFFS 目錄與容量）、WiFi 掃描 / 連線 / 清除、Leaflet 地圖 + OSRM 路線、OTA 更新 |

裝置本身沒有 GPS：請以手機 / 電腦開啟網頁地圖，將瀏覽器定位（HTTPS 或 localhost 才可用）
或「模擬位置」送到裝置；裝置再依 OSRM 路線計算方位角與剩餘里程。

---

## 開發環境

| 項目 | 版本 |
| --- | --- |
| Arduino IDE | **1.8.19**（2.x 亦可） |
| ESP32 核心 | **2.0.17** 或 **3.3.12**（本專案以 3.3.12 編譯測試） |
| TFT_eSPI | **2.5.43** |
| ArduinoJson | **7.4.2**（6.x 亦相容） |

開發板設定（Tools）：

- Board：`LILYGO T-Display-S3`
- FQBN：`esp32:esp32:lilygo_t_display_s3:CDCOnBoot=cdc,USBMode=hwcdc`
- Partition Scheme：使用專案內的 `partitions.csv`（見下節）
- PSRAM：Enabled（若有此選項）

### 函式庫安裝

1. **TFT_eSPI 2.5.43**：`Tools > Manage Libraries` 搜尋安裝。
   sketch 內的 `tft_setup.h` 會由 TFT_eSPI 2.5.31+ 自動載入，**不需要修改函式庫的 `User_Setup.h`**。
2. **ArduinoJson 7.4.2**（或 6.x）：`json_compat.h` 會自動處理版本差異。
3. 其餘（WiFi、WebServer、Update、SPIFFS、Preferences、DNSServer、ESPmDNS、HTTPClient、
   WiFiClientSecure）皆為 ESP32 核心內建。

---

## 目錄結構

```
LILYGO-T-Display-S3_NAV/           專案根目錄（Git repository）
├── LILYGO-T-Display-S3_Nav/       Arduino sketch 資料夾
│   ├── LILYGO-T-Display-S3_Nav.ino  主程式（資料夾與 .ino 必須同名）
│   ├── config.h                   腳位、顏色、AP / mDNS 設定
│   ├── tft_setup.h                TFT_eSPI 設定（ST7789 / 8-bit 並列）
│   ├── display_ui.h / .cpp        狀態列、選單、資訊頁、導航轉盤、提示 / OTA 畫面
│   ├── ui_text.h / .cpp           16px 點陣字繪製（ASCII 半寬 + CJK 全寬）
│   ├── cjk_font.h                 自動產生的字型點陣（請勿手改）
│   ├── app_state.h / .cpp         共用狀態、格式化工具
│   ├── buttons.h / .cpp           GPIO0 / GPIO14 去彈跳、長按、連發
│   ├── wifi_manager.h / .cpp      STA / AP、mDNS、NVS 帳密
│   ├── nav_engine.h / .cpp        OSRM 查詢、方位角、剩餘里程、轉彎文字
│   ├── web_server.h / .cpp        REST API、靜態網頁、OTA
│   ├── json_compat.h              ArduinoJson 6 / 7 相容
│   └── data/                      網頁前端（打包成 SPIFFS 映像）
├── docs/                          腳位圖
├── tools/gen_cjk_font.py          字型產生器
└── README.md
```

> **注意**：Arduino IDE 的「ESP32 Sketch Data Upload」與自動套用 `partitions.csv`
> 都只認 **sketch 資料夾內**的 `data/` 與 `partitions.csv`。
> 本專案的 `data/` 已直接放在 sketch 資料夾內並納入版控；`partitions.csv` 則保留在
> 根目錄，另於 sketch 資料夾放置一份供自動套用（該副本不進版控）。

---

## 分割表

本專案在專案根目錄附帶 `partitions.csv`，內容如下：

| 分割區 | 大小 |
| --- | --- |
| app0 / app1 | 3 MB × 2（支援 OTA） |
| spiffs | 9.375 MB |
| coredump | 64 KB |

ESP32 核心 2.0.5+ 只會自動採用 **sketch 資料夾內**的 `partitions.csv`，
請先複製一份再開啟 Arduino IDE：

```powershell
copy partitions.csv LILYGO-T-Display-S3_Nav\partitions.csv
```

若 IDE 仍未採用，可改用 `Tools > Partition Scheme` 中名稱相近的
`3MB APP / 9MB SPIFFS` 選項。

> OTA 更新失敗最常見的原因就是分割表不符。

---

## 編譯與上傳

1. 以 Arduino IDE 開啟 `LILYGO-T-Display-S3_Nav/LILYGO-T-Display-S3_Nav.ino`。
2. 選擇開發板與序列埠，按「上傳」。
3. 首次燒錄後，SPIFFS 尚無網頁檔案，需上傳 `LILYGO-T-Display-S3_Nav/data/`：
   - 安裝 **ESP32 Sketch Data Upload** 外掛（arduino-esp32fs-plugin），
     再於 Arduino IDE 執行 `Tools > ESP32 Sketch Data Upload`。
   - 或使用 `mkspiffs` / `arduino-cli` 自行產生映像後，由網頁 OTA 上傳（見「OTA 更新」）。
4. 重新啟動後：
   - 若尚未設定 WiFi：裝置開啟 AP `ESP32-S3_NAV-<MAC後4碼>`（例：`ESP32-S3_NAV-A1B2`），預設密碼 `12345678`。
   - 連上 AP 後開啟 `http://192.168.4.1` 或 `http://nav.local` 設定家中 WiFi。
   - 連線成功後，裝置會以 `nav.local` 提供網頁（也可使用取得的 IP）。

以 arduino-cli 編譯（於專案根目錄執行）：

```powershell
arduino-cli compile -b "esp32:esp32:lilygo_t_display_s3:CDCOnBoot=cdc,USBMode=hwcdc" LILYGO-T-Display-S3_Nav
```

---

## 操作

| 按鍵 | 短按 | 長按（0.8 秒） |
| --- | --- | --- |
| GPIO0（BOOT） | 選擇 / 下一項 / 換頁 / 亮度 +10% | 自動連發 |
| GPIO14（外接） | 確認 / 進入 / 返回資訊頁 | 返回上一層 |

GPIO14 接線：一端接 GPIO14，另一端接 GND（內部上拉，按下為 LOW）。

選單結構：

```
主選單
├── 系統狀態
│   ├── WiFi 連線資訊
│   ├── 系統狀態
│   ├── 顯示設定
│   └── 返回主選單
├── MAP導航
└── 關於本機
```

亮度設定會存入 NVS，重新開機後保留。

---

## 網頁控制台

| 頁面 | 功能 |
| --- | --- |
| 系統狀態 | WiFi / 電池 / 記憶體 / 韌體資訊、SPIFFS 使用量與檔案清單（每 4 秒更新） |
| WiFi 設定 | 掃描 SSID、訊號強度、手動輸入 SSID / 密碼、連線、清除設定 |
| MAP 導航 | Leaflet + OSM 圖磚、Nominatim 搜尋、地圖點選起終點、OSRM 路線、轉彎清單、即時導航狀態 |
| OTA 更新 | 上傳 `firmware.bin`、`spiffs.bin`，顯示進度，完成後自動重啟 |

> 地圖圖磚與 Nominatim 需要網際網路。裝置處於 AP 模式時，手機 / 電腦通常無法上網，
> 此時地圖會只剩灰色底圖；可先連上家中 WiFi，再以 `nav.local` 開啟網頁。

### REST API

| 方法 | 路徑 | 說明 |
| --- | --- | --- |
| GET | `/api/status` | 系統、WiFi、電池、記憶體、SPIFFS、導航摘要 |
| GET | `/api/spiffs` | SPIFFS 容量與檔案清單 |
| GET | `/api/wifi/scan` | 掃描附近 SSID（阻塞數秒） |
| POST | `/api/wifi` | `{"ssid":"...","pass":"..."}` 連線並儲存 |
| DELETE | `/api/wifi` | 清除 WiFi 設定（之後自動重啟） |
| POST | `/api/nav/route` | `{"start":{"lat":..,"lon":..},"dest":{...},"name":"..."}` 查詢 OSRM |
| POST | `/api/nav/position` | `{"lat":..,"lon":..,"heading":..}` 更新目前位置 |
| GET | `/api/nav` | 目前路線（含 geometry 與步驟） |
| DELETE | `/api/nav` | 清除路線 |
| POST | `/api/ota/firmware` | multipart 上傳韌體 |
| POST | `/api/ota/spiffs` | multipart 上傳 SPIFFS 映像 |
| POST | `/api/system/reboot` | 重新啟動 |

OSRM 使用公開示範伺服器 `https://router.project-osrm.org`（僅供測試，
長期使用請自架或改用商業服務）。裝置以 `WiFiClientSecure` 連線，憑證採 `setInsecure()`。

---

## OTA 更新

### 韌體 `firmware.bin`

Arduino IDE：`Sketch > Export Compiled Binary`，產生的 `.bin` 位於 sketch 資料夾
（`LILYGO-T-Display-S3_Nav/`）。
arduino-cli：加上 `--output-dir` 指定輸出資料夾後取用 `*.ino.bin`。

### 檔案系統 `spiffs.bin`

使用 `mkspiffs`（ESP32 版）於**專案根目錄**產生，映像大小需與分割表一致
（`0x960000` = 9.375 MB）：

```bash
mkspiffs -c LILYGO-T-Display-S3_Nav/data -p 256 -b 4096 -s 0x960000 spiffs.bin
```

上傳完成後裝置會自動重新啟動。

> 若使用 Arduino IDE 內建的「ESP32 Sketch Data Upload」，`data/` 必須位於
> `LILYGO-T-Display-S3_Nav/` 內，本專案已如此配置。

---

## 字型與授權

`cjk_font.h` 由 [GNU Unifont](https://unifoundry.com/unifont/) 16.0.04 點陣化產生，
包含 ASCII、Big5 全部、GB2312 全部與程式碼中用到的字元（約 16,000 字）。

重新產生（需要 `unifont.hex` 或 `.hex.gz`，可自動下載；於專案根目錄執行）：

```bash
python tools/gen_cjk_font.py --download
```

產生器會自動尋找含 `.ino` 的 sketch 資料夾，掃描其中所有 `.c` / `.h` / `.cpp` / `.ino`
使用到的字元，並將結果輸出為
`LILYGO-T-Display-S3_Nav/cjk_font.h`（可用 `--out` 指定其他路徑）。

GNU Unifont 採雙授權，可擇一遵循：

1. GNU GPL v2 或更新版本，附**字型嵌入例外條款**（font embedding exception）
2. SIL Open Font License 1.1

產生的 `cjk_font.h` 為點陣化衍生資料，隨本專案散布；若再散布請保留本段授權說明。
**不建議**在未確認授權的情況下將本字型用於商業產品；商用請改採 Noto Sans CJK /
Source Han Sans 等 OFL 字型重新產生。

---

## 疑難排解

| 症狀 | 處理 |
| --- | --- |
| 螢幕全黑 | 確認 `PIN_POWER_ON`（GPIO15）為 HIGH；確認 `tft_setup.h` 有被載入（TFT_eSPI ≥ 2.5.31） |
| 中文顯示空心方塊 | 該字不在字型檔內；重新執行 `tools/gen_cjk_font.py`（會掃描 sketch 原始碼字元） |
| 網頁 404 / 空白 | `data/` 尚未上傳到 SPIFFS；複製到 sketch 資料夾後重新執行 Sketch Data Upload |
| 分割表未生效 / OTA 失敗 | 將 `partitions.csv` 複製到 `LILYGO-T-Display-S3_Nav/`，或手動選擇 `3MB APP / 9MB SPIFFS` |
| `nav.local` 無法開啟 | 部分 Android / Windows 不支援 mDNS，改用裝置 IP |
| WiFi 一直連不上 | 確認 2.4GHz、密碼正確；必要時清除設定回到 AP 模式（預設密碼 `12345678`） |
| 導航沒有箭頭 | 尚未規劃路線，或瀏覽器沒有送出位置；先在網頁按「規劃路線」 |
| 電池顯示 USB | 未接電池或電壓 > 4.32V（USB 供電時的正常現象） |

---

## 授權

程式碼：MIT（可自由修改使用）。
字型：GNU Unifont，GPLv2+（含字型嵌入例外）或 OFL 1.1，詳見上節。
