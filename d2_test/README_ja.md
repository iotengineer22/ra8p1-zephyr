[English Version (README.md)](README.md)

# Zephyr D/AVE 2D グラフィックス描画テスト (d2_test)

## 概要
本プログラムは、μT-Kernel 3.0 上で動作していた `tron_d2_test` を **Zephyr RTOS** ネイティブ環境に移植したグラフィックス描画プログラムです。
ルネサス RA8 シリーズ（EK-RA8P1, Arm Cortex-M85 @ 1 GHz）の 2D グラフィックスアクセラレータ「D/AVE 2D (Dave2D)」および GLCDC (Global LCD Controller) を用いて、外部 SDRAM 上のトリプルバッファを用いたティアリングのない「バウンシングボール」アニメーションを描画・表示します。

## 主な機能と最適化
1. **外部 SDRAM 初期化＆セルフテスト**:
   - Zephyr の `CONFIG_MEMC_RENESAS_RA_SDRAM=y` により外部 SDRAM (64MB: `0x68000000`) を自動初期化。
   - 物理メモリの読み書き検証セルフテストを実行。
2. **GLCDC & 1024x600 パラレル液晶駆動**:
   - Zephyr 公式の `rtklcdpar1s00001be` 定義に基づく GLCDC ピン・タイミング・バックライト制御 (`P514`)。
   - 堅牢な液晶ハードウェアリセットシーケンス（500ms High -> 200ms Low -> 500ms High）により、起動時の液晶同期外れを防止。
   - `CONFIG_RENESAS_RA_GLCDC_FB_NUM=0` によるアプリケーション主導のトリプルバッファリング。
   - Vblank (垂直同期) 割り込みに完全同期した画面切り替え。
3. **D/AVE 2D (DRW) ハードウェアアクセラレーション**:
   - Dave2D 描画ライブラリをプロジェクト内に統合。
   - キャッシュコヒーレンシの完全保証 (`SCB_CleanInvalidateDCache`)。
   - 描画完了割り込みは Zephyr セマフォ (`k_sem`) でブロック待機。
4. **SDRAMバス帯域競合の根絶（ダーティ矩形消去方式）**:
   - 毎フレーム 1.2MB の SDRAM 全画面クリア（`d2_clear`）を廃止し、3フレーム前のボール座標のみを背景色で消去するトリプルバッファ差分描画を導入。
   - SDRAM 書き込み量を 1.2MB $\rightarrow$ 約50KB（1/24以下）に激減させ、GLCDC の DMA FIFO アンダーフローによる黒帯発生を根本的に排除。
   - 安定した **48〜49 FPS** の極めて滑らかなアニメーションを実現。
5. **AXI バス調停設定**:
   - `R_BUS->BUSS[i].CNT = 0x0013`（Fixed Priority, Group 3 = GLCDC）を設定し、表示コントローラのバス帯域を最優先化。
6. **カメラ (OV5640) 接続確認**:
   - `CAMERA_RESET` (P709) トグル後、I2C 経由で OV5640 の Product ID (`0x5640`) の読み出し確認。
7. **マルチスレッド構成**:
   - `task_1` (描画スレッド, スタック 32KB): ペリフェラル初期化、SDRAM テスト、カメラ確認、Dave2D アニメーション描画ループ。
   - `task_2` (周期監視スレッド, スタック 1KB): 7秒ごとに動作ログを出力。

## ハードウェア構成
* **評価ボード**: Renesas EK-RA8P1 (R7KA8P1KFLCAC, Cortex-M85)
* **ディスプレイ**: 1024x600 TFT-LCD (RTKLCDPAR1S00001BE, パラレル RGB888)
* **外部 SDRAM**: 64MB SDRAM (`0x68000000`)
* **主要ピン配置**:
  * LCD バックライト: `P514`
  * LCD リセット: `P606`
  * カメラ リセット: `P709`
  * カメラ I2C: IIC1 (`SDA`: P511, `SCL`: P512)

## ビルド＆書き込み方法

### 付属スクリプトを使用
```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\d2_test

:: ビルド
build.bat

:: 実機へのフラッシュ書き込み (SEGGER J-Link)
flash.bat
```

### 手動で west コマンドを実行
```cmd
set "PYTHONUTF8=1"
call "%USERPROFILE%\zephyrproject\.venv\Scripts\activate.bat"
cd /d "%USERPROFILE%\zephyrproject"
west build -b ek_ra8p1/r7ka8p1kflcac/cm85 -s "C:\Users\ioten\tron_zephyr\d2_test" -d "%USERPROFILE%\zephyrproject\build_d2_test" -p always
west flash -d "%USERPROFILE%\zephyrproject\build_d2_test"
```

## 動作ログ例 (COMポート 115200bps)
```text
*** Booting Zephyr OS build v4.4.0-12700-g8dafb9a897da ***
Starting Zephyr Dave2D Test Application...

=== Zephyr Camera I2C & LCD D2D Test Start ===
task 2 running...
Testing physical SDRAM at address 0x68000000...
SDRAM verification SUCCESS!
=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===
Resetting Camera (CAMERA_RESET -> P709)...
Reading OV5640 Product ID registers via I2C...
Camera I2C communication returned -5 (camera module may be disconnected)
Clearing SDRAM framebuffers to black...
Setting Bus Slave Arbitration to Fixed Priority (Group 3)...
Initializing LCD (GLCDC)...
LCD Backlight enabled.
Initializing D/AVE 2D Graphics Engine...
Starting D/AVE 2D Rendering Loop (SDRAM Triple Buffer Bouncing Ball)...
FPS: 49 (frame time: 1010 ms)
FPS: 48 (frame time: 1019 ms)
FPS: 49 (frame time: 1011 ms)
task 2 running...
FPS: 48 (frame time: 1018 ms)
```
