# YOLO顔検出 CPU推論版 (Zephyr yoloface_cpu)

Renesas EK-RA8P1 ボード上の **Zephyr RTOS** で動作する、**Cortex-M85 CPU** 単体推論の YOLO 顔検出リアルタイムエッジAIアプリケーションです。

---

## 1. プログラム概要

MIPI-CSI2 カメラ (OV5640) から取得した 320x240 RGB565 映像に対して、**TensorFlow Lite for Microcontrollers (TFLM)** および **CMSIS-NN** を用いて Cortex-M85 CPU 上で YOLO-Fastest 顔検出モデルの推論を非同期実行します。

検出された人物の顔位置に緑のバウンディングボックスと確信度 (%) を重ね描きし、画面左上の HUD パネルに **FPS (48 FPS)**、**顔検出数 (Faces)**、**確信度 (Score)**、**AI推論時間 (AI Time: 約4.3秒)** を常時安定表示します。

---

## 2. NPU版 (yoloface_npu) との性能比較 (実測ベンチマーク)

| 比較項目 | NPU版 (`yoloface_npu`) | CPU版 (`yoloface_cpu`) | 差異・効果 |
| :--- | :--- | :--- | :--- |
| **推論ハードウェア** | **Arm Ethos-U55 NPU** (@ 400 MHz) | **Arm Cortex-M85 CPU** (@ 1.0 GHz) | 専用NPU vs 汎用CPU |
| **推論フレームワーク** | Renesas RUHMI / Ethos-U ドライバ | TensorFlow Lite Micro + CMSIS-NN | フラットバッファ直接実行 |
| **AI推論時間 (実測)** | **約 8 ms** | **約 4,298 ms ～ 4,357 ms** | **NPUが約 537倍 高速！** |
| **表示フレームレート** | **48 FPS** (Vblank同期) | **48 FPS** (Vblank同期) | 同等の滑らかさを維持 |
| **表示テアリング・チラつき** | **完全ゼロ** (SDRAMトリプルバッファ) | **完全ゼロ** (SDRAMトリプルバッファ) | 同等 |
| **HUD表示の安定性** | 常時安定表示 (非同期持続バッファ) | 常時安定表示 (3秒ホールド持続バッファ) | 同等 |

### 非同期マルチスレッド設計のメリット
CPU推論では1回の推論に約4.3秒かかりますが、AI推論を優先度 11 のバックグラウンドスレッドで実行し、優先度 10 の描画スレッドと非同期分離しているため、**AI演算中であってもカメラ映像とHUD描画は 48 FPS で一切停止・コマ落ちすることなくリアルタイム動作**します。

---

## 3. ハードウェア／システム構成

* **MCU / 評価ボード**: Renesas EK-RA8P1 (R7KA8P1KFLCAC)
  * CPU: Arm Cortex-M85 (最大 1.0 GHz, 浮動小数点演算ユニット搭載)
  * 内蔵メモリ: 2MB コードFlash, 1MB SRAM
* **外部メモリ**: 64MB SDRAM (`0x68000000`)
  * `0x68000000`: Framebuffer 0 (1024x600 RGB565: 1.2MB)
  * `0x68200000`: Framebuffer 1 (1024x600 RGB565: 1.2MB)
  * `0x68400000`: Framebuffer 2 (1024x600 RGB565: 1.2MB)
  * `0x68800000`: TFLite Micro Tensor Arena (2MB ワーク領域)
* **カメラ**: OmniVision OV5640 (MIPI-CSI2 2レーン接続, 320x240 RGB565 @ 60fps)
* **グラフィックス**:
  * D/AVE 2D (DRW) エンジン: 320x240 -> 800x600 中央バイリニア拡大転送
  * GLCDC: 1024x600 パネル駆動 (Vsync 割り込み同期フリップ)
* **フォント**: `bg_font_18_full` (18ドット美フォント描画)

---

## 4. スレッド・アーキテクチャ

1. **task_disp_entry** (優先度: 10, スタックサイズ: 32KB)
   * メイン描画タスク。Vblank待ちとトリプルバッファローテーションを実行。
   * Dave2D によるカメラ画像拡大描画、最新のAI検出顔枠 (緑線)、左上HUD情報パネルを描画。
   * AIタスクがアイドル状態であれば、セマフォで非同期に推論をキック (`ai_inference_kick()`)。
2. **ai_inference_thread_entry** (優先度: 11, スタックサイズ: 32KB)
   * AI推論タスク。バックグラウンドで実行。
   * 最新のカメラキャプチャバッファから 192x192 グレースケール (int8) に前処理変換。
   * `interpreter->Invoke()` を呼び出し、Cortex-M85 CPU 上で推論を実行。
   * YOLO-Fastest の2つの出力テンソルからバウンディングボックスをデコード、NMS (Non-Maximum Suppression) を実施。
   * 検出結果をスレッドセーフな持続ストアに更新。
3. **task2_entry** (優先度: 10, スタックサイズ: 1KB)
   * 7秒周期のハートビート監視スレッド。

---

## 5. ディレクトリ構成

```
yoloface_cpu/
├── app.overlay               # EK-RA8P1 用デバイスツリーオーバーレイ (SDRAM, GLCDC, I2C, UART)
├── prj.conf                  # Zephyr Kconfig 設定 (C++17, GLCDC, SDRAM, Picolibc)
├── CMakeLists.txt            # アプリケーションビルド定義
├── tflm.cmake                # TFLite Micro (147ファイル) + CMSIS-NN (113ファイル) ビルド定義
├── build.bat                 # Zephyr ビルドスクリプト (west build)
├── flash.bat                 # J-Link フラッシュ書き込みスクリプト (west flash)
├── reset.bat                 # J-Link リセットスクリプト
├── yolo-fastest_192_face_v4.tflite # YOLO-Fastest TFLite モデルバイナリ
├── src/
│   ├── main.cpp              # メインエントリー、GLCDC、トリプルバッファ、D/AVE 2D、HUD描画
│   ├── ai/
│   │   ├── ai_inference.cpp  # TFLM CPU推論パイプライン、前処理・後処理、結果ストア
│   │   ├── ai_inference.h    # AIインターフェース定義
│   │   ├── yolo_fastest_model_data.h # モデルフラットバッファ C配列
│   │   ├── time_counter.c/h  # DWT サイクルカウンター
│   │   ├── face_detection/   # YOLO デコーダー・NMS処理
│   │   └── common/           # 画像処理・算術ユーティリティ
│   ├── tflite-micro/         # TensorFlow Lite for Microcontrollers ソースコード
│   ├── cmsis-nn/             # Arm CMSIS-NN 最適化ライブラリ
│   ├── flatbuffers/          # Flatbuffers ヘッダー
│   ├── gemmlowp/             # 固定小数点演算ヘッダー
│   ├── ruy/                  # 行列演算ヘッダー
│   ├── camera/               # カメラ抽象化レイヤー
│   ├── mipi/                 # Renesas FSP MIPI-CSI2 & VIN ドライバ
│   ├── drw/                  # Renesas FSP D/AVE 2D ドライバ
│   ├── dave2d/               # TES D/AVE 2D 低レベルドライバ
│   └── font/                 # 18ドットフォントビットマップ
```

---

## 6. ビルド＆書き込み手順

### ビルド
コマンドプロンプトで以下を実行します：
```cmd
C:\Users\ioten\tron_zephyr\yoloface_cpu\build.bat
```
成功すると `build\zephyr\zephyr.elf` および `zephyr.hex` が生成されます。

### 実機書き込み
ボードと PC を USB ケーブル (J-Link J10) で接続し、以下を実行します：
```cmd
C:\Users\ioten\tron_zephyr\yoloface_cpu\flash.bat
```

---

## 7. 実機シリアルログ例 (COM8, 115200 baud)

```
=== Zephyr YOLO Face Detection (Cortex-M85 CPU + MIPI Camera) Start ===
Testing physical SDRAM at address 0x68000000...
SDRAM verification SUCCESS!
=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===
Clearing SDRAM framebuffers to black...
Initializing LCD (GLCDC)...
LCD Backlight enabled.
Initializing D/AVE 2D Graphics Engine...
Initializing MIPI-CSI2 Camera (OV5640)...
[Camera Init] Product ID High: 0x56, Low: 0x40
SUCCESS: Camera initialized and capture started.
=== Initializing CPU TFLite Micro Subsystem (YOLO-Fastest) ===
SUCCESS: TensorFlow Lite Micro CPU initialized successfully!
Input tensor size: 36864 bytes, shape: 192 x 192 x 1
Entering Real-time Camera Display Loop (Triple Buffer + HUD)...
[VIN CB] First frame captured successfully!
=== AI Inference Thread Started (CPU-based YOLO-Fastest) ===
FPS: 48 | CamBuf: 0x22000A00 | Faces: 0 | Score: 0% | AI: 0 ms
FPS: 48 | CamBuf: 0x22026200 | Faces: 0 | Score: 0% | AI: 0 ms
[AI CPU #1] Found 0 face(s) in 4310 ms (Pre: 11 ms, Inv: 4298 ms, Post: 1 ms)
task 2 running...
FPS: 48 | CamBuf: 0x2204BA00 | Faces: 0 | Score: 0% | AI: 4298 ms
FPS: 48 | CamBuf: 0x2204BA00 | Faces: 0 | Score: 0% | AI: 4298 ms
[AI CPU #2] Found 2 face(s) in 4369 ms (Pre: 11 ms, Inv: 4357 ms, Post: 1 ms)
  Face 0: Box[110, 109, 48, 55], Conf: 91%
  Face 1: Box[22, 94, 9, 12], Conf: 63%
FPS: 48 | CamBuf: 0x22026200 | Faces: 2 | Score: 91% | AI: 4357 ms
FPS: 48 | CamBuf: 0x22026200 | Faces: 2 | Score: 91% | AI: 4357 ms
task 2 running...
```
