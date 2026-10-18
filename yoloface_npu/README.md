[日本語版 (README_ja.md)](README_ja.md)

# Zephyr YOLO Face Detection System with Arm Ethos-U55 NPU & MIPI-CSI2 Camera
**Target Board**: Renesas EK-RA8P1 (Arm Cortex-M85 @ 1.0 GHz + Arm Ethos-U55 NPU)

---

## 1. Overview

This project implements a high-performance, native **Zephyr RTOS** port of an edge AI vision pipeline integrating the **Arm Ethos-U55 NPU**, **MIPI-CSI2 camera interface**, and **D/AVE 2D (DRW) hardware graphics accelerator** on the **Renesas EK-RA8P1** evaluation kit.

Live video is captured from an OmniVision **OV5640 image sensor** across a 2-lane MIPI-CSI2 link, upscaled in hardware using D/AVE 2D bilinear texture filtering (320×240 $\rightarrow$ 800×600) onto a **1024×600 TFT-LCD panel**, and concurrently processed by the embedded **Arm Ethos-U55 NPU** for ultra-fast (**8 ms**) YOLO-Fastest face detection with zero visual flicker and a rock-solid, permanent on-screen HUD metric overlay.

---

## 2. Key Architectural Features & Highlights

### 1. Hardware-Accelerated 8 ms Inference on Arm Ethos-U55 NPU
- **Ultra-Fast 8 ms Latency**:
  - Executes the YOLO-Fastest model (192×192 int8 1ch grayscale input).
  - The NPU Tensor Arena (~442 KB) and model input buffer are mapped directly to on-chip SRAM1 (`0x22170000`), completely eliminating external SDRAM bus contention and reducing inference time from 91 ms down to **8 ms** (an 11× speedup).
- **Asynchronous Pipeline**:
  - The display thread kicks inference via a lightweight Zephyr semaphore (`g_ai_kick_sem`), allowing continuous inference in the background without dropping display framerates.

### 2. Zero-Copy MIPI-CSI2 & VIN Video Capture
- **2-Lane MIPI-CSI2 Receiver & D-PHY**: Robust high-speed reception of 320×240 YUV422 video streams.
- **Video Input (VIN) DMA Engine**: Writes captured frames directly into 128-byte aligned triple buffers allocated in internal SRAM, preventing frame drops.
- **GPT12 24.0 MHz XCLK Clock**: Timer channel 12 (`P501`) generates the 24 MHz clock required by the sensor's internal PLL.

### 3. Bilinear Upscaling via D/AVE 2D (DRW) GPU
- The D/AVE 2D hardware engine performs real-time **bilinear texture interpolation** (`d2_blitcopy` with `d2_tm_filter`), upscaling the 320×240 camera stream to 800×600 centered on the 1024×600 screen ($X=112, Y=0$).
- Offloads all pixel rasterization from the CPU, maintaining a silky-smooth **48 FPS**.

### 4. Permanent Top-Left HUD Status Panel & Timing Synchronization
- **Status HUD Panel (X=118, Y=8, W=216, H=78)**:
  - Solid black background with a crisp 2-pixel bright green outer and inner border.
  - Live metric readouts:
    - `AI Time: 8 ms` (Real-time NPU execution duration)
    - `Score  : %` (Highest face confidence score; holds last valid score or shows 0%)
    - `Faces  : N` (Active detected face count; holds or shows 0)
  - **Permanently and solidly displayed on every frame, with or without detected faces**.
- **Per-Face Bounding Boxes & Confidence Badges**:
  - 3-pixel thick green bounding boxes framing detected faces.
  - Black background pill tag with green border and numerical percentage (e.g., `71%`) floating directly above each face.
- **D/AVE 2D 1-Frame Asynchronous Pipeline Synchronization**:
  - Resolves D/AVE 2D's internal double-buffered command queue behavior where `d2_endframe()` would normally queue current commands for execution on the *subsequent* frame.
  - By calling `d2_startframe()` immediately after `d2_endframe()` and synchronizing with `d2_flushframe()`, the GPU is guaranteed to finish all SDRAM writes *before* the CPU draws the HUD overlay. This completely eliminates camera blit overwriting and prevents HUD blinking.

### 5. Triple-Buffering in External 64MB SDRAM & AXI Bus Arbitration
- **Triple Framebuffers**: Three 1024×600 16-bit RGB565 display buffers (~3.6 MB total) reside in external SDRAM (`0x68000000`).
- **AXI Bus Matrix Tuning**: Slave arbitration priority is configured to **Fixed Priority (Group 3 = GLCDC)** via `R_BUS->BUSS[i].CNT = 0x0013`, prioritizing GLCDC DMA display reads over all other masters to prevent FIFO underflow.
- **Zero Tearing**: Buffer switching is synchronized with GLCDC Vblank line events.

---

## 3. System Architecture

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

## 4. Memory Footprint (Build Statistics)

| Memory Region | Used Size | Region Size | % Used | Description |
| :--- | :--- | :--- | :--- | :--- |
| **FLASH (ROM)** | **576,684 B** (~563 KB) | 768 KB | **73.33%** | Code, Ethos-U driver, YOLO weights, font data |
| **RAM (Internal SRAM)** | **836,508 B** (~817 KB) | 1,024 KB (1 MB) | **79.78%** | Thread stacks, BSS, heap, VIN DMA buffers |
| **Internal SRAM1** | **479,232 B** (468 KB) | 640 KB | **74.88%** | NPU Tensor Arena (`sub_0000_arena`: 442 KB) + input buffer |
| **External SDRAM** | Dynamic (~3.6 MB) | 64 MB | ~5.6% | Triple RGB565 Framebuffers (3 × 1.2 MB) |

---

## 5. Hardware Requirements & Pin Mapping

### Required Hardware
- **MCU Board**: Renesas EK-RA8P1 (R7KA8P1KFLCAC, Cortex-M85 + Ethos-U55 NPU)
- **Camera Module**: OmniVision OV5640 CMOS Sensor (2-lane MIPI CSI-2)
- **Display Panel**: RTKLCDPAR1S00001BE (1024×600 Parallel RGB888 TFT-LCD)
- **External Memory**: On-board 64MB SDRAM (`0x68000000`)
- **Debugger**: On-board SEGGER J-Link OB

### Pin Configuration
| Signal Name | Pin / Port | Function | Description |
|---|---|---|---|
| `CAM_XCLK` | P501 | GPT12 (GTIOC12A) | 24.0 MHz External Clock for Camera |
| `MIPI_IF_EN` | P108 | GPIO Output | MIPI Multiplexer Enable (Active Low) |
| `CAM_RST` | P709 | GPIO Output | OV5640 Hardware Reset (Active Low) |
| `CAM_SDA` | P511 | SCI9 (I2C) | Camera I2C Data |
| `CAM_SCL` | P512 | SCI9 (I2C) | Camera I2C Clock |
| `DISP_BLEN` | P514 | GPIO Output | LCD Backlight Enable (Active High) |
| `DISP_RST` | P606 | GPIO Output | LCD Panel Hardware Reset (Active Low) |
| `LCDC_CLK` | P515 | GLCDC | LCD Pixel Clock (40 MHz) |
| `LCDC_TCON0..2`| P806, P805, P807 | GLCDC | VSYNC, HSYNC, DE |
| `LCDC_DATA` | Various | GLCDC | 24-bit Parallel RGB888 Video Bus |

---

## 6. How to Build and Flash

### Using Provided Scripts (Recommended)
```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\yoloface_npu

:: Build
build.bat

:: Flash via SEGGER J-Link
flash.bat
```

### Manual Build via Zephyr `west`
```cmd
set "PYTHONUTF8=1"
call "%USERPROFILE%\zephyrproject\.venv\Scripts\activate.bat"
cd /d "%USERPROFILE%\zephyrproject"

:: Build
west build -b ek_ra8p1/r7ka8p1kflcac/cm85 -s "C:\Users\ioten\tron_zephyr\yoloface_npu" -d "%USERPROFILE%\zephyrproject\build_yoloface_npu" -p always

:: Flash
west flash -d "%USERPROFILE%\zephyrproject\build_yoloface_npu" --speed 4000
```

---

## 7. Serial Console Output (COM Port 115200bps, 8N1)

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

## 8. License Information

- **Zephyr RTOS Kernel & Subsystems**: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **Arm Ethos-U Core Driver & CMSIS-NN**: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **YOLO-Fastest Face Detection Model & Post-Processing**: [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0)
- **Renesas Flexible Software Package (FSP) Drivers & HAL**: [BSD 3-Clause License](https://opensource.org/licenses/BSD-3-Clause)
- **TES D/AVE 2D (DRW) Graphics Engine**: Licensed under the Renesas Electronics / TES Electronic Solutions Software License Agreement for evaluation and production use on supported Renesas RA microcontrollers.
