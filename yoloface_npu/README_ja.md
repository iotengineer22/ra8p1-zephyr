[English Version (README.md)](README.md)

# Zephyr YOLO 顔検出システム (Arm Ethos-U55 NPU + MIPI-CSI2 カメラ + D/AVE 2D + GLCDC)
**対象ボード**: Renesas EK-RA8P1 (Arm Cortex-M85 @ 1.0 GHz + Arm Ethos-U55 NPU)

---

## 1. 概要

本プロジェクトは、ルネサス EK-RA8P1 評価キット上で動作する、**Arm Ethos-U55 NPU**、**MIPI-CSI2 カメラ**、および **D/AVE 2D (DRW) ハードウェアグラフィックスエンジン** を統合した **Zephyr RTOS ネイティブ版 YOLO-Fastest リアルタイム顔検出システム** です。

OmniVision **OV5640 センサ** からのカメラ映像を 2-lane MIPI-CSI2 経由で取得し、D/AVE 2D により **1024×600 パラレル TFT 液晶** へバイリニア拡大転送（320×240 $\rightarrow$ 800×600）しつつ、同時にオンチップの **Arm Ethos-U55 NPU** を用いて超高速（**8 ms**）に顔検出を実行し、検出バウンディングボックスおよびステータス HUD パネルを常時安定表示します。

---

## 2. 主な機能と技術的特徴

### ① Arm Ethos-U55 NPU ハードウェアアクセラレーション
- **超高速 8 ms 推論**:
  - YOLO-Fastest（192×192 int8 1ch グレースケール入力）モデルを実行。
  - テンソルアリーナ（Tensor Arena: 約 442 KB）および入力バッファをオンチップ超高速 SRAM1 (`0x22170000`) に完全配置。外部 SDRAM バス帯域との競合を完全に排除し、推論時間を 91 ms から **8 ms** へ大幅短縮。
- **非同期スレッド駆動**:
  - 表示スレッドからセマフォ（`g_ai_kick_sem`）で非同期キックされ、表示フレームレートを落とさずにバックグラウンドで連続推論。

### ② MIPI-CSI2 & VIN DMA ビデオパイプライン
- **2-lane MIPI-CSI2 & D-PHY**: OV5640 から 320×240 YUV422 映像を安定キャプチャ。
- **Video Input (VIN) DMA**: 内蔵 SRAM 上のトリプルキャプチャバッファへゼロコピー転送。
- **GPT12 24.0 MHz XCLK 供給**: タイマチャンネル 12 (`P501`) によりカメラ動作に必要な 24 MHz クロックを安定出力。

### ③ D/AVE 2D (DRW) ハードウェアバイリニア拡大
- D/AVE 2D コプロセッサの `d2_blitcopy` により、320×240 のカメラ映像をリアルタイムに **800×600 にバイリニア補間拡大**。
- 1024×600 LCD の中央（X=112, Y=0）に正確に配置し、毎フレーム滑らかな **48 FPS** を維持。

### ④ 画面左上 HUD パネルの完全常時表示と同期最適化
- **HUD ステータスパネル (X=118, Y=8, 幅=216, 高さ=78)**:
  - 黒背景＋視認性の高い 2px 太さの鮮明な緑色枠線で構成。
  - `AI Time: 8 ms`（リアルタイム NPU 推論時間）
  - `Score  : %`（検出確信度、非検出時は前回最高値または 0%）
  - `Faces  : N`（検出人数、非検出時は 0）
  - **顔の有無にかかわらず毎フレーム 100% 常時安定表示**。
- **各顔バウンディングボックス＆確信度バッジ**:
  - 検出された顔の座標に追従する 3px 幅の緑色バウンディングボックス。
  - 各顔の上部に黒ピル背景＋緑枠＋数値による確信度バッジ（例: `71%`）を表示。
- **D/AVE 2D 1フレーム非同期遅延の完全同期化**:
  - D/AVE 2D のコマンドキュー仕様（`d2_endframe` は現フレームを発行せず保留する仕様）に対し、明示的な `d2_startframe()` による即時発行と `d2_flushframe()` による同期待機を導入。
  - GPU によるカメラ拡大転送が 100% 完了した「後」に CPU が HUD パネルを描画する厳密な順序制御を確立し、カメラ映像による HUD の上書き・点滅を完全に解消。

### ⑤ 外部 64MB SDRAM トリプルバッファ＆AXI バス調停
- **トリプルバッファリング**: 外部 SDRAM (`0x68000000`) 上に 3 面の 1024×600 RGB565 フレームバッファ（各 1.2 MB、計 3.6 MB）を確保。
- **GLCDC AXI バス調停優先度**: `R_BUS->BUSS[i].CNT = 0x0013`（Group 3 = GLCDC 固定優先）を設定し、描画中も GLCDC の DMA 読み出しを最優先化して画面のチラつきやアンダーフローを根絶。
- **ティアリングフリー**: GLCDC の垂直同期（Vblank / ライン検知割り込み）に同期してバッファを切り替え。

---

## 3. システムアーキテクチャ

```
  +-----------------------------------------------------------------------------------------+
  |                                  Renesas EK-RA8P1 Board                                 |
  |                                                                                         |
  |  +--------------------+  MIPI-CSI2  +------------------------------------------------+  |
  |  | OmniVision OV5640  |  (2 Lanes)  | RA8P1 MCU (Arm Cortex-M85 @ 1.0 GHz)           |  |
  |  | Image Sensor       |===========> | [MIPI D-PHY] -> [MIPI CSI-2] -> [VIN DMA]      |  |
  |  +--------------------+             +-----------------------|------------------------+  |
  |      ^    ^    ^                                            v                           |
  |      |    |    +--- CAM_RST (P709)             Camera Capture Buffer (SRAM)             |
  |      |    +-------- MIPI_IF_EN (P108, LOW)                  |                           |
  |      +------------- 24MHz XCLK (GPT12, P501)       +--------+--------+                  |
  |                                                    |                 |                  |
  |                                                    v                 v                  |
  |                                         +-------------------+ +----------------------+  |
  |                                         | D/AVE 2D (GPU)    | | Pre-process          |  |
  |                                         | - 320x240->800x600| | (192x192 int8 Gray)  |  |
  |                                         | - Bilinear Upscale| +----------+-----------+  |
  |                                         | - Face Bboxes     |            |              |
  |                                         +---------+---------+            v              |
  |                                                   |           +----------------------+  |
  |                                                   |           | Arm Ethos-U55 NPU    |  |
  |                                                   |           | - YOLO-Fastest (8ms) |  |
  |                                                   |           | - SRAM1 Arena (442KB)|  |
  |                                                   |           +----------+-----------+  |
  |                                                   |                      |              |
  |                                                   |                      v              |
  |                                                   |           +----------------------+  |
  |                                                   |           | Post-Process (NMS)   |  |
  |                                                   |           +----------+-----------+  |
  |                                                   |                      |              |
  |                                                   v                      v              |
  |                                         +--------------------------------------------+  |
  |                                         | CPU HUD Overlay Engine                     |  |
  |                                         | - Green Box & Metrics (AI Time, Score)     |  |
  |                                         | - Sync: GPU Flush -> CPU Draw -> D-Cache   |  |
  |                                         +---------------------+----------------------+  |
  |                                                               |                         |
  |                                                               v                         |
  |  +--------------------+     Parallel RGB888     +------------------------------------+  |
  |  | 1024x600 TFT-LCD   | <====================== | GLCDC Display Controller           |  |
  |  | (Centered 800x600) |                         | Triple FB in 64MB SDRAM (0x68000000|  |
  |  +--------------------+                         +------------------------------------+  |
  +-----------------------------------------------------------------------------------------+
```

---

## 4. メモリフットプリント (最新ビルド統計)

| メモリ領域 | 使用量 (Used Size) | 総容量 (Region Size) | 使用率 (%age) | 役割・格納データ |
| :--- | :--- | :--- | :--- | :--- |
| **FLASH (ROM)** | **576,684 B** (~563 KB) | 768 KB | **73.33%** | プログラムコード、Ethos-U ドライバ、YOLO モデル重み、フォント |
| **RAM (内蔵 SRAM)** | **836,508 B** (~817 KB) | 1,024 KB (1 MB) | **79.78%** | 各スレッドスタック、BSS、システムヒープ、VIN DMA バッファ |
| **内蔵 SRAM1** | **479,232 B** (468 KB) | 640 KB | **74.88%** | NPU テンソルアリーナ (`sub_0000_arena`: 442 KB) + モデル入力 |
| **外部 SDRAM** | 動的確保 (~3.6 MB) | 64 MB | ~5.6% | 3面フレームバッファ (1024×600×2B × 3) |

---

## 5. ハードウェア要件およびピン配置

### 必要機材
- **マイコンボード**: Renesas EK-RA8P1 (R7KA8P1KFLCAC, Arm Cortex-M85)
- **カメラモジュール**: OmniVision OV5640 CMOS センサ（2-lane MIPI CSI-2 接続）
- **液晶ディスプレイ**: RTKLCDPAR1S00001BE (1024×600 パラレル RGB888 TFT-LCD)
- **外部メモリ**: オンボード 64MB SDRAM (`0x68000000`)
- **デバッガ**: オンボード SEGGER J-Link OB

### 主要ピン配置
| 信号名 | マイコンピン / ポート | 機能 | 説明 |
|---|---|---|---|
| `CAM_XCLK` | P501 | GPT12 (GTIOC12A) | カメラ用 24.0 MHz クロック出力 |
| `MIPI_IF_EN` | P108 | GPIO 出力 | MIPI マルチプレクサイネーブル (Active Low) |
| `CAM_RST` | P709 | GPIO 出力 | OV5640 ハードウェアリセット (Active Low) |
| `CAM_SDA` | P511 | SCI9 (I2C) | カメラ制御 I2C データ |
| `CAM_SCL` | P512 | SCI9 (I2C) | カメラ制御 I2C クロック |
| `DISP_BLEN` | P514 | GPIO 出力 | 液晶バックライトイネーブル (Active High) |
| `DISP_RST` | P606 | GPIO 出力 | 液晶パネルハードウェアリセット (Active Low) |
| `LCDC_CLK` | P515 | GLCDC | 液晶ピクセルクロック (40 MHz) |
| `LCDC_TCON0..2`| P806, P805, P807 | GLCDC | VSYNC, HSYNC, DE |
| `LCDC_DATA` | 各種ポート | GLCDC | 24-bit パラレル RGB888 ビデオバス |

---

## 6. ビルドおよび書き込み手順

### 付属スクリプトを使用する場合（推奨）
```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\yoloface_npu

:: ビルド
build.bat

:: 実機フラッシュ書き込み (SEGGER J-Link)
flash.bat
```

### 手動で west コマンドを実行する場合
```cmd
set "PYTHONUTF8=1"
call "%USERPROFILE%\zephyrproject\.venv\Scripts\activate.bat"
cd /d "%USERPROFILE%\zephyrproject"

:: ビルド
west build -b ek_ra8p1/r7ka8p1kflcac/cm85 -s "C:\Users\ioten\tron_zephyr\yoloface_npu" -d "%USERPROFILE%\zephyrproject\build_yoloface_npu" -p always

:: 書き込み
west flash -d "%USERPROFILE%\zephyrproject\build_yoloface_npu" --speed 4000
```

---

## 7. 実機シリアルログ例 (COMポート 115200bps, 8N1)

```text
*** Booting Zephyr OS build v4.4.0 ***

=== Zephyr YOLO Face Detection (Ethos-U55 NPU + MIPI Camera) Start ===
Testing physical SDRAM at address 0x68000000...
SDRAM verification SUCCESS!
=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===
Clearing SDRAM framebuffers to black...
Initializing LCD (GLCDC)...
LCD Backlight enabled.
Initializing D/AVE 2D Graphics Engine...
[Camera Init] Product ID High: 0x56, Low: 0x40
SUCCESS: Camera capture started.
Initializing Arm Ethos-U55 NPU Subsystem...
SUCCESS: Arm Ethos-U55 NPU driver initialized successfully!
Entering Real-time Camera Display Loop (Bilinear Upscale 320x240 -> 800x600)...
=== AI Inference Thread Started (NPU-accelerated YOLO-Fastest) ===
[VIN CB] First frame captured successfully!
[AI] Face detected: 2 (Score: 63%, Inf: 8 ms, MaxObj: B0=36.9%, B1=63.6%)
[AI] Face detected: 1 (Score: 59%, Inf: 8 ms, MaxObj: B0=10.5%, B1=59.2%)
FPS: 48 | CamBuf: 0x22000A00 | Faces: 1 | Score: 59% | AI: 8 ms
[AI] Face detected: 1 (Score: 67%, Inf: 8 ms, MaxObj: B0=18.6%, B1=67.7%)
[AI] Face detected: 2 (Score: 71%, Inf: 8 ms, MaxObj: B0=53.3%, B1=71.6%)
[AI] Face detected: 1 (Score: 63%, Inf: 8 ms, MaxObj: B0=14.9%, B1=63.6%)
FPS: 48 | CamBuf: 0x22026200 | Faces: 2 | Score: 88% | AI: 8 ms
```

---

## 8. ライセンス情報

- **Zephyr RTOS Kernel & Subsystems**: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **Arm Ethos-U Core Driver & CMSIS-NN**: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **YOLO-Fastest Face Detection Model & Post-Processing**: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **Renesas Flexible Software Package (FSP) Drivers & HAL**: [BSD 3-Clause License](https://opensource.org/licenses/BSD-3-Clause)
- **TES D/AVE 2D (DRW) Graphics Engine**: Renesas RA ファミリ向け TES Electronic Solutions / ルネサス エレクトロニクス社 評価・利用規約に準拠
