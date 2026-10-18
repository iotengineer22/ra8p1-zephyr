# YOLO Face Detection on CPU (Zephyr yoloface_cpu)

[日本語版 README (README_ja.md)](README_ja.md)

Real-time edge AI face detection application running on **Zephyr RTOS** with **Cortex-M85 CPU-only inference** on the Renesas EK-RA8P1 board.

---

## 1. Overview

This application captures 320x240 RGB565 video frames from an OV5640 MIPI-CSI2 camera, performs YOLO-Fastest face detection inference on the **Arm Cortex-M85 CPU** using **TensorFlow Lite for Microcontrollers (TFLM)** and **CMSIS-NN**, and displays the upscaled video (800x600) with bounding box overlays on a 1024x600 TFT LCD.

An on-screen HUD permanently displays real-time performance statistics: **FPS (48 FPS)**, **Face Count (Faces)**, **Detection Score (%)**, and **AI Inference Latency (AI Time: ~4.3 seconds)**.

---

## 2. CPU vs NPU Benchmark Comparison (Measured Data)

| Metric | NPU Version (`yoloface_npu`) | CPU Version (`yoloface_cpu`) | NPU Advantage |
| :--- | :--- | :--- | :--- |
| **Inference Hardware** | **Arm Ethos-U55 NPU** (@ 400 MHz) | **Arm Cortex-M85 CPU** (@ 1.0 GHz) | Dedicated NPU vs General CPU |
| **Framework** | Renesas RUHMI / Ethos-U Driver | TensorFlow Lite Micro + CMSIS-NN | Direct FlatBuffer Execution |
| **AI Inference Latency** | **~8 ms** | **~4,298 ms - 4,357 ms** | **~537x Speedup with NPU!** |
| **Display Framerate** | **48 FPS** (Vblank synced) | **48 FPS** (Vblank synced) | Identical real-time smoothness |
| **Tearing & Flicker** | **Zero** (SDRAM Triple Buffer) | **Zero** (SDRAM Triple Buffer) | Identical |
| **HUD Stability** | Rock-solid permanent overlay | Rock-solid permanent overlay (3s hold) | Identical |

### Asynchronous Multi-Threading Architecture
While CPU inference takes ~4.3 seconds per frame, the AI task runs in a background thread (priority 11), fully decoupled from the display thread (priority 10). Consequently, **camera capture and LCD display continue at a steady 48 FPS without dropping a single frame** while the CPU evaluates the model in the background.

---

## 3. Hardware & Memory Configuration

* **Target Board**: Renesas EK-RA8P1 (R7KA8P1KFLCAC)
  * CPU: Arm Cortex-M85 @ up to 1.0 GHz
  * Flash: 2MB, SRAM: 1MB
* **External 64MB SDRAM (`0x68000000`)**:
  * `0x68000000`: Framebuffer 0 (1024x600 RGB565: 1.2MB)
  * `0x68200000`: Framebuffer 1 (1024x600 RGB565: 1.2MB)
  * `0x68400000`: Framebuffer 2 (1024x600 RGB565: 1.2MB)
  * `0x68800000`: TFLite Micro Tensor Arena (2MB work area)
* **Camera**: OV5640 MIPI-CSI2 (320x240 RGB565 @ 60fps)
* **Display Engine**: D/AVE 2D (DRW) Hardware 2D Accelerator + GLCDC Controller
* **Font**: 18-dot bitmap font (`bg_font_18_full`)

---

## 4. Build & Flash Instructions

### Build
Run in Command Prompt:
```cmd
C:\Users\ioten\tron_zephyr\yoloface_cpu\build.bat
```

### Flash
Connect EK-RA8P1 via USB (J-Link J10 port) and run:
```cmd
C:\Users\ioten\tron_zephyr\yoloface_cpu\flash.bat
```

---

## 5. Serial Console Output (COM8, 115200 baud)

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
