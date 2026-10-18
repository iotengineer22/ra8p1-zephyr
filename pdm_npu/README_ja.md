# EK-RA8P1 PDM マイク音声認識・Arm Ethos-U55 NPU 推論システム (Zephyr RTOS 版)

本プロジェクトは、μT-Kernel 3.0 上で実装されていたルネサス RA8P1 向け PDM デジタルマイク・音声キーワード検出プログラム (`tron_pdm_detect`) を、**Zephyr RTOS** 上へ完全移植したものです。

EK-RA8P1 ボード搭載のオンボード PDM デジタルマイクから 32kHz サンプリング・20bit 精度で音声データを取得し、16kHz・1.0 秒間（16,000 サンプル）の円形バッファへ蓄積。Edge Impulse C++ SDK と CMSIS-DSP による音響特徴量抽出（MFCC）を経て、**内蔵 Arm Ethos-U55 NPU（マイクロ NPU）** によるハードウェアアクセラレーション推論（約 **0.25ms / 250μs**）を実行します。

推論結果（5クラス分類：`down`, `left`, `noise`, `right`, `up`）、クラス別確率バーチャート、リアルタイム音声波形（オシロスコープ）、および RMS 音量メーターを、TES D/AVE 2D ハードウェアグラフィックスエンジンを用いて 1024×600 LCD ディスプレイ上に岩のように安定した表示（ブレ・ちらつき・白画面ゼロ）で描画します。

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
  - 32kHz サンプルを 2 分の 1 に間引き間引き（2サンプルに1回抽出）、16kHz・16,000 サンプル（1.0 秒窓）のリングバッファ `g_audio_buffer` に連続蓄積
  - 16,000 サンプル満了後、即座に推論タスクへ最新音響データを供給

### 1.2 Arm Ethos-U55 NPU 超高速推論
- **Ethos-U55 マイクロ NPU 連携**:
  - ルネサス FSP Ethos-U ドライバ (`RM_ETHOSU`) および Arm Core ドライバ (`ethosu_driver`) を Zephyr RTOS 上に統合
  - NPU 割り込み (`ELC_EVENT_NPU_IRQ`) を NVIC Vector 20 に接続
  - コマンドストリーム (`sub_0001_command_stream.c`) および重みデータ (`sub_0001_model_data.c`) を NPU ハードウェアが直接実行
- **推論パフォーマンス実測値**:
  - **NPU ニューラルネットワーク推論時間: 約 0.25 ms (249〜252 μs)**
  - **DSP 特徴抽出時間 (MFCC 650 特徴): 約 32.5 ms** (CMSIS-DSP 加速)
  - **推論周期**: 250ms (毎秒 4 回の連続リアルタイム判定)
- **分類クラス (5 クラス)**:
  - `down` (下)
  - `left` (左)
  - `noise` (環境雑音 / 無音)
  - `right` (右)
  - `up` (上)

### 1.3 リアルタイム GUI & 可視化 (D/AVE 2D ハードウェア描画 + GLCDC)
- **D/AVE 2D 完全ハードウェア描画**:
  - 全ての UI カード枠、バーチャート、オシロスコープ波形、およびテキストフォント描画（`font5x7`）を D/AVE 2D のハードウェアディスプレイリストに直接投入。
  - フォント描画関数 `draw_char_5x7` は垂直ランレングス最適化（連続ドットを1つの描画命令に統合）され、GPU コマンド数を大幅に削減。
  - CPU によるフレームバッファ直書きや不安定な D-Cache フラッシュ（`SCB_CleanInvalidateDCache`）を完全に排除し、ハードウェアシーケンスによる順序正しいクリア・描画とキャッシュ競合ゼロを実現。
- **AI 検出結果カード (左上)**:
  - 見出し: `AI KEYWORD DETECT (5-CLASS)`
  - 最尤クラスと確信度を大きく表示 (`DETECTED: up (96%)`)
  - 有効キーワード検出時（確信度 50% 以上かつ `noise` 以外）は**ネオングリーン**、環境雑音時は**ネオンシアン**に自動切り替え
  - NPU 推論レイテンシおよび DSP 処理時間をリアルタイム表示 (`DSP: 32.5 ms | NN: 0.25 ms`)
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
pdm_npu/
├── .gitignore                     # Git 除外設定
├── CMakeLists.txt                 # CMake ビルドスクリプト (CMSIS-DSP, NPU, D2D 連携)
├── prj.conf                       # Zephyr Kconfig 設定 (CMSIS-DSP, C++17, GLCDC)
├── app.overlay                    # デバイスツリーオーバーレイ (GLCDC 20MHz, High Drive)
├── build.bat                      # ワンクリックビルドスクリプト
├── flash.bat                      # J-Link ワンクリックフラッシュスクリプト
├── README_ja.md                   # 日本語ドキュメント (本書)
├── README.md                      # 英語ドキュメント
└── src/
    ├── main.cpp                   # メイン処理 (GUI 描画タスク, NPU 推論タスク, モニタタスク)
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
    ├── npu/                       # Arm Ethos-U55 NPU ドライバ
    │   ├── ethosu_irq.h           # NPU 割り込み & 初期化ヘッダ
    │   ├── ethosu_irq.c           # NPU NVIC 接続 (Vector 20) & ハンドラ
    │   ├── rm_ethosu.c            # Renesas FSP Ethos-U ドライバ
    │   ├── ethosu_driver.c        # Arm Ethos-U Core ドライバ
    │   ├── ethosu_device_u55_u65.c
    │   └── ethosu_pmu.c
    ├── ruhmi/                     # RUHMI 変換済み Ethos-U コマンドストリーム
    │   ├── compute_sub_0000.c     # CPU 入力処理
    │   ├── model.c                # モデル実行ディスパッチャ
    │   ├── sub_0001_command_stream.c # Ethos-U55 NPU コマンドバイナリ
    │   ├── sub_0001_invoke.c      # Ethos-U 呼び出しインタフェース
    │   ├── sub_0001_model_data.c  # NPU 重みバイナリ
    │   └── wrapper.h              # モデルラッパーヘッダ
    ├── model-parameters/          # Edge Impulse モデルメタデータ & 変数
    │   ├── model_metadata.h
    │   └── model_variables.h
    ├── tflite-model/              # EON コンパイル済みモデル
    │   └── tflite_learn_943529_3_compiled.cpp
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
│   ・DSP (MFCC 特徴抽出): 32.5 ms (CMSIS-DSP)                │
│   ・NPU 推論: 0.25 ms (Arm Ethos-U55 ハードウェア加速)     │
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
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\pdm_npu
build.bat
```
CMake が自動的に CMSIS-DSP、D/AVE 2D、PDM ドライバ、Ethos-U NPU ドライバ、Edge Impulse SDK を検出し、Ninja によりビルドが行われます。
ビルドが成功すると、`build\zephyr\zephyr.elf` および `zephyr.hex` が生成されます。

### 4.3 フラッシュ書き込み
EK-RA8P1 ボードを J-Link USB ポート（J10）経由で PC に接続し、`flash.bat` を実行します：
```cmd
flash.bat
```
書き込み完了後、マイコンが自動リセットされ、LCD パネルにグラフィックスが表示され、NPU 音声認識が直ちに開始されます。

---

## 5. 動作確認とシリアルログ

J-Link CDC UART ポート（115200 bps, 8N1）を開くと、リアルタイムの推論ログを確認できます：

```text
Starting Zephyr PDM Microphone + Arm Ethos-U55 NPU Voice Classifier...
Initializing LCD (GLCDC)...
Initializing D/AVE 2D Graphics Engine...
LCD Backlight enabled (safe power-on sequencing complete).
Starting D/AVE 2D Rendering Loop (PDM Audio + NPU Visualizer)...
=== Starting Arm Ethos-U55 NPU Voice Detection Thread... ===
SUCCESS: Arm Ethos-U55 NPU driver initialized successfully!
[AI] DETECTED: noise (88%) | DSP: 32.433 ms | NN: 0.251 ms
[AI] DETECTED: noise (73%) | DSP: 32.435 ms | NN: 0.252 ms
[AI] DETECTED: left (44%)  | DSP: 32.593 ms | NN: 0.252 ms
[AI] DETECTED: down (33%)  | DSP: 32.592 ms | NN: 0.252 ms
[AI] DETECTED: up (64%)    | DSP: 32.598 ms | NN: 0.249 ms
FPS: 23 | PDM Events: 821 | RMS: 2 | Pk-Pk: 17 | AI Inferences: 131
```

- PDM マイクに向かって「アップ」「ダウン」「レフト」「ライト」等と発声すると、該当クラスの確信度が跳ね上がり、画面上のバナーがネオングリーンに点灯します。
- NPU 推論時間は **0.25 ms (約 250 マイクロ秒)** と極めて高速であり、Cortex-M85 の CPU 負荷をほとんど消費せずに常時音声認識を動作させることができます。
