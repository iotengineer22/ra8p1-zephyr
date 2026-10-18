[English Version (README.md)](README.md)

# Renesas RA8 MIPI-CSI2 カメラ映像 LCDリアルタイム表示アプリケーション (`mipi_csi2`) for Zephyr RTOS

## 1. プロジェクト概要

本プロジェクトは、**Renesas EK-RA8P1** 評価キット（Arm Cortex-M85 @ 1.0 GHz）向けに作成されていた μT-Kernel 3.0 用カメラ映像表示プログラム（`tron_mipi_test_ori`）を、**Zephyr RTOS ネイティブ環境**へと完全移植したものです。

**OmniVision OV5640 CMOSイメージセンサ**から **2レーン MIPI-CSI2** インタフェース経由で映像を取得し、ハードウェア 2D グラフィックスアクセラレータ **D/AVE 2D (DRW)** を用いてリアルタイムにバイリニア拡大を行い、**1024×600 TFT 液晶パネル**の中央に **約 48〜51 FPS** の高速・滑らかな映像を描画します。

### 主な活用ハードウェア・機能
- Renesas **MIPI D-PHY & MIPI-CSI2** ハードウェアコントローラ
- Renesas **Video Input (VIN)** DMAエンジン（内蔵SRAM上のトリプルバッファリング）
- Renesas **D/AVE 2D (DRW)** グラフィックスアクセラレータ（320×240 $\rightarrow$ 800×600 バイリニアテクスチャ拡大）
- Renesas **GLCDC (Global LCD Controller)** （外部 64MB SDRAM 上のトリプルバッファリング）
- ハードウェア **GPT12** タイマーによるセンサ用 24.0 MHz XCLK 出力

---

## 2. システム構成と技術ハイライト

```
 +--------------------------------------------------------------------------------+
 |                           Renesas EK-RA8P1 Board                               |
 |                                                                                |
 |  +-------------------+  MIPI-CSI2  +----------------------------------------+  |
 |  |  OmniVision       |  (2 レーン) |  Renesas RA8P1 MCU (Cortex-M85 @ 1GHz) |  |
 |  |  OV5640 カメラ    |===========> |  [MIPI D-PHY] -> [MIPI CSI2 レシーバ]  |  |
 |  |  (320x240 YUV422) |             |                     |                  |  |
 |  +-------------------+             |              [VIN DMA エンジン]        |  |
 |      ^    ^    ^                   +---------------------|------------------+  |
 |      |    |    +--- CAM_RST (P709)                       v                     |
 |      |    +-------- MIPI_IF_EN (P108, LOW)    トリプルバッファ              |
 |      +------------- 24MHz XCLK (GPT12, P501)  (内蔵SRAM: 153.6 KB x 3)      |
 |                                                          |                     |
 |                                                          v                     |
 |                                            +---------------------------+       |
 |                                            | D/AVE 2D ハードウェア GPU |       |
 |                                            | - バイリニア拡大描画      |       |
 |                                            |   (320x240 -> 800x600)    |       |
 |                                            | - 外枠装飾ライン描画      |       |
 |                                            +---------------------------+       |
 |                                                          |                     |
 |                                                          v                     |
 |  +--------------------+     Parallel RGB888  +---------------------------+     |
 |  | 1024x600 TFT 液晶  | <=================== | GLCDC ディスプレイ制御    |     |
 |  | (中央 800x600表示) |                      | 外部 64MB SDRAM 描画面    |     |
 |  +--------------------+                      +---------------------------+     |
 +--------------------------------------------------------------------------------+
```

### 1. 高速 MIPI-CSI2 & VIN キャプチャパイプライン
- **MIPI D-PHY / CSI-2 レシーバ**: 2レーン高速（HS）受信を行い、YUV422（8-bit）データを取得。
- **VIN DMA コントローラ**: 内蔵 SRAM（`0x22000000`）に配置された 128 バイトアラインのトリプルバッファへ直接 DMA 転送。CPU コピー負荷をゼロ化し、フレーム落ちを防止。
- **Zephyr NVIC 割り込み統合**: `VIN_IRQ`、`VIN_ERR`、`MIPICSI_RX`、`MIPICSI_DL`、`MIPICSI_VC`、`MIPICSI_PM`、`MIPICSI_GST` の各ベクタを Zephyr の割り込みコントローラへ直接接続。

### 2. D/AVE 2D ハードウェアによるバイリニア拡大描画
- GPU 描画命令 `d2_blitcopy` とバイリニアテクスチャフィルタ（`d2_tm_filter`）により、320×240 のカメラ画像を 800×600 に高品質拡大。
- 1024×600 の液晶中央（$X=112, Y=0$）にセンタリング配置し、周囲にグリーンの装飾枠を描画。CPU 負荷をかけずに 48〜51 FPS の高速リフレッシュを実現。

### 3. クロック供給とハードウェア制御シーケンス
- **GPT12 24.0 MHz XCLK 出力**: タイマーチャネル 12（`P501` / `GTIOC12A`）から OV5640 の動作に必要な 24 MHz クロックを出力。
- **MIPI 切替スイッチ (`P108`)**: Active-Low（LOW）出力に設定し、ボード上の高速アナログマルチプレクサをカメラコネクタ側へ接続。
- **ハードウェアリセット (`P709`)**: 電源投入後、LOW 出力によるハードウェアリセット（100ms 保持）を経て HIGH に引き上げ、センサの内部回路・発振器を安定化。

### 4. トリプルバッファリングと AXI バス調停
- **外部 SDRAM トリプルバッファ**: 1024×600 RGB565 バッファ（1面約 1.2MB、計 3.6MB）を外部 64MB SDRAM（`0x68000000`）に確保。
- **AXI バス調停設定**: `R_BUS->BUSS[i].CNT = 0x0013` を設定し、GLCDC の読み出し DMA を最優先（Group 3 固定優先）に設定。同時メモリアクセス時にもディスプレイの FIFO アンダーラン（黒帯ノイズ）の発生を防止。
- **ティアリングフリー表示**: GLCDC の垂直同期（Vblank）割り込みと同期してバッファ切り替えを実行。

---

## 3. ハードウェア要件

| 項目 | 仕様 |
|---|---|
| **評価ボード** | Renesas EK-RA8P1 |
| **MCU** | R7KA8P1KFLCAC (Arm Cortex-M85 @ 1.0 GHz, Flash 2MB, SRAM 1MB) |
| **カメラモジュール** | OmniVision OV5640 CMOS センサ（2レーン MIPI CSI-2, 320×240 YUV422） |
| **LCD パネル** | RTKLCDPAR1S00001BE (1024×600 TFT-LCD, パラレル RGB888 接続) |
| **外部メモリ** | 64MB SDRAM (32-bit バス, 先頭アドレス: `0x68000000`) |
| **デバッガ** | オンボード SEGGER J-Link OB |

---

## 4. ピン配置一覧

| 信号名 | ピン番号 | 機能 | 説明 |
|---|---|---|---|
| `CAM_XCLK` | P501 | GPT12 (GTIOC12A) | カメラ用 24.0 MHz クロック出力 |
| `MIPI_IF_EN` | P108 | GPIO 出力 | ボード上 MIPI マルチプレクサ有効化 (Active Low) |
| `CAM_RST` | P709 | GPIO 出力 | OV5640 ハードウェアリセット (Active Low) |
| `IIC1_SDA` | P511 | ペリフェラル | カメラ制御用 I2C (SCCB) データ |
| `IIC1_SCL` | P512 | ペリフェラル | カメラ制御用 I2C (SCCB) クロック |
| `DISP_BLEN` | P514 | GPIO 出力 | LCD バックライト点灯 (Active High) |
| `DISP_RST` | P606 | GPIO 出力 | LCD パネルハードウェアリセット (Active Low) |
| `LCDC_CLK` | P515 | ペリフェラル | LCD ピクセルクロック (~40 MHz) |
| `LCDC_TCON[0..3]`| 各種 | ペリフェラル | LCD 制御信号 (VSYNC, HSYNC, DE) |
| `LCDC_DATA[00..23]` | 各種 | ペリフェラル | 24-bit パラレル RGB888 ビデオバス |

---

## 5. ビルドと書き込み手順

### 1. ビルド
PowerShell または コマンドプロンプトを開き、プロジェクトディレクトリで `build.bat` を実行します:
```bat
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\mipi_csi2
.\build.bat
```

### 2. 実機へのフラッシュ書き込み
Renesas EK-RA8P1 ボードを USB ケーブル（J-Link OB ポート）で PC に接続し、`flash.bat` を実行します:
```bat
.\flash.bat
```

### 3. シリアルモニタ出力例
J-Link Virtual COM ポートに **115200 bps, 8N1** で接続します:
```text
*** Booting Zephyr OS build v4.4.0-12700-g8dafb9a897da ***
Starting Zephyr MIPI-CSI2 Camera Application...

=== Zephyr MIPI-CSI2 Camera to LCD Display Application Start ===
task 2 running...
Testing physical SDRAM at address 0x68000000...
SDRAM verification SUCCESS!
=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===
Clearing SDRAM framebuffers to black...
Initializing LCD (GLCDC)...
LCD Backlight enabled.
Initializing D/AVE 2D Graphics Engine...
Starting Camera Subsystem Initialization...
Starting GPT12 Clock for Camera XCLK on P501...
Releasing Camera Reset (P709 -> HIGH)...
[Camera Init] Product ID High: 0x56, Low: 0x40
SUCCESS: Camera connection verified! (OV5640 detected)
Loading OV5640 Configuration Table...
Configuring OV5640 PLL and internal clocks...
Connecting MIPI & VIN Interrupts to NVIC...
Opening Video Input (VIN) Controller...
SUCCESS: Camera stream enabled.
SUCCESS: Camera capture started.
Entering Real-time Camera Display Loop (Bilinear Upscale 320x240 -> 800x600)...
[VIN CB] First frame captured successfully!
FPS: 51 | Capture Buffer: 0x22026200
FPS: 48 | Capture Buffer: 0x22026200
FPS: 48 | Capture Buffer: 0x2204BA00
FPS: 48 | Capture Buffer: 0x2204BA00
FPS: 48 | Capture Buffer: 0x22000A00
```

---

## 6. ビルド時のメモリ使用量 (RAM / ROM)

本プロジェクト（Zephyr RTOS カーネル + Renesas FSP + D/AVE 2D + カメラ映像処理）をビルドした際の、マイコン内蔵メモリおよび外部 SDRAM の使用状況です。

| メモリ種別 | 物理領域 / 用途 | 割り当てサイズ | 実使用量 | 使用率 (%) | 残り空き容量 |
|:---|:---|:---:|:---:|:---:|:---:|
| **ROM (Code Flash)** | 内蔵 Flash (`0x02000000`)<br>プログラムコード、定数、ベクタテーブル | 2,048 KB<br>(2.0 MB) | **69,456 B**<br>(約 67.8 KB) | **3.31%** | 約 1,980 KB<br>(96.69%) |
| **RAM (Internal SRAM)** | 内蔵 SRAM (`0x22000000`)<br>カーネル、スタック、ヒープ、カメラVIN受信用バッファ | 1,024 KB<br>(1.0 MB) | **728,897 B**<br>(約 711.8 KB) | **69.51%** | 約 312 KB<br>(30.49%) |
| **外部 SDRAM** | 外部 32-bit SDRAM (`0x68000000`)<br>LCD表示用トリプルバッファ (1024×600 RGB565 × 3面) | 64 MB | **3,686,400 B**<br>(約 3.52 MB) | **5.49%** | 約 60.48 MB<br>(94.51%) |

> [!NOTE]
> **RAM (内蔵 SRAM) 使用量の内訳**:
> - **VIN カメラキャプチャ用 YUV422 トリプルバッファ**: `320 × 240 × 2 Bytes × 3 面 = 460,800 Bytes`（内蔵 SRAM 全体の約 **43.9%** を占有）。内部 SRAM 上で DMA 受信を完結させることでゼロコピー転送を実現しています。
> - **Zephyr システムヒープ**: `65,620 Bytes` (`CONFIG_HEAP_MEM_POOL_SIZE=65536`)
> - **スレッドスタック領域**: 約 `43,000 Bytes`（メインループ Task 1: 32 KB、メインスレッド: 8 KB、割り込みスタック: 2 KB、Task 2: 1 KB 等）
> - **データ・カーネル BSS**: 約 `159,477 Bytes`

---

## 7. ライセンス情報

本プロジェクトは、商用利用・改変・製品組み込みが可能な**パーミッシブ（寛容型）マルチライセンス**で構成されています。GPL のようなソースコードの強制開示義務（コピーレフト）はありません。

| コンポーネント | 対象ファイル・範囲 | ライセンス | 著作権者 |
|:---|:---|:---|:---|
| **Zephyr RTOS 本体** | カーネル、スケジューラ、GPIO/I2C ドライバ、CMSIS | **Apache License 2.0** | The Zephyr Project / Linux Foundation |
| **Renesas FSP モジュール** | `src/mipi/` (VIN, MIPI-CSI2, PHY)<br>`src/drw/` (D/AVE 2D ラッパーコード) | **BSD 3-Clause License** | Renesas Electronics Corporation |
| **D/AVE 2D ドライバ** | `src/dave2d/` (2D 描画アクセラレータエンジン) | **Renesas Software License** (ロイヤリティフリー無償許諾) | Renesas Electronics Europe / TES Electronic Solutions |
| **アプリケーションコード** | `src/main.c`, `src/camera/`, `src/mipi/mipi_vin_config.*` | **Apache-2.0** または **BSD-3-Clause** | プロジェクト作成者 |

> [!NOTE]
> **μT-Kernel 3.0 (T-License 2.0) からの完全脱却**:
> 本プロジェクトは μT-Kernel 3.0 向けプログラム（`tron_mipi_test_ori`）をベースに移植されたものですが、タスクやセマフォ、同期処理などのカーネル依存コードはすべて Zephyr ネイティブ API に完全置換されています。そのため、T-License 2.0（TRONライセンス）の制約・義務は一切適用されません。
