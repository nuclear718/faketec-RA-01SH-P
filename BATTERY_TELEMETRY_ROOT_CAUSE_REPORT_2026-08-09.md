# nRF52 18650 電量未顯示於 Meshtastic App 的根因調查報告

- **調查日期：** 2026-08-09
- **主要專案：** `nuclear718/faketec-RA-01SH-P`
- **主要分支／固定版本：** `develop` / `9306d3f55f6f3a73325a5140fba38e9d92210301`
- **唯讀參考專案：**
  - `nuclear718/ntsocial-mesh-gateway-android` / `main` / `1fc4411fb0f76618e7e827585ebd433f434f1f78`
  - `nuclear718/NTsocial-with-Meshtastic-` / `main`
  - 官方 `meshtastic/Meshtastic-Android`，用來驗證標準 App 的顯示規則
- **本次變更範圍：** 僅新增本調查報告；未修改任何韌體、App、板級設定或硬體設計檔案。

---

## 1. 執行摘要

### 1.1 最終判定

Meshtastic App 並不是沒有支援此節點的電池資訊，也不是 App 漏解 `battery_level` 欄位。韌體確實建立並傳送標準 `DeviceMetrics`；真正的問題位於 **nRF52 電池量測路徑與韌體電源狀態判斷**。

端到端行為如下：

1. 此板型只靠 `P0.31` 的 ADC 輸入量測電池電壓，沒有電量計 IC。
2. 實體硬體必須存在 `VBAT → 分壓器 → P0.31` 的電壓感測路徑；韌體本身無法隔空知道 18650 電壓。
3. 韌體若判定「沒有電池」或「正在充電」，不會送出 0～100 的百分比，而會送出特殊值 `101`。
4. 官方 Meshtastic Android App 與 NTsocial MeshLink 都將 `battery_level > 100` 視為外部供電，顯示 `PWR`，而不是百分比。
5. `nrf52_promicro_diy_xtal` 目前沒有獨立的外部電源偵測腳；`Power::isVbusIn()` 退而以「量測電壓是否高於 4.20 V」判定 VBUS。
6. 此板型宣告的分壓比為 `0.6`，理論反算倍率應為 `1 / 0.6 = 1.6666667`，但韌體使用 `1.73`。因此回報電壓約高估 3.8%，實際 18650 只要約高於 **4.046 V**，就可能被錯判為外部供電／充電，進而傳送 `101`。

### 1.2 根因分層

| 層級 | 根因 | 證據狀態 | 對症狀的影響 |
|---|---|---:|---|
| 硬體量測層 | 實機若未配置、未焊接或接錯 `VBAT → 分壓器 → P0.31`，ADC 無法取得有效電池電壓 | **需以 PCB/BOM/萬用電表確認** | 最符合「不論電量高低，都沒有正常百分比」的情況 |
| 板級參數層 | 宣告分壓比 `0.6`，卻使用 `1.73` 反算，理論上高估約 3.8% | **已由程式碼證實** | 百分比及電壓偏高；滿電附近更容易觸發錯誤判斷 |
| 電源狀態層 | XTAL 板型以 `batteryVoltage > 4200 mV` 代替真正的 VBUS 偵測 | **已由程式碼證實** | 電池約高於 4.046 V 即可能被誤認為充電／外部供電 |
| Telemetry 層 | `!hasBattery || isCharging` 時固定傳送 `battery_level = 101` | **已由程式碼證實** | App 不會收到正常 0～100 百分比 |
| App 顯示層 | `battery_level > 100` 顯示 `PWR` | **已由兩個 App 證實，屬正常設計** | App 只是忠實顯示韌體提供的狀態，不是根因 |

### 1.3 對「真正原因」的精確表述

> **真正的軟體根因，是韌體把不可靠或被錯誤換算的 ADC 電壓同時拿來判定「有沒有電池」與「是否有外部供電」，再把異常結果轉成 `battery_level=101`；App 依 Meshtastic 規則把 101 顯示為 `PWR`，因此不會顯示一般電量百分比。**

至於實機為何進入這條錯誤路徑，存在兩種可區分的觸發條件：

- **整個放電區間都不顯示百分比：** 優先懷疑 P0.31 沒有接到有效分壓器、分壓器漏焊、接點錯誤，或實際電阻比與韌體不一致。
- **滿電附近顯示 PWR，放電後才恢復百分比：** 已可由現行 `1.73` 倍率加上 `>4.20 V` 的 VBUS 判斷完整解釋。

因為參考硬體專案只提供 Gerber ZIP，未提供可直接稽核的原理圖與 BOM，本報告不把「實機一定漏裝分壓器」寫成已證實事實；第 8 節提供可在單一節點上快速定案的量測程序。

---

## 2. 調查範圍與方法

本次沿著完整資料鏈進行稽核，而不是只搜尋 App 畫面：

```text
18650 電池
   ↓
外部電阻分壓器
   ↓
P0.31 / ADC
   ↓
Power::getBattVoltage()
   ↓
PowerStatus
   ├─ hasBattery
   ├─ isCharging
   ├─ isUSBPowered
   ├─ batteryVoltageMv
   └─ batteryChargePercent
   ↓
DeviceTelemetry / DeviceMetrics
   ├─ voltage
   └─ battery_level
   ↓
Meshtastic Android / NTsocial MeshLink
   ↓
百分比或 PWR
```

調查重點包括：

- nRF52 variant 的 ADC 腳位、分壓比、參考電壓與解析度。
- ADC 原始值如何轉換成毫伏。
- `hasBattery`、`isVbusIn`、`isCharging` 的判斷來源。
- `DeviceMetrics.battery_level` 的特殊值規則。
- 官方 App 與 NTsocial MeshLink 的顯示條件。
- 硬體專案是否有明確記載電池感測線與分壓器。

---

## 3. 板級設定與實體腳位

### 3.1 韌體宣告 P0.31 為電池 ADC

檔案：

- `variants/nrf52840/diy/nrf52_promicro_diy_xtal/variant.h`
- 固定版本：`9306d3f55f6f3a73325a5140fba38e9d92210301`

目前設定包含：

```cpp
#define BATTERY_PIN (0 + 31) // P0.31 Battery ADC
#define VBAT_DIVIDER (0.6F)
#define VBAT_DIVIDER_COMP (1.73F)
#define ADC_MULTIPLIER VBAT_DIVIDER_COMP
#define BATTERY_SENSE_RESOLUTION_BITS 12
#define AREF_VOLTAGE 3.0
#define VBAT_AR_INTERNAL AR_INTERNAL_3_0
```

`P0.31` 的含義是 nRF52840 **Port 0 的第 31 號 GPIO**，不是「Arduino 第 31 支外觀腳」。這類 SuperMini 模組常用三碼絲印表示原生 GPIO，因此：

- `010` = `P0.10`
- `031` = `P0.31`

`variant.cpp` 的 `g_ADigitalPinMap` 使用 0～31 的一對一映射，所以此專案中的 `BATTERY_PIN = 31` 確實會對應到 `P0.31`，不是 logical pin mapping 錯置。

### 3.2 韌體已假定外部存在分壓器

註解寫明：

```text
1.5 MΩ + 1.0 MΩ = 2.5 MΩ
分壓比 = 1.5 / 2.5 = 0.6
```

因此韌體隱含的硬體契約是：

```text
18650 正極 / VBAT
        │
      1.0 MΩ            ← Rtop
        │
        ├──────── P0.31 / 絲印 031 / ADC
        │
      1.5 MΩ            ← Rbottom
        │
18650 負極 / GND
```

必要條件：

- 電池負極、開發板 GND 與分壓器下端必須共地。
- P0.31 只量測電壓，不承擔供電電流。
- 18650 正極仍需接到板子的正確供電輸入；同一正極另外分出一條感測支路。
- **不得把充飽的 4.2 V 直接接到 P0.31。** 必須先分壓，否則可能超過 GPIO/ADC 可承受範圍。
- 高阻值分壓器可在 P0.31 與 GND 間加一顆約 `100 nF` 陶瓷電容，降低 ADC 取樣電容與 RF 雜訊造成的波動；實際值仍應以啟動時間與取樣穩定度驗證。

### 3.3 目前硬體文件存在缺口

`NTsocial-with-Meshtastic-` 的 README 有 SuperMini 與 LoRa 模組接線，但沒有明確記載：

- `031 / P0.31` 是電池感測腳。
- 電池正極到 P0.31 的上臂電阻。
- P0.31 到 GND 的下臂電阻。
- 電阻值、容差、BOM 料號與是否已內建於 PCB。
- 可供驗證的 `VBAT_SENSE` 測試點。

這個文件缺口會讓組裝者合理地以為「只要把 18650 接上供電端，韌體就能自動讀到電壓」，但 nRF52840 並不會自動知道外部電池端電壓。

---

## 4. 韌體如何計算電池電壓

### 4.1 ADC 初始化

`Power::analogInit()` 會：

1. 將 `BATTERY_PIN` 設為 `INPUT`，關閉內部上拉。
2. 將 ADC 解析度設為 `BATTERY_SENSE_RESOLUTION_BITS`，本板型為 12 bit。
3. 將類比電池量測物件指定給 `batteryLevel`。

所以只要 `BATTERY_PIN` 存在，韌體就假定該腳有合法的類比電池訊號。P0.31 若浮接，軟體仍會繼續取樣；浮接 ADC 讀值並不代表電池電壓。

### 4.2 濾波與換算

`AnalogBatteryLevel::getBattVoltage()` 最終使用下式換算：

```text
battery_mV = ADC_raw
             × ADC_MULTIPLIER
             × 1000
             × AREF_VOLTAGE
             ÷ 2^(BATTERY_SENSE_RESOLUTION_BITS)
```

目前參數為：

```text
ADC_MULTIPLIER = 1.73
AREF_VOLTAGE = 3.0 V
BATTERY_SENSE_RESOLUTION_BITS = 12
```

取樣本身並非單次讀取：韌體連續取得 30 筆 ADC，排序後去除最高與最低各 4 筆，再平均中間 22 筆。這可以抑制偶發雜訊，但無法修復：

- ADC 腳沒有接到電池。
- 分壓器比例錯誤。
- ADC 參考／校正係數錯誤。
- 用電池電壓推論 VBUS 的邏輯錯誤。

### 4.3 分壓倍率存在可計算的不一致

若硬體確實為：

```text
Rtop = 1.0 MΩ
Rbottom = 1.5 MΩ
```

則：

```text
Vadc / Vbat = 1.5 / (1.0 + 1.5) = 0.6
```

理論反算倍率應為：

```text
Vbat / Vadc = 1 / 0.6 = 1.6666667
```

但目前使用 `1.73`：

```text
總回報比例 = 0.6 × 1.73 = 1.038
```

亦即，在未考慮電阻容差與 ADC 誤差前，回報電壓已先系統性高估約：

```text
(1.038 - 1) × 100% = 3.8%
```

| 真實電池電壓 | 現行理想化回報值 | 誤差 |
|---:|---:|---:|
| 3.00 V | 3.114 V | +3.8% |
| 3.30 V | 3.425 V | +3.8% |
| 3.70 V | 3.841 V | +3.8% |
| 4.00 V | 4.152 V | +3.8% |
| 4.05 V | 4.204 V | +3.8% |
| 4.20 V | 4.360 V | +3.8% |

`1.73` 有可能原本意圖補償特定樣品、參考電壓或電阻誤差，但程式碼沒有提供校正來源、量測紀錄或板版號條件。把板級理論分壓倍率與單一樣品校正混為一個常數，會讓後續硬體版本難以維護。

---

## 5. 為何韌體會傳送 101，而不是百分比

### 5.1 有無電池判斷

目前關鍵門檻包括：

```text
noBatVolt    = 2600 mV
chargingVolt = 4200 mV
```

類比電池實作以量測電壓判斷是否有電池。若 P0.31 沒有合法訊號、分壓過低或換算不符，可能落入 `hasBattery = false`。

### 5.2 XTAL 板型沒有獨立外部電源偵測

此 variant 沒有宣告 `EXT_PWR_DETECT`。在 nRF52 分支內，`Power::isVbusIn()` 對部分特定板型使用 nRF USB 暫存器，但目前 `nrf52_promicro_diy_xtal` 不在該特例內，因此走到：

```cpp
return getBattVoltage() > chargingVolt;
```

也就是：

```text
量測電池電壓 > 4.20 V
        ↓
韌體宣告 VBUS 存在
        ↓
ARCH_NRF52 的 isCharging() 直接回傳 isVbusIn()
        ↓
韌體宣告正在充電
```

這個推論本質上不可靠：

- 電池電壓與 USB VBUS 是不同物理量。
- 一顆充飽的 18650 本來就可能接近 4.20 V。
- ADC 正偏差、電阻容差或錯誤倍率，足以跨過 4.20 V 門檻。
- VBUS 存在也不必然等於電池正在充電；充電器可能未接電池、已充滿、受溫度限制或已停止充電。

### 5.3 假外部供電門檻實際下降到約 4.046 V

因為回報值約為：

```text
Vreported = 1.038 × Vactual
```

目前判斷：

```text
Vreported > 4.20 V
```

換算成真實電池電壓：

```text
Vactual > 4.20 / 1.038
        > 4.046 V
```

因此只要電池剛充飽或仍高於約 4.05 V，韌體就可能將純電池供電誤判為外部供電／充電。

### 5.4 DeviceTelemetry 的 101 規則

`DeviceTelemetry::getDeviceTelemetry()` 會先傳送電壓：

```cpp
deviceMetrics->voltage = powerStatus->getBatteryVoltageMv() / 1000.0f;
```

但百分比欄位使用：

```cpp
if (!powerStatus->getHasBattery() || powerStatus->getIsCharging()) {
    deviceMetrics->battery_level = 101;
} else {
    deviceMetrics->battery_level = powerStatus->getBatteryChargePercent();
}
```

所以以下任一條件成立，都不會得到 0～100：

- `hasBattery == false`
- `isCharging == true`

而會固定得到：

```text
battery_level = 101
```

這正是硬體量測問題與 App 症狀之間的直接連結。

---

## 6. App 端不是根因

### 6.1 官方 Meshtastic Android App

官方 `MaterialBatteryInfo` 的規則為：

```kotlin
val isPowered = batteryLevel > 100
```

若 `isPowered`：

- 顯示電源插頭圖示。
- 顯示文字 `PWR`。

否則才顯示：

```text
0% ～ 100%
```

因此 `101` 不是「101%」，而是 Meshtastic 既有的外部供電特殊值。

### 6.2 NTsocial MeshLink

`ntsocial-mesh-gateway-android` 的 `MaterialBatteryInfo.kt` 採用相同規則：

```kotlin
val isPowered = batteryLevel > 100
```

因此：

- 官方 App 與 NTsocial App 對相同封包的行為一致。
- 不應在 App 端把 101 強制改成 100%，那會掩蓋韌體錯誤並造成錯誤資訊。
- 根本修正應讓節點在純電池供電時傳送可信的 0～100 值。

### 6.3 Telemetry 傳送本身存在

`DeviceTelemetryModule` 在節點啟動後建立，並定期將裝置 Telemetry 傳給手機；其中送往手機的週期設定為一分鐘。因此完成硬體／韌體修正後，測試時應：

- 重新啟動節點或重新連線。
- 至少等待一個完整 Telemetry 週期。
- 不要只看刷機後前幾秒的舊快取資料。

---

## 7. 症狀與原因對照表

| 實際症狀 | 最可能原因 | 判讀方式 |
|---|---|---|
| 從滿電到低電都只顯示 PWR 或沒有百分比 | P0.31 浮接、分壓器未裝、感測線錯接，導致 `hasBattery=false` 或讀值異常 | 同時量測 VBAT 與 031；檢查 `V031 / VBAT` |
| 只有滿電附近顯示 PWR，使用一段時間後出現百分比 | `1.73` 高估加上 `>4.20 V` 的假 VBUS 判斷 | 電池降至約 4.04 V 以下後觀察是否恢復 |
| 百分比有顯示，但與萬用電表差很多 | 實際分壓比、電阻容差、ADC 參考或倍率不一致 | 以多個電壓點做線性校正 |
| 電量跳動很大 | 高阻分壓器、ADC 取樣電容、RF 雜訊或 P0.31 浮接 | 在感測節點加適當電容並檢查佈線 |
| USB 拔除後仍長時間顯示 PWR | 電壓仍被判為 >4.20 V、狀態更新延遲或 App 快取 | 看即時韌體 log 與下一個 Telemetry 週期 |
| 低電量時突然變 PWR | 實際可能使用 1 MΩ / 1 MΩ，但韌體仍套用 1.73，或 ADC 路徑失效造成 `hasBattery=false` | 量測分壓比例並核對 BOM |

### 7.1 若實際 PCB 使用 1 MΩ / 1 MΩ

若硬體實際為：

```text
Rtop = 1.0 MΩ
Rbottom = 1.0 MΩ
分壓比 = 0.5
```

但韌體仍使用 `1.73`，則：

```text
Vreported = Vactual × 0.5 × 1.73
          = Vactual × 0.865
```

| 真實電池電壓 | 回報值 |
|---:|---:|
| 4.20 V | 3.633 V |
| 4.00 V | 3.460 V |
| 3.70 V | 3.201 V |
| 3.30 V | 2.855 V |
| 3.00 V | 2.595 V |

接近 3.0 V 時會低於 `noBatVolt = 2.6 V`，進而被判為沒有電池並傳送 101。這是假設情境，不代表目前 PCB 已證實使用 1 MΩ / 1 MΩ；其目的在說明 **BOM 與韌體倍率必須成對管理**。

---

## 8. 不修改程式碼即可完成的實機定案程序

以下程序應先完成，再決定最終修改值。每個步驟只需一部節點、可調電源或 18650、萬用電表與序列埠。

### 8.1 關機電阻／導通檢查

1. 拔除 USB、18650 與所有外部電源。
2. 找出模組絲印 `031` 的焊孔或 PCB 對應網路。
3. 量測 VBAT 正端到 031 的電阻。
4. 量測 031 到 GND 的電阻。
5. 核對是否接近預期：
   - VBAT → 031：約 1.0 MΩ。
   - 031 → GND：約 1.5 MΩ。
6. 若完全開路，表示感測支路不存在或未焊。
7. 若接近 1.0 MΩ / 1.0 MΩ，表示實際 PCB 與 variant 註解不一致。

注意：在電路板上直接量阻值可能受 MCU 保護電路與其他並聯路徑影響；若數值不清楚，應對裸板、未焊模組板或分離單側電阻再量測。

### 8.2 通電電壓檢查

USB 必須先拔除，只以電池供電：

1. 量測 `VBAT` 對 GND，記為 `Vbat`。
2. 量測 `031` 對 GND，記為 `V031`。
3. 計算：

```text
ratio = V031 / Vbat
```

判讀：

| `V031 / Vbat` | 結論 |
|---:|---|
| 約 0.60 | 硬體大致符合 1.0 MΩ / 1.5 MΩ 假設 |
| 約 0.50 | 硬體較像 1.0 MΩ / 1.0 MΩ，韌體倍率需重設 |
| 約 0 | 感測線未接、下拉短路、上臂缺件或量錯點 |
| 接近 1.0 | 可能直接把 VBAT 接到 ADC，存在超壓風險，應立即斷電 |
| 持續飄動 | P0.31 可能浮接或量測節點阻抗／接地有問題 |

若符合 0.60，4.20 V 電池的 031 應約為：

```text
4.20 × 0.60 = 2.52 V
```

### 8.3 三點校正

使用可調電源模擬電池，至少測試：

- 3.30 V
- 3.70 V
- 4.20 V

每一點記錄：

- 電源供應器實際輸出。
- DMM 量得 VBAT。
- DMM 量得 P0.31。
- 韌體計算的 `batteryVoltageMv`。
- `hasBattery`。
- `isVbusIn`。
- `isCharging`。
- 最終 `battery_level`。

這組資料可以一次區分：

- 硬體分壓比錯誤。
- ADC 參考／倍率錯誤。
- VBUS 邏輯誤判。
- Telemetry 特殊值誤用。

### 8.4 最小診斷 log 建議

後續實作階段可暫時加入下列診斷欄位，驗證後再決定是否保留為 debug log：

```text
raw_adc
v_adc_mv
v_battery_mv
has_battery
vbus_detected
is_charging
battery_percent
telemetry_battery_level
```

不要只印最終百分比，否則無法判斷錯誤發生在哪一層。

---

## 9. 具體修改計畫

本節是施工計畫，不代表本次已修改程式碼。

### P0-1：先固定硬體量測契約

**目標：** 明確定義每一塊量產板的 `VBAT_SENSE` 電路，消除「韌體以為存在、硬體實際不存在」的情況。

1. 在原理圖與 BOM 明列：
   - `Rtop`：VBAT 到 P0.31。
   - `Rbottom`：P0.31 到 GND。
   - 電阻值與容差，建議至少 1%。
   - 可選濾波電容及其值。
2. 在 PCB 網路命名加入 `VBAT_SENSE`，並增加測試點。
3. 在 README 明確標示：
   - SuperMini 絲印 `031` 即 P0.31。
   - 18650 不可直接接到 031。
   - 若 PCB 已內建分壓器，不得再重複外接另一組。
4. 對現有庫存板逐版確認：
   - 哪些版本有分壓器。
   - 哪些版本電阻值不同。
   - 哪些版本需要飛線／補件。
5. 以板版號對應韌體 variant，禁止不同分壓 BOM 共用同一組未註明的倍率。

### P0-2：讓 ADC_MULTIPLIER 直接由實際電阻比推導

**目標：** 不再以不明來源的 `1.73` 同時代表理論倍率與校正補償。

若確認硬體為 1.0 MΩ / 1.5 MΩ：

```text
ADC_MULTIPLIER 理論值 = (Rtop + Rbottom) / Rbottom
                       = 2.5 / 1.5
                       = 1.6666667
```

建議將參數拆成：

```text
理論分壓反算倍率 × 經實測得到的 ADC 校正係數
```

例如概念上：

```cpp
VBAT_R_TOP_OHMS
VBAT_R_BOTTOM_OHMS
VBAT_DIVIDER_INVERSE
VBAT_ADC_CALIBRATION
ADC_MULTIPLIER = VBAT_DIVIDER_INVERSE * VBAT_ADC_CALIBRATION
```

校正係數必須由多個電壓點、數部樣品推導，不能只為了讓單一滿電樣品看起來接近 4.2 V 而硬填常數。

### P0-3：停止用電池電壓推論 VBUS

**目標：** 將「電池電壓」與「外部電源存在」拆開。

對 nRF52840 USB 板型，優先評估直接使用 nRF POWER 的 USB VBUS 偵測狀態：

```text
NRF_POWER->USBREGSTATUS
POWER_USBREGSTATUS_VBUSDETECT_Msk
```

目前程式已對部分 nRF52 板型使用這條路徑，但 XTAL variant 未被納入。建議：

1. 驗證此 SuperMini 的 USB 供電路徑確實能讓 nRF52840 VBUS detector 反映 USB 插拔。
2. 若有效，讓 XTAL variant 使用相同硬體 VBUS 偵測，而不是 `batteryVoltage > 4200 mV`。
3. 若板子無法使用內建 VBUS detector，則增加真正的 `EXT_PWR_DETECT` 網路／GPIO。
4. 禁止以電池電壓門檻作為 VBUS 的正式判斷來源。

### P0-4：修正 Telemetry 的狀態來源

**目標：** `battery_level=101` 只能表示已被可靠確認的外部供電狀態，不能把「ADC 未知」自動包裝成 PWR。

建議決策順序：

```text
確認外部供電或充電狀態
    → battery_level = 101
否則，確認有有效電池量測
    → battery_level = 0～100
否則
    → 標記未知／不提供百分比，並保留診斷資訊
```

實作時需遵循現有 protobuf 欄位的 presence 語意；若既有 schema 無法表示 unknown，不應任意選一個正常百分比冒充。最小安全修正仍是先讓 P0.31 與 VBUS 判斷可靠，避免進入 unknown。

### P1-1：校正電壓轉百分比曲線

目前 OCV 表中：

- 100% 約 4190 mV。
- 0% 約 3200 mV。

但另有 `LOWEST_BATTERY_OCV = 3100`。建議統一：

- 關機保護電壓。
- 0% 顯示電壓。
- `hasBattery` 的最低有效電壓。
- 使用中的負載壓降與靜置 OCV 差異。

18650 的電壓—SOC 關係與電芯化學、負載、溫度及老化有關；不應把百分比當作精密庫侖計結果。對此硬體而言，合理目標是穩定、單調且不出現明顯錯判。

### P1-2：加入合理性檢查與故障辨識

建議加入：

- ADC 飽和偵測。
- 連續數次讀值超出物理範圍才改變狀態，避免單次雜訊。
- 浮接／開路的診斷 log。
- 電壓變化速率檢查，避免瞬間從 3.7 V 跳到 4.4 V 就宣告充電。
- 將 raw ADC 與換算後毫伏保留在 debug build。

此處不建議建立複雜狀態機；簡單的範圍檢查、連續樣本確認與真正 VBUS 訊號已足夠。

### P1-3：補齊文件與製造測試

每塊出廠板至少執行：

1. 以 3.70 V 測試電源供電。
2. 確認 P0.31 分壓節點落在預期比例。
3. 確認韌體回報電壓誤差符合規格。
4. 純電池供電時 App 顯示 0～100，而非 PWR。
5. 插入 USB 後 App 顯示 PWR。
6. 拔除 USB 後於下一個 Telemetry 週期恢復百分比。

### P2：App 僅增加診斷，不修改既有語意

App 不需要為根因修正而改動。可選的工程診斷改善：

- 在開發者頁面同時顯示 raw `voltage` 與 `battery_level`。
- 對 101 顯示「外部供電」，不要只顯示 PWR。
- 若未收到有效電池資料，顯示「未知」，而不是猜測 0% 或 100%。

這些屬可觀測性改善，不能取代韌體與硬體修正。

---

## 10. 建議施工順序

為避免同時修改多層而失去可歸因性，建議依序執行：

1. **量測現有 PCB：** 確認 P0.31 是否真的有分壓、實際比例是多少。
2. **固定 BOM：** 選定唯一分壓器配置並更新原理圖、BOM、README。
3. **修正 ADC 倍率：** 先用理論值，再用多樣品校正係數微調。
4. **修正 VBUS 偵測：** 改用 nRF VBUS detector 或實體 `EXT_PWR_DETECT`。
5. **驗證 PowerStatus：** 分別確認純電池、USB、電池加 USB、無電池加 USB。
6. **驗證 DeviceTelemetry：** 檢查 `voltage` 與 `battery_level`。
7. **最後驗證兩個 App：** 官方 Meshtastic 與 NTsocial MeshLink 應得到一致結果。
8. **建立回歸測試與製造測試：** 防止未來換電阻或 variant 時再次發生。

不建議先改 App，因為那只會隱藏錯誤的 101。

---

## 11. 驗證矩陣

| 測試情境 | 預期 PowerStatus | 預期 Telemetry | App 預期 |
|---|---|---|---|
| 18650，3.30 V，USB 拔除 | 有電池、無 VBUS、未充電 | 正常低電量百分比 | 顯示百分比 |
| 18650，3.70 V，USB 拔除 | 有電池、無 VBUS、未充電 | 正常中段百分比 | 顯示百分比 |
| 18650，4.20 V，USB 拔除 | 有電池、無 VBUS、未充電 | 接近 100%，不得為 101 | 顯示百分比 |
| 18650 + USB | 有電池、有 VBUS；充電狀態依硬體 | 101 | 顯示 PWR |
| 無電池 + USB | 無電池、有 VBUS | 101 | 顯示 PWR |
| 拔除 USB、保留電池 | 有電池、無 VBUS | 0～100 | 下一週期恢復百分比 |
| P0.31 開路故障 | 電池狀態未知／錯誤，不得因電壓猜成 VBUS | 應可診斷，不應偽裝正常百分比 | 未知或明確故障狀態 |
| 連續 RF 傳輸 | 電壓可小幅下降但應穩定、單調 | 不應在 101 與百分比間抖動 | 顯示穩定 |

---

## 12. 驗收條件

修正完成後，至少滿足：

1. 純 18650 供電時，在 3.30～4.20 V 全區間不會顯示 PWR。
2. DMM 與韌體回報電壓誤差建議控制於 ±2%，最差不得超過已定義規格。
3. 4.20 V 純電池不得被判定為 VBUS 或 charging。
4. 插拔 USB 的狀態轉換可在下一個 Telemetry 週期內正確反映。
5. 官方 Meshtastic App 與 NTsocial MeshLink 顯示一致。
6. 重新啟動、睡眠喚醒與 RF 高負載後，電量不應跳成 PWR。
7. README、原理圖、BOM、variant 常數與量產板版號完全對應。
8. 至少以 3 部板子、3 個電壓點完成校正；不要只用單一節點定義全域校正係數。

---

## 13. 程式碼證據索引

主要證據檔案：

- `variants/nrf52840/diy/nrf52_promicro_diy_xtal/variant.h`
  - P0.31、分壓比、1.73 倍率、12-bit、3.0 V 參考。
- `variants/nrf52840/diy/nrf52_promicro_diy_xtal/variant.cpp`
  - logical pin 31 與 P0.31 的一對一映射。
- `src/Power.cpp`
  - ADC 初始化、濾波、電壓換算、`isVbusIn()`、`isCharging()`、PowerStatus 更新。
- `src/power.h`
  - 2600 mV 無電池門檻、4200 mV charging 門檻、OCV 表。
- `src/modules/Telemetry/DeviceTelemetry.cpp`
  - `!hasBattery || isCharging` 時傳送 101。
- `src/modules/Telemetry/DeviceTelemetry.h`
  - 裝置 Telemetry 的生命週期與送往手機週期。
- `ntsocial-mesh-gateway-android/core/ui/src/commonMain/kotlin/com/ntsocial/meshlink/core/ui/component/MaterialBatteryInfo.kt`
  - `batteryLevel > 100` 顯示 PWR。
- 官方 `Meshtastic-Android/core/ui/src/commonMain/kotlin/org/meshtastic/core/ui/component/MaterialBatteryInfo.kt`
  - 同一套 PWR 規則。

---

## 14. 調查限制

1. `NTsocial-with-Meshtastic-` 中的 PCB 以 Gerber ZIP 提供，未附可直接閱讀的原理圖與 BOM；本次 GitHub 唯讀稽核無法從文字程式碼證實實體量產板是否已焊接 1.0 MΩ / 1.5 MΩ 分壓器。
2. 未取得故障節點的萬用電表數據、序列 log、原始 ADC 讀值與 App 畫面，因此無法在「缺少分壓器」與「滿電誤判」之間替特定實機做最後二選一。
3. 上述限制不影響已確認的軟體事實：
   - App 正確解析 101。
   - 韌體會在無電池或充電時送 101。
   - XTAL variant 以電池電壓推論 VBUS。
   - 目前 `0.6 × 1.73` 造成約 +3.8% 的理論偏差。

只要完成第 8 節的兩個電壓量測，便可在不修改程式碼的前提下確定實機屬於哪一種觸發條件。

---

## 15. 最終結論

本問題不應以修改 Meshtastic App UI 解決。App 沒有遺失電池欄位，而是收到韌體送出的特殊供電值，或收到由錯誤電源狀態所產生的資料。

最小且正確的修正方向是：

1. 確認並固定 `VBAT → 分壓器 → P0.31` 的硬體路徑。
2. 依實際電阻比修正 ADC 反算倍率，將理論倍率與校正係數分離。
3. 以真正的 VBUS 訊號判定外部供電，不再以電池電壓高於 4.20 V 代替。
4. 確保純電池供電時 `DeviceTelemetry` 傳送 0～100，只有確定外部供電時才傳送 101。
5. 以三點電壓、USB 插拔與兩個 App 完成端到端驗收。

其中第 2、3、4 點已由程式碼證據確認必須修正；第 1 點則需對實體 PCB/BOM 做一次量測確認。這套計畫不需要重構 Meshtastic Telemetry 協議，也不需要改寫 App，屬於範圍小、風險可控且可直接驗證的修正。
