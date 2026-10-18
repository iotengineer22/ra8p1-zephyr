# EK-RA8P1 PDM マイク音声認識・Cortex-M85 CPU 推論システム (Zephyr RTOS 版)

本プロジェクトは、μT-Kernel 3.0 上で実装されていたルネサス RA8P1 向け PDM デジタルマイク・音声キーワード検出 CPU 推論プログラム (`tron_pdm_detect_cpu`) を、**Zephyr RTOS** 上へ完全移植したものです。

EK-RA8P1 ボード搭載のオンボード PDM デジタルマイクから 32kHz サンプリング・20bit 精度で音声データを取得し、16kHz・1.0 秒間（16,000 サンプル）の円形バッファへ蓄積。Edge Impulse C++ SDK と CMSIS-DSP による音響特徴量抽出（MFCC）を経て、**Arm Cortex-M85 CPU (480MHz) + CMSIS-NN** による高速 CPU 推論（約 **1.23 ms**）を実行します。

推論結果（5クラス分類：`down`, `left`, `noise`, `right`, `up`）、クラス別確率バーチャート、リアルタイム音声波形（オシロスコープ）、および RMS 音量メーターを、TES D/AVE 2D ハードウェアグラフィックスエンジンを用いて 1024×600 LCD ディスプレイ上に岩のように安定した表示（ブレ・ちらつき・文字被りゼロ）で描画します。

---

## 1. 主な機能と特徴

### 1.1 音声入力 & プリプロセス
- **オンボード PDM デジタルマイク (Unit 0, Channel 2)**:
  - クロックピン: `P812` (MIC_CLK, `pdmclk2`)
  - データピン: `P502` (MIC_DAT, `pdmdat2`)
  - サンプリング周波数: 32 kHz (4次 Sinc フィルター `sincrng=9`, `sincdec=62`)
  - データ形式: 32bit レジスタ右詰め 20bit 符号付き PCM
- **DMAC (Channel 1) バックグラウンド転送**:
  - 1 ブロック（1600 サンプル = 50ms 単位）を CPU 負荷ゼロで自動転送
  - DMAC 割り込みハンドラ (`ELC_EVENT_DMAC1_INT` -> Vector 32) にて、20bit 符号拡張および 16bit 規格化スケーリングを実施
- **16kHz ダウンサンプリング円形バッファ**:
  - 32kHz サンプルを 2 分の 1 に間引き（2サンプルに1回抽出）、16kHz・16,000 サンプル（1.0 秒窓）のリングバッファ `g_audio_buffer` に連続蓄積
  - 16,000 サンプル満了後、即座に推論タスクへ最新音響データを供給

### 1.2 Arm Cortex-M85 CPU (CMSIS-NN) 高速推論
- **CMSIS-NN ハードウェア最適化**:
  - Arm Cortex-M85 コアの Helium (M-Profile Vector Extension: MVE) ベクトル命令および DSP 命令を活用
  - `CONFIG_CMSIS_NN=y` により畳み込み演算（`arm_convolve_wrapper_s8`）、プーリング（`arm_max_pool_s8`）、全結合層（`arm_fully_connected_wrapper_s8`）、ソフトマックス（`arm_softmax_s8`）を最大効率で実行
  - Edge Impulse EON コンパイル済みモデル（`compute_sub_0000.c`）を純粋な CPU コードとして実行
- **推論パフォーマンス実測値**:
  - **CPU ニューラルネットワーク推論時間: 約 1.23 ms (1,232〜1,235 μs)**
  - **DSP 特徴抽出時間 (MFCC 650 特徴): 約 40.8 ms** (CMSIS-DSP 加速)
  - **推論周期**: 250ms (毎秒 4 回の連続リアルタイム判定)
- **分類クラス (5 クラス)**:
  - `down` (下)
  - `left` (左)
  - `noise` (環境雑音 / 無音)
  - `right` (右)
  - `up` (上)

### 1.3 リアルタイム GUI & 可視化 (D/AVE 2D ハードウェア描画 + GLCDC)
- **D/AVE 2D 完全ハードウェア描画**:
  - 全ての UI カード枠、バーチャート、オシロスコープ波形、およびテキストフォント描画（`draw_char_5x7`）を D/AVE 2D ハードウェアディスプレイリストに直接投入。
  - フォント描画関数は垂直ランレングス最適化され、GPU コマンド数を大幅削減。
  - CPU によるフレームバッファ直書きや不安定な D-Cache フラッシュ（`SCB_CleanInvalidateDCache`）を完全に排除し、ハードウェアシーケンスによる順序正しいクリア・描画とキャッシュ競合ゼロを実現。
- **AI 検出結果カード (左上)**:
  - 見出し: `AI KEYWORD DETECT (5-CLASS CPU)`
  - 最尤クラスと確信度を大きく表示 (`DETECTED: up (96%)`)
  - 有効キーワード検出時（確信度 50% 以上かつ `noise` 以外）は**ネオングリーン**、環境雑音時は**ネオンシアン**に自動切り替え
  - CPU 推論レイテンシおよび DSP 処理時間をリアルタイム表示 (`DSP: 40.8 ms | CPU: 1.23 ms`)
  - 累積推論回数とバッファ状態表示 (`INFERENCES: 1234  STATUS: ACTIVE`)
- **クラス別確率バーチャート (右上, 0〜100%)**:
  - 見出し: `CLASS PROBABILITIES (0-100%):`
  - 5 クラス（`down`, `left`, `noise`, `right`, `up`）の確率をリアルタイムなカラープログレスバー（最尤クラスはグリーン、その他はスカイブルー）とパーセンテージ数値（`%3d%%`）で表示
- **ライブ音声オシロスコープ (中央)**:
  - 最新 1024 サンプルの PDM マイク波形をネオングリーンの折れ線（`d2_renderline`）で滑らかに描画
  - ゼロ基準線（Y=382）を中心としたダイナミックレンジ可視化
- **RMS 音量メーター & オーディオステータス (下部)**:
  - 整数平方根アルゴリズムによる実効音量をネオンシアンのレベルバーで表示
  - 最下部にリアルタイム指標（`RMS: <val>  PK-PK: <val>  PDM IRQ: <cnt>  STATUS: NORMAL`）を表示

### 1.4 完全な画面安定性（ぶらつき・ちらつき・白画面・文字被りゼロ）
- `pdm_mic` プロジェクトで確立された高品質表示設計を完全継承：
  1. 出力クロック分周比 12 (PLL2 240MHz ÷ 12 = **20.0 MHz** ピクセルクロック)
  2. GLCDC 端子駆動能力 **High Drive**
  3. 安定した Vsync タイミング (1334 × 635, Hsync 10/300, Vsync 5/30)
  4. 電源投入時バックライト遅延（100ms 安定化待機後に点灯）による突入電圧低下・白画面ハングアップ防止
  5. D/AVE 2D の毎フレーム開始/終了 (`d2_startframe` / `d2_endframe` / `d2_flushframe`) の厳格な整合
  6. SDRAM 外部メモリ トリプルバッファリング (`0x68000000`, `0x68200000`, `0x68400000`)
  7. D/AVE 2D ディスプレイリストブロックサイズ拡張 (`d2_setdlistblocksize(g_d2_handle, 2048)`) によるヒープ断片化・メモリ枯渇防止
  8. カード幅・見出し幅の厳密な座標設計による文字被り（テキストオーバーフロー）の完全防止

---

## 2. ディレクトリ構成

```
pdm_cpu/
├── .gitignore                     # Git 除外設定
├── CMakeLists.txt                 # CMake ビルドスクリプト (CMSIS-DSP, CMSIS-NN, D2D 連携)
├── prj.conf                       # Zephyr Kconfig 設定 (CMSIS-DSP, CMSIS-NN, C++17, GLCDC)
├── app.overlay                    # デバイスツリーオーバーレイ (GLCDC 20MHz, High Drive)
├── build.bat                      # ワンクリックビルドスクリプト
├── flash.bat                      # J-Link ワンクリックフラッシュスクリプト
├── README_ja.md                   # 日本語ドキュメント (本書)
├── README.md                      # 英語ドキュメント
└── src/
    ├── main.cpp                   # メイン処理 (GUI 描画タスク, CPU 推論タスク, モニタタスク)
    ├── font5x7.h                  # 5x7 ビットマップフォントデータ
    ├── dave2d/                    # TES D/AVE 2D グラフィックスドライバ
    │   ├── inc/
    │   └── src/
    ├── drw/                       # Renesas DRW 低レベルラッパードライバ
    ├── pdm/                       # PDM マイク & DMAC ドライバ
    │   ├── pdm_audio.h            # PDM API & 16kHz 円形バッファ定義
    │   ├── pdm_audio.c            # PDM / DMAC 初期化 & コールバック処理
    │   ├── r_pdm.c
    │   └── r_dmac.c
    ├── font/                      # フォントライブラリ (bg_font_18)
    ├── model-parameters/          # Edge Impulse モデルメタデータ & 変数
    │   ├── model_metadata.h
    │   └── model_variables.h
    ├── tflite-model/              # EON コンパイル済み CPU 推論モデル
    │   ├── compute_sub_0000.c     # CMSIS-NN 畳み込み・全結合・ソフトマックス呼び出し
    │   ├── compute_sub_0000.h
    │   ├── kernel_library_int.c   # 量子化・活性化関数ルーチン
    │   ├── kernel_library_int.h
    │   ├── kernel_library_utils.c # カーネルユーティリティ
    │   ├── kernel_library_utils.h
    │   ├── model_io_data.c        # 重み・バイアス定数データ
    │   ├── model_io_data.h
    │   ├── tflite_learn_943529_3_compiled.cpp
    │   ├── tflite_learn_943529_3_compiled.h
    │   └── trained_model_ops_define.h
    └── edge-impulse-sdk/          # Edge Impulse C++ SDK
        ├── classifier/            # 分類エンジン & DSP 実行
        ├── dsp/                   # MFCC / FFT / 音響処理
        └── porting/zephyr/        # Zephyr RTOS 移植層
```

---

## 3. タスク構成とアーキテクチャ

システムは 3 つの協調スレッド（Zephyr スレッド）と 2 つのハードウェア割り込みで動作します。

```
[PDM マイク入力 (32kHz)]
       │
       ▼ (DMAC1 転送: 1600 サンプル / 50ms)
[PDM DMAC ISR (Vector 32)]
       ├─ 20bit -> 16bit 規格化スケーリング
       ├─ RMS 音量 & Peak-to-Peak 算出
       ├─ 1024 サンプル波形履歴更新
       └─ 間引き (/2) -> 16kHz 円形バッファ (16,000 サンプル = 1.0秒)
              │
              ▼ (1.0秒満了後、常時最新)
┌─────────────────────────────────────────────────────────────┐
│ Task 3: AI Inference Thread (優先度 11, Stack 32KB)         │
│   ・周期: 250ms (毎秒 4 回)                                 │
│   ・DSP (MFCC 特徴抽出): 40.8 ms (CMSIS-DSP)                │
│   ・CPU 推論: 1.23 ms (Cortex-M85 + CMSIS-NN ハードウェア加速)│
│   ・確信度・検出ラベル・レイテンシを共有変数へ反映           │
└─────────────────────────────────────────────────────────────┘
              │ (共有ステート)
              ▼
┌─────────────────────────────────────────────────────────────┐
│ Task 1: UI Render Thread (優先度 10, Stack 8KB)             │
│   ・D/AVE 2D ハードウェア全描画 (UIカード, フォント, 波形)  │
│   ・GLCDC Vsync 垂直同期トリプルバッファリング              │
│   ・CPUキャッシュ競合・ちらつきのない高信頼描画             │
└─────────────────────────────────────────────────────────────┘
              │ (バックグラウンド)
              ▼
┌─────────────────────────────────────────────────────────────┐
│ Task 2: System Monitor Thread (優先度 14, Stack 2KB)        │
│   ・5秒ごとに FPS、PDM イベント数、推論回数をシリアル出力   │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. ビルドと書き込み手順

### 4.1 環境要件
- Windows 10 / 11 64bit
- Zephyr SDK 1.0.1 (ARM ツールチェーン)
- Python 3.12+ (Zephyr 仮想環境: `C:\Users\ioten\zephyrproject\.venv`)
- SEGGER J-Link ソフトウェア (V9.42 以上)

### 4.2 ビルド
プロジェクトルートで `build.bat` を実行します：
```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\pdm_cpu
build.bat
```
CMake が自動的に CMSIS-DSP、CMSIS-NN、D/AVE 2D、PDM ドライバ、Edge Impulse SDK を検出し、Ninja によりビルドが行われます。
ビルドが成功すると、`build\zephyr\zephyr.elf` および `zephyr.hex` が生成されます。

### 4.3 フラッシュ書き込み
EK-RA8P1 ボードを J-Link USB ポート（J10）経由で PC に接続し、`flash.bat` を実行します：
```cmd
flash.bat
```
書き込み完了後、マイコンが自動リセットされ、LCD パネルにグラフィックスが表示され、CPU 音声認識が直ちに開始されます。

---

## 5. 動作確認とシリアルログ

J-Link CDC UART ポート（115200 bps, 8N1）を開くと、リアルタイムの推論ログを確認できます：

```text
Starting Zephyr PDM Microphone + Cortex-M85 CPU Voice Classifier...
Initializing LCD (GLCDC)...
Initializing D/AVE 2D Graphics Engine...
LCD Backlight enabled (safe power-on sequencing complete).
Starting D/AVE 2D Rendering Loop (PDM Audio + CPU Visualizer)...
=== Starting Cortex-M85 CPU Voice Detection Thread (CMSIS-NN)... ===
[AI-CPU] DETECTED: noise (86%) | DSP: 40.718 ms | CPU: 1.235 ms
[AI-CPU] DETECTED: noise (62%) | DSP: 40.802 ms | CPU: 1.235 ms
[AI-CPU] DETECTED: noise (96%) | DSP: 40.896 ms | CPU: 1.233 ms
FPS: 20 | PDM Events: 89 | RMS: 2 | Pk-Pk: 10 | AI Inferences: 11
[AI-CPU] DETECTED: noise (72%) | DSP: 40.803 ms | CPU: 1.233 ms
[AI-CPU] DETECTED: noise (75%) | DSP: 40.679 ms | CPU: 1.233 ms
[AI-CPU] DETECTED: noise (85%) | DSP: 40.837 ms | CPU: 1.232 ms
[AI-CPU] DETECTED: noise (82%) | DSP: 40.826 ms | CPU: 1.232 ms
[AI-CPU] DETECTED: noise (89%) | DSP: 40.921 ms | CPU: 1.233 ms
[AI-CPU] DETECTED: noise (95%) | DSP: 40.836 ms | CPU: 1.233 ms
[AI-CPU] DETECTED: noise (97%) | DSP: 40.852 ms | CPU: 1.232 ms
[AI-CPU] DETECTED: noise (88%) | DSP: 40.629 ms | CPU: 1.233 ms
[AI-CPU] DETECTED: noise (99%) | DSP: 40.860 ms | CPU: 1.233 ms
```

- PDM マイクに向かって「アップ」「ダウン」「レフト」「ライト」等と発声すると、該当クラスの確信度が跳ね上がり、画面上のバナーがネオングリーンに点灯します。
- Cortex-M85 (480MHz) の Helium ベクトル拡張と CMSIS-NN の最適化により、**わずか 1.23 ms** でニューラルネットワーク推論を完了します。

---

## 6. NPU 版 (`pdm_npu`) と CPU 版 (`pdm_cpu`) の比較

| 項目 | NPU 推論版 (`pdm_npu`) | CPU 推論版 (`pdm_cpu`) |
|:---|:---|:---|
| **推論エンジン** | Arm Ethos-U55 NPU (240MHz) | Cortex-M85 CPU (480MHz) + CMSIS-NN |
| **NN 推論時間** | **約 0.25 ms (247〜250 μs)** | **約 1.23 ms (1,232〜1,235 μs)** |
| **DSP MFCC 抽出時間** | 約 32.5 ms | 約 40.8 ms |
| **推論周期** | 250 ms (4 FPS) | 250 ms (4 FPS) |
| **描画エンジン** | TES D/AVE 2D 完全ハードウェア描画 | TES D/AVE 2D 完全ハードウェア描画 |
| **LCD フレームレート** | 20〜23 FPS (岩のように安定) | 20〜23 FPS (岩のように安定) |
| **ハードウェア依存度** | NPU ドライバ (`rm_ethosu`, `ethosu_driver`) | 標準 CMSIS-NN (`CONFIG_CMSIS_NN=y`) |
| **ROM 使用量 (Flash)** | 約 109 KB (13.9%) | 約 125 KB (15.9%) |
| **RAM 使用量 (SRAM)** | 約 249 KB (23.8%) | 約 247 KB (23.6%) |
