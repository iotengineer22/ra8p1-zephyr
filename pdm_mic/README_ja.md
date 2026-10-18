# EK-RA8P1 PDM マイク・リアルタイム波形オシロスコープ (Zephyr RTOS 版)

本プロジェクトは、μT-Kernel 3.0 上で実装されていたルネサス RA8P1 向け PDM デジタルマイク・リアルタイム音響可視化プログラム (`tron_pdm_d2_test`) を、**Zephyr RTOS** 上へ完全移植したものです。

EK-RA8P1 ボード搭載のオンボード PDM デジタルマイクから 32kHz サンプリング・20bit 精度で音響波形を取得し、DMAC 転送とハードウェア 2D グラフィックスエンジン (D/AVE 2D / DRW) を用いて、1024×600 LCD ディスプレイ上にネオングリーンのオシロスコープ波形およびネオンシアンの RMS 音量メーターを高速描画（約45〜50 FPS）します。

---

## 1. 主な機能と特徴

- **PDM デジタルマイク入力 (Unit 0, Channel 2)**:
  - クロックピン: `P812` (MIC_CLK, `pdmclk2`)
  - データピン: `P502` (MIC_DAT, `pdmdat2`)
  - サンプリング周波数: 32 kHz (理論値 32,000 Hz, 実効値 32,258 Hz)
  - フィルター構成: 4次 Sinc フィルター (`sincrng = 9`, `sincdec = 62`)、ハイパスフィルター (HPF)、ローパスフィルター (LPF)、補償フィルター
  - データ形式: 32bit レジスタ右詰め 20bit 符号付き PCM
- **DMAC (Channel 1) 連動バックグラウンド転送**:
  - ELC イベント `ELC_EVENT_PDM_DAT2` (0x0C1) により、CPU 介在なしに PDM FIFO からメモリへ 1 ブロック（1600 サンプル = 50ms 単位）を自動転送
  - DMAC 完了割り込み (`ELC_EVENT_DMAC1_INT` = 0x041) を ICU 経由で NVIC Vector 32 に接続
  - PDM エラー割り込み (`ELC_EVENT_PDM_ERR2` = 0x0C5) を NVIC Vector 33 に接続
  - ダブルバッファリング構成により、オーディオドロップのない連続サンプリングを実現
- **リアルタイム音響 DSP 処理**:
  - 20bit 符号拡張 (`(val << 12) >> 12`)
  - 16bit 規格化スケーリング (`>> 4` による -32768〜+32767 レンジへの変換)
  - 高速整数平方根アルゴリズム (`integer_sqrt`) による実効音量 (RMS) 算出
  - Peak-to-Peak 振幅算出
  - 1024 サンプルの波形履歴バッファ (`g_waveform_history[1024]`)
- **TES D/AVE 2D ハードウェア描画エンジン (DRW)**:
  - 1024×600 RGB565 画面へのオシロスコープ描画 (ネオングリーン: `0xFF00FF66`)
  - RMS 音量レベルバー描画 (ネオンシアン: `0xFF00E5FF`)
  - 画面全体の毎フレームクリアを廃止し、更新矩形のみを部分クリアする方式により外部 SDRAM バスの帯域消費を抑制
  - 3バッファ全てに外枠・タイトルをプリレンダリング済み
- **GLCDC (液晶コントローラ) & SDRAM トリプルバッファリング**:
  - 外部 SDRAM (`0x68000000`, `0x68200000`, `0x68400000`) に 3 面分のフレームバッファを確保
  - Vblank 垂直同期（`display_write`）によるテアリング・フリッカーのない滑らかな表示更新
  - AXI バス調停器 (Arbitration) を固定優先度 Group 3 (`R_BUS->BUSS[i].CNT = 0x0013`) に設定し、GLCDC の最優先帯域を確保
- **CPU 5×7 ビットマップフォント表示**:
  - D/AVE 2D のコマンドバッファ溢れを防止するため、ステータス文字列 (`PDM STATUS`, `IRQ COUNT`, `RMS VOLUME`, `PEAK-TO-PEAK`) は CPU 直接描画
  - 描画領域（Y: 90〜145）のみをピンポイントでキャッシュクリーン (`SCB_CleanInvalidateDCache_by_Addr`)

---

## 2. ディレクトリ構成

```
pdm_mic/
├── .gitignore             # ビルド生成物等の除外設定
├── CMakeLists.txt         # ビルド定義ファイル
├── prj.conf               # Zephyr Kconfig 設定
├── app.overlay            # デバイスツリーオーバーレイ (GLCDC, DRW, SDRAM, GPIO)
├── build.bat              # ビルド用ワンクリックバッチスクリプト
├── flash.bat              # J-Link フラッシュ書き込みバッチスクリプト
├── README_ja.md           # 日本語ドキュメント (本書)
├── README.md              # 英語ドキュメント
└── src/
    ├── main.cpp           # メイン処理 (SDRAM初期化, D/AVE 2D描画ループ, トリプルバッファ)
    ├── font5x7.h          # 5x7 ビットマップフォントデータ
    ├── dave2d/            # TES D/AVE 2D グラフィックスドライバ
    │   ├── inc/
    │   └── src/
    ├── drw/               # D/AVE 2D 低レベル (DRW) Zephyr ラッパードライバ
    │   ├── r_drw_base.c
    │   ├── r_drw_irq.c
    │   ├── r_drw_memory.c
    │   └── r_drw_cfg.h
    └── pdm/               # ルネサス RA PDM & DMAC ドライバ
        ├── pdm_audio.h    # PDM オーディオサブシステム公開ヘッダ
        ├── pdm_audio.c    # PDM ピン設定, ICU/NVIC割り込み接続, DSP処理, コールバック
        ├── r_pdm.h        # FSP PDM インスタンスヘッダ
        ├── r_pdm_api.h    # FSP PDM API ヘッダ
        ├── r_pdm.c        # FSP PDM ドライバソース
        ├── r_pdm_cfg.h    # PDM コンフィグレーション
        ├── r_dmac.c       # FSP DMAC ドライバソース
        └── r_dmac_cfg.h   # DMAC コンフィグレーション
```

---

## 3. ハードウェア接続 & ピンアサイン

| 機能名 | ピン番号 | ポート設定 | 説明 |
|:---|:---|:---|:---|
| **MIC_CLK** | P812 | `IOPORT_PERIPHERAL_PDM` (PSEL 0x1B) | PDM クロック出力 (`pdmclk2`) |
| **MIC_DAT** | P502 | `IOPORT_PERIPHERAL_PDM` (PSEL 0x1B) | PDM データ入力 (`pdmdat2`) |
| **DISP_BLEN** | P514 | GPIO 出力 High | LCD バックライト有効化 |
| **DISP_RST** | P606 | GPIO 出力 (リセットパルス) | LCD リセット制御 |
| **CAMERA_RESET** | P709 | GPIO 出力 High | カメラリセット解除 (互換シーケンス用) |
| **LCD RGB** | 各種 | `IOPORT_PERIPHERAL_LCD_GRAPHICS` | 1024×600 パラレル RGB バス |

---

## 4. ビルド & 書き込み手順

### 必要環境
- OS: Windows 10 / 11
- Zephyr SDK: v1.0.1 (`C:\Users\ioten\zephyr-sdk-1.0.1`)
- Zephyr Workspace: `C:\Users\ioten\zephyrproject`
- SEGGER J-Link: v9.42 以降

### ビルド実行
コマンドプロンプトまたは PowerShell で下記を実行します：
```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\pdm_mic
build.bat
```
成功すると `build\zephyr\zephyr.elf` および `zephyr.bin` / `zephyr.hex` が生成されます。

### 実機書き込み (Flash)
EK-RA8P1 ボードを USB (J-Link OB ポート) に接続した状態で下記を実行します：
```cmd
flash.bat
```
書き込み完了後、ボードが自動リセットされ、LCD パネルにオシロスコープ波形が表示されます。

---

## 5. 動作確認 & シリアルモニタ

シリアル通信ターミナル (COM8, 115200 bps, 8N1) を開くと、リアルタイムでステータスが出力されます：

```text
=== Zephyr PDM Microphone Visualizer Test Start ===
Testing physical SDRAM at address 0x68000000...
SDRAM verification SUCCESS!
=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===
Resetting Camera (CAMERA_RESET -> P709)...
Reading OV5640 Product ID registers via I2C...
Product ID Read: H = 0x56, L = 0x40
SUCCESS: Camera connection verified! (OV5640 detected)
Clearing SDRAM framebuffers to black...
Initializing LCD (GLCDC)...
LCD Backlight enabled.
Initializing D/AVE 2D Graphics Engine...
Pre-rendering background and static borders to all 3 framebuffers...
Configuring PDM Pins (P502: MIC_DAT, P812: MIC_CLK)...
Connecting DMAC1 (IRQ 32) and PDM_ERR2 (IRQ 33) to ICU & NVIC...
Opening PDM driver (g_pdm0)...
PDM driver opened. Waiting for sinc filter settling (60ms)...
Starting PDM capture with DMAC (1600 samples / 50ms frames)...
PDM capture started successfully!
Starting D/AVE 2D Rendering Loop (PDM Audio Visualizer)...
Loop 0: PDM Events: 1, RMS: 0, Pk-Pk: 3, DrawBuf: 0
  Raw Samples: 0x000FFFFE 0x000FFFFF 0x00000001 0x00000000
...
Loop 100: PDM Events: 44, RMS: 2, Pk-Pk: 7, DrawBuf: 1
  Raw Samples: 0x00000005 0x0000000A 0x00000004 0x00000014
```

マイクに向かって声を出したり手を叩いたりすると、画面中央の緑色波形がダイナミックに振幅し、下部のシアン色音量バーが即座に反応します。

---

## 6. ディスプレイ安定化と画面ぶらつき・チラつき解消技術

本移植において直面した「画面の定期的な上下のぶらつき（揺れ）」および「USB抜き差し時の画面真っ白化」に対し、以下のハードウェア・ドライバ層の恒久対策を実装しています。

### ① パネルピクセルクロックの適正化 (20 MHz)
- **課題**: Zephyr の PLL2R 設定（480 MHz）に対し、従来の分周比 6 ではピクセルクロックが **40 MHz**（Tron の 2 倍）となり、GLCDC の SDRAM 読み出し帯域が跳ね上がって FIFO アンダーフローや垂直同期外れを誘発していました。
- **対策**: `app.overlay` の `output-clock-divisor` を **`12`** に変更（$240\text{ MHz} \div 12 = \mathbf{20\text{ MHz}}$）。Tron と完全同一のクロックで駆動し、SDRAM バス競合を完全に解消しました。

### ② 全 LCD ピンの High Drive 強度設定 (`drive-strength = "high"`)
- **課題**: デバイスツリーで駆動能力が未指定だったためデフォルトの `low` で動作し、配線容量によりクロックおよび VSYNC/HSYNC/DE 信号のエッジが鈍り、パネルコントローラが定期的に 1 ライン分の誤認識（上下のぶらつき）を起こしていました。
- **対策**: `app.overlay` の `glcdc_default` グループに `drive-strength = "high";` を追加し、全 LCD 信号線を最大駆動能力でスルーレート向上させ、ジッターを物理的に排除しました。

### ③ D/AVE 2D 描画パイプラインの正規化
- **課題**: 描画ループ末尾に余分な `d2_startframe` が混入していたため、1 フレームで 2 回のコマンドリストスワップが発生し、バッファ位相が狂って空リストの実行や描画遅延が生じていました。
- **対策**: フレーム先頭で `d2_startframe`、末尾で `d2_endframe` $\rightarrow$ `d2_flushframe` を呼ぶ Tron 完全互換の正規シーケンスに統一しました。

### ④ 安全な電源・リセット・バックライト投入シーケンス
- **課題**: USB 抜き差しやリセット時、GLCDC のピクセルクロックやデータ出力が安定する前にバックライト（P514）を点灯すると、突入電流やパネル内部チャージポンプ（VGH/VGL）の未充電により画面が真っ白にフリーズしていました。
- **対策**: 起動時はバックライト OFF のままパネルのリセット（P606）を行い、GLCDC のクロックが開始（初期 `display_write`）されてから **100ms** の安定化時間を確保した後にバックライトを点灯する安全シーケンスを実装しました。
