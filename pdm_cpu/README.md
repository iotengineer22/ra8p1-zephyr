# EK-RA8P1 PDM Microphone Voice Classifier - Cortex-M85 CPU Inference (Zephyr RTOS)

This project ports the μT-Kernel 3.0 audio keyword recognition program (`tron_pdm_detect_cpu`) to **Zephyr RTOS** running on the Renesas RA8P1 (EK-RA8P1) development board using pure **Cortex-M85 CPU inference with CMSIS-NN**.

Audio is captured continuously from the on-board PDM digital microphone at 32kHz (20-bit precision), transferred via DMAC, and downsampled to a 16kHz circular buffer (16,000 samples = 1.0 second window). The Edge Impulse C++ SDK and CMSIS-DSP compute MFCC acoustic features, which are fed to the EON-compiled neural network executed entirely on the **Arm Cortex-M85 CPU (480MHz)** using **CMSIS-NN** vector acceleration in just **1.23 ms**.

Real-time inference results (5-class classification: `down`, `left`, `noise`, `right`, `up`), class probability bar charts, live oscilloscope waveform, and RMS volume meters are rendered with rock-solid stability on the 1024×600 LCD panel using the TES D/AVE 2D hardware graphics engine.

---

## 1. Key Features

### 1.1 Audio Capture & Preprocessing
- **On-board PDM Digital Microphone (Unit 0, Channel 2)**:
  - Clock pin: `P812` (MIC_CLK, `pdmclk2`)
  - Data pin: `P502` (MIC_DAT, `pdmdat2`)
  - Sample rate: 32 kHz (4th-order Sinc filter)
- **DMAC Channel 1 Ping-Pong Transfer**:
  - 1,600 samples (50 ms) transferred with zero CPU intervention
  - ISR (`ELC_EVENT_DMAC1_INT` -> Vector 32) handles 20-bit sign extension and 16-bit normalization
- **16kHz Circular Buffer**:
  - Downsampled (/2) into a 16,000-sample (1.0 second) circular buffer `g_audio_buffer`

### 1.2 Arm Cortex-M85 CPU (CMSIS-NN) High-Speed Inference
- **CMSIS-NN Hardware Acceleration**:
  - Utilizes Cortex-M85 Helium (M-Profile Vector Extension: MVE) vector instructions
  - `CONFIG_CMSIS_NN=y` optimizes convolutions (`arm_convolve_wrapper_s8`), pooling (`arm_max_pool_s8`), fully connected layers (`arm_fully_connected_wrapper_s8`), and softmax (`arm_softmax_s8`)
- **Measured Real-World Latency**:
  - **CPU NN Inference Time: ~1.23 ms (1,232 ~ 1,235 μs)**
  - **DSP MFCC Extraction Time: ~40.8 ms** (CMSIS-DSP accelerated)
  - **Inference Interval: 250 ms (4 FPS continuous evaluation)**
- **Supported Classification Labels (5 classes)**:
  - `down`
  - `left`
  - `noise` (ambient noise / silence)
  - `right`
  - `up`

### 1.3 Real-Time Visualizer GUI (D/AVE 2D Full Hardware + GLCDC)
- **D/AVE 2D Full Hardware Rendering**:
  - All UI elements and 5x7 font glyphs (`draw_char_5x7`) recorded directly into D/AVE 2D display list
  - Vertical run-length font optimization minimizes GPU command overhead
  - Zero CPU frame buffer direct writes and zero cache flush contention
- **AI Detection Card (Top Left)**:
  - Header: `AI KEYWORD DETECT (5-CLASS CPU)`
  - Winner label & confidence (`DETECTED: up (96%)`)
  - Highlights in **neon green** when recognized (confidence ≥ 50%), **neon cyan** for noise
  - Live CPU and DSP latency metrics (`DSP: 40.8 ms | CPU: 1.23 ms`)
  - Inference counter and status (`INFERENCES: 1234  STATUS: ACTIVE`)
- **Class Probability Bar Chart (Top Right, 0-100%)**:
  - Header: `CLASS PROBABILITIES (0-100%):`
  - 5 color progress bars and percentage labels (`%3d%%`)
- **Live Microphone Oscilloscope (Center)**:
  - Plots 1024 raw audio samples in neon green (`d2_renderline`)
- **RMS Volume Meter & Status (Bottom)**:
  - Cyan level bar displaying real acoustic volume
  - Bottom status line showing real-time metrics (`RMS`, `PK-PK`, `PDM IRQ`, `STATUS`)

### 1.4 Rock-Solid Display Stability (Zero Wobble / Flicker / White-Screen / Overlap)
1. Pixel clock divisor 12 (PLL2 240MHz ÷ 12 = **20.0 MHz** pixel clock)
2. Pin control drive strength: **High Drive**
3. Stable Vsync parameters (1334 × 635, Hsync 10/300, Vsync 5/30)
4. Safe power-on backlight sequencing (100 ms charge pump delay before backlight enable)
5. Strict D/AVE 2D frame lifecycle management (`d2_startframe` / `d2_endframe` / `d2_flushframe`)
6. SDRAM Triple Framebuffering (`0x68000000`, `0x68200000`, `0x68400000`)
7. D/AVE 2D display list block size expansion (`d2_setdlistblocksize(g_d2_handle, 2048)`)
8. Exact coordinate boundaries eliminating text overflow and collisions

---

## 2. Directory Structure

```
pdm_cpu/
├── .gitignore                     # Git ignore rules
├── CMakeLists.txt                 # CMake configuration (CMSIS-DSP, CMSIS-NN, D2D)
├── prj.conf                       # Zephyr Kconfig (CMSIS-DSP, CMSIS-NN, C++17, GLCDC)
├── app.overlay                    # Device Tree overlay (GLCDC 20MHz, High Drive)
├── build.bat                      # One-click build script
├── flash.bat                      # One-click J-Link flash script
├── README_ja.md                   # Japanese documentation
├── README.md                      # English documentation (this file)
└── src/
    ├── main.cpp                   # Main threads (GUI rendering, CPU inference, monitor)
    ├── font5x7.h                  # 5x7 font bitmap
    ├── dave2d/                    # TES D/AVE 2D graphics driver
    ├── drw/                       # Renesas DRW low-level wrapper
    ├── pdm/                       # PDM microphone & DMAC driver
    ├── font/                      # Font library
    ├── model-parameters/          # Edge Impulse model metadata & parameters
    ├── tflite-model/              # EON compiled CPU model (CMSIS-NN)
    │   ├── compute_sub_0000.c     # CMSIS-NN conv / fc / softmax invocation
    │   ├── compute_sub_0000.h
    │   ├── kernel_library_int.c   # Quantization & activation routines
    │   ├── kernel_library_int.h
    │   ├── kernel_library_utils.c # Kernel utility helpers
    │   ├── kernel_library_utils.h
    │   ├── model_io_data.c        # Model weights & biases
    │   ├── model_io_data.h
    │   ├── tflite_learn_943529_3_compiled.cpp
    │   ├── tflite_learn_943529_3_compiled.h
    │   └── trained_model_ops_define.h
    └── edge-impulse-sdk/          # Edge Impulse C++ SDK
```

---

## 3. Architecture & Task Breakdown

```
[PDM Mic Input (32kHz)]
       │
       ▼ (DMAC1: 1600 samples / 50ms)
[PDM DMAC ISR (Vector 32)]
       ├─ 20-bit -> 16-bit scaling & sign extension
       ├─ RMS volume & peak-to-peak calculation
       ├─ 1024-sample oscilloscope buffer update
       └─ Downsample (/2) -> 16kHz circular buffer (16,000 samples = 1.0s)
              │
              ▼
┌─────────────────────────────────────────────────────────────┐
│ Task 3: AI Inference Thread (Priority 11, Stack 32KB)       │
│   ・Interval: 250 ms (4 FPS)                                │
│   ・DSP (MFCC feature extraction): 40.8 ms (CMSIS-DSP)      │
│   ・CPU Inference: 1.23 ms (Cortex-M85 + CMSIS-NN)          │
│   ・Updates shared classification state                     │
└─────────────────────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────┐
│ Task 1: UI Render Thread (Priority 10, Stack 8KB)           │
│   ・D/AVE 2D full hardware rendering (UI, fonts, waveform)  │
│   ・GLCDC Vsync triple framebuffering                       │
│   ・Zero CPU cache coherency races or flickering            │
└─────────────────────────────────────────────────────────────┘
              │
              ▼
┌─────────────────────────────────────────────────────────────┐
│ Task 2: System Monitor Thread (Priority 14, Stack 2KB)      │
│   ・Logs FPS, PDM events, inference count every 5 seconds   │
└─────────────────────────────────────────────────────────────┘
```

---

## 4. Build and Flash Instructions

### 4.1 Prerequisites
- Zephyr SDK 1.0.1
- Python 3.12+ in Zephyr venv (`C:\Users\ioten\zephyrproject\.venv`)
- SEGGER J-Link software (V9.42+)

### 4.2 Build
Run `build.bat` from the project root:
```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\pdm_cpu
build.bat
```

### 4.3 Flash
Connect the EK-RA8P1 board via USB J10 and run `flash.bat`:
```cmd
flash.bat
```

---

## 5. Live Verification Output

Serial console output from J-Link CDC UART (COM8, 115200 bps):

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
```

---

## 6. NPU (`pdm_npu`) vs CPU (`pdm_cpu`) Comparison

| Parameter | NPU Version (`pdm_npu`) | CPU Version (`pdm_cpu`) |
|:---|:---|:---|
| **Inference Engine** | Arm Ethos-U55 NPU (240MHz) | Cortex-M85 CPU (480MHz) + CMSIS-NN |
| **NN Inference Time** | **~0.25 ms (247 ~ 250 μs)** | **~1.23 ms (1,232 ~ 1,235 μs)** |
| **DSP MFCC Time** | ~32.5 ms | ~40.8 ms |
| **Inference Interval** | 250 ms (4 FPS) | 250 ms (4 FPS) |
| **GUI Renderer** | TES D/AVE 2D Full Hardware | TES D/AVE 2D Full Hardware |
| **Display FPS** | 20 ~ 23 FPS (rock-solid) | 20 ~ 23 FPS (rock-solid) |
| **Hardware Driver** | NPU Driver (`rm_ethosu`, `ethosu_driver`) | Standard CMSIS-NN (`CONFIG_CMSIS_NN=y`) |
| **Flash Usage** | ~109 KB (13.9%) | ~125 KB (15.9%) |
| **SRAM Usage** | ~249 KB (23.8%) | ~247 KB (23.6%) |
