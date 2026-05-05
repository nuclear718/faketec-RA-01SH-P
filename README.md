# NTsocial Meshtastic nRF52 Firmware

這個 repository 是從 Meshtastic firmware 生態系延伸出來的開源韌體專案，目標是為 [NTsocial-with-Meshtastic-](https://github.com/nuclear718/NTsocial-with-Meshtastic-) 打造專用、可重現、可長期維護的 Meshtastic 節點韌體。

本 fork 不再以支援所有 Meshtastic 官方硬體為主要目標，而是聚焦在 `SuperMini nRF52840` 搭配 `Adafruit RFM95W` LoRa 模組的低功耗節點。這個硬體組合成本低、容易自行組裝，並且因為 nRF52840 的低功耗特性，適合長時間作為 NTsocial 專案中的 Meshtastic 節點使用。

## 專案定位

NTsocial 需要的是一套穩定、清楚、容易讓使用者刷入的 Meshtastic 韌體流程。官方 Meshtastic 韌體提供了完整基礎，但通用韌體和上游設定不一定完全符合 `SuperMini nRF52840 + Adafruit RFM95W` 的接線、低功耗和實際使用需求。

因此這個 fork 會專注於：

- 維護 NTsocial 專用的 Meshtastic nRF52 韌體
- 修正或繞過上游官方韌體中影響此硬體組合的小問題
- 保留完整開源精神，讓使用者可以檢查、修改、重新編譯與再散布
- 提供已編譯完成、可直接刷入的 UF2 韌體
- 保留 DIY variant 設定，方便後續調整接腳、電源策略與低功耗設定

這不是 Meshtastic 官方發行版，也不是要取代 Meshtastic 官方韌體。本專案會盡量延續 Meshtastic 的上游成果，同時把維護重點收斂到 NTsocial 需要的硬體與使用情境。

## 目標硬體

| 項目 | 內容 |
| --- | --- |
| MCU | `SuperMini nRF52840` |
| LoRa 模組 | `Adafruit RFM95W` |
| LoRa driver | `RF95 / SX127x` |
| 韌體目標 | `nrf52_promicro_diy_xtal` |
| 主要用途 | 低成本、低功耗、長時間運作的 Meshtastic 節點 |

## 本專案目前重點檔案

以下是這個 fork 目前主要維護與使用的檔案：

```text
bin/Meshtastic_nRF52_factory_erase_v3_S140_6.1.0.uf2
release/firmware-nrf52_promicro_diy_xtal-2.7.12.6ccbda8c.uf2
variants/nrf52840/diy/
```

### 已編譯韌體

`release/firmware-nrf52_promicro_diy_xtal-2.7.12.6ccbda8c.uf2`

這是目前提供給 `SuperMini nRF52840 + Adafruit RFM95W` 使用的主要 Meshtastic 韌體。若你只是要刷入已驗證的韌體，通常會使用這個檔案。

SHA-256:

```text
DF43534375177F4F21CE105B21D0F716EE2CD473AC6E36657FE25825CFBC66B1
```

### Factory erase

`bin/Meshtastic_nRF52_factory_erase_v3_S140_6.1.0.uf2`

這個 UF2 用於清除 nRF52 裝置上的既有設定或狀態。若裝置曾刷過其他 Meshtastic 韌體、設定異常、藍牙配對或 LoRa 狀態不穩，可以先執行 factory erase，再刷入本專案韌體。

SHA-256:

```text
E082C085D2DBF58FEAC73C6BF47A9A148909C7613069493A4488D640714A4F34
```

### DIY variant

主要設定位於：

```text
variants/nrf52840/diy/nrf52_promicro_diy_xtal/
```

其中最重要的是：

```text
variant.h
variant.cpp
platformio.ini
```

這組 variant 定義了 `SuperMini nRF52840` 與 `Adafruit RFM95W` 之間的 SPI、IRQ、reset、電源與其他周邊腳位，是本 fork 的核心維護範圍。

## RFM95W 接線摘要

目前 `nrf52_promicro_diy_xtal` variant 使用以下 LoRa 腳位：

| Adafruit RFM95W | SuperMini nRF52840 | 說明 |
| --- | --- | --- |
| `VIN` | `VCC / 3.3V` | LoRa 模組供電 |
| `GND` | `GND` | 共地 |
| `SCK` | `P0.17` / `O17` | SPI clock |
| `MISO` | `P0.08` / `O08` | SPI MISO |
| `MOSI` | `P0.06` / `O06` | SPI MOSI |
| `CS` | `P0.24` / `O24` | LoRa chip select |
| `RST` | `P0.09` / `O09` | LoRa reset |
| `G0` | `P0.11` / `O11` | `DIO0 / IRQ` |
| `EN` | 空接 | 目前不需連接 |

注意：在 USB 送電、重開機、刷機或測試前，請先接好 LoRa 天線。RFM95W / SX127x 類射頻模組在沒有天線或適當 RF 負載時進入發射狀態，可能增加射頻前端受損風險。

## 刷入流程

1. 完成 `SuperMini nRF52840` 與 `Adafruit RFM95W` 的接線。
2. 確認 LoRa 天線已接上。
3. 讓 nRF52840 進入 UF2 bootloader 模式。
4. 如需清除舊狀態，先複製 `bin/Meshtastic_nRF52_factory_erase_v3_S140_6.1.0.uf2` 到 UF2 磁碟。
5. 裝置重啟後，再次進入 UF2 bootloader 模式。
6. 複製 `release/firmware-nrf52_promicro_diy_xtal-2.7.12.6ccbda8c.uf2` 到 UF2 磁碟。
7. 等待裝置自動重啟。
8. 使用 Meshtastic App 或相容工具搜尋並連線裝置。

若刷入後無法正常啟動或找不到 LoRa，請優先檢查 `SCK / MISO / MOSI / CS / RST / G0` 接線是否與 `variant.h` 一致。

## 自行編譯

本專案保留 PlatformIO 編譯流程。目標環境為：

```text
nrf52_promicro_diy_xtal
```

常用編譯指令：

```powershell
pio run -e nrf52_promicro_diy_xtal
```

相關設定由根目錄 `platformio.ini` 載入：

```text
variants/*/diy/*/platformio.ini
```

如果你修改了接線、LoRa 模組、電源控制或其他周邊定義，請優先檢查並修改：

```text
variants/nrf52840/diy/nrf52_promicro_diy_xtal/variant.h
```

## 與 NTsocial 的關係

[NTsocial-with-Meshtastic-](https://github.com/nuclear718/NTsocial-with-Meshtastic-) 是主要面向使用者的硬體整合與刷機說明專案。這個 firmware repository 則負責保存、修改與發佈 NTsocial 所需的 Meshtastic 韌體。

簡單來說：

- `NTsocial-with-Meshtastic-`：說明硬體、接線、刷機與使用流程
- 本 repository：維護 NTsocial 專用 Meshtastic 韌體原始碼、variant 與 UF2 檔案

未來這個 fork 的變更會以 NTsocial 的實際需求為優先，例如低功耗表現、RFM95W 穩定性、刷機流程、節點長時間運作與使用者可重現性。

## 開源承諾

本 fork 會繼續保持開源。任何人都可以檢視此專案如何修改 Meshtastic 韌體、如何設定 nRF52840 腳位、如何產生 UF2 韌體，也可以依照授權條款進行修改與再散布。

本 repository 保留上游 Meshtastic firmware 的授權基礎。請參考本專案內的 `LICENSE`、`CONTRIBUTING.md` 與相關上游檔案。

## 重要提醒

- 本專案不是 Meshtastic 官方支援韌體。
- 本專案目前聚焦於 `SuperMini nRF52840 + Adafruit RFM95W`。
- 使用其他 nRF52840 板、其他 LoRa 模組或不同接線時，必須自行確認並修改 variant。
- 刷機、接線與射頻測試都有硬體風險，操作前請確認電壓、腳位與天線狀態。
- 若要回報問題，請盡量提供硬體版本、接線方式、刷入的 UF2 檔名、Meshtastic App 連線狀態與序列埠 log。

## 致謝

本專案建立在 Meshtastic 開源社群與 nRF52 / RadioLib / PlatformIO 生態系之上。這個 fork 的目標是把這些上游成果整理成 NTsocial 可以長期使用與維護的專用韌體基礎。
