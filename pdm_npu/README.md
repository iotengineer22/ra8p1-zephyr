# EK-RA8P1 PDM Voice Keyword Detection & Arm Ethos-U55 NPU Inference (Zephyr RTOS)

This project is a complete port of the Renesas RA8P1 PDM digital microphone keyword classification system (`tron_pdm_detect`, originally developed on μT-Kernel 3.0) to **Zephyr RTOS**.

It captures acoustic audio at 32kHz 20-bit precision from the onboard PDM microphone on the EK-RA8P1 board, downsamples and buffers it into a 16kHz circular buffer (16,000 samples = 1.0s window), extracts MFCC features using the Edge Impulse C++ SDK accelerated by CMSIS-DSP, and executes neural network inference on the integrated **Arm Ethos-U55 NPU (microNPU)** in only **0.25 ms (250 μs)**.

The inference results (5 classes: `down`, `left`, `noise`, `right`, `up`), individual class probability bars, real-time audio waveform (oscilloscope), and RMS volume meter are rendered on the 1024×600 LCD panel via the TES D/AVE 2D graphics hardware accelerator with rock-solid stability (zero screen jitter, zero tearing, and zero white-screen issues).

---

## 1. Key Features

### 1.1 Audio Ingestion & Preprocessing
- **Onboard PDM Digital Microphone (Unit 0, Channel 2)**:
  - Clock pin: `P812` (MIC_CLK, `pdmclk2`)
  - Data pin: `P502` (MIC_DAT, `pdmdat2`)
  - Sampling rate: 32 kHz (4th order Sinc filter `sincrng=9`, `sincdec=62`)
  - Data format: 20-bit signed PCM right-justified in 32-bit registers
- **DMAC (Channel 1) Background Transfer**:
  - Automatically transfers blocks of 1600 samples (50 ms frames) with zero CPU overhead
  - DMAC ISR (`ELC_EVENT_DMAC1_INT` -> Vector 32) handles 20-bit sign-extension and 16-bit scaling
- **16kHz Downsampling Circular Buffer**:
  - Downsamples 32kHz audio by 2 into `g_audio_buffer` (16,000 samples = 1.0 second sliding window)
  - Feeds the continuous audio stream to Edge Impulse's `run_classifier()`

### 1.2 Ultra-Fast Arm Ethos-U55 NPU Inference
- **Ethos-U55 Hardware Acceleration**:
  - Integrates Renesas FSP Ethos-U driver (`RM_ETHOSU`) and Arm Core driver (`ethosu_driver`) onto Zephyr
  - NPU interrupt (`ELC_EVENT_NPU_IRQ`) connected to NVIC Vector 20
  - Command stream (`sub_0001_command_stream.c`) and weight binaries (`sub_0001_model_data.c`) executed directly by the NPU
- **Benchmark Latencies (Measured live on hardware)**:
  - **NPU Neural Network Inference: ~0.25 ms (249 to 252 μs)**
  - **DSP MFCC Feature Extraction (650 features): ~32.5 ms** (CMSIS-DSP accelerated)
  - **Inference Rate**: 4 times/sec (every 250 ms)
- **Supported Classification Labels (5 classes)**:
  - `down`
  - `left`
  - `noise` (ambient noise / silence)
  - `right`
  - `up`

### 1.3 Real-Time Visualizer GUI (D/AVE 2D Full Hardware + GLCDC)
- **D/AVE 2D Full Hardware Rendering**:
  - All UI cards, progress bars, oscilloscope waveform, and 5x7 font glyphs (`draw_char_5x7`) are recorded directly into the D/AVE 2D hardware display list.
  - The font renderer uses vertical run-length batching, drastically reducing GPU command volume.
  - Zero CPU direct writes to SDRAM and zero D-cache flushing (`SCB_CleanInvalidateDCache`), eliminating bus contention and cache coherency races.
- **AI Detection Card (Top Left)**:
  - Header: `AI KEYWORD DETECT (5-CLASS)`
  - Displays winner keyword and confidence (`DETECTED: up (96%)`)
  - Automatically highlights in **neon green** when keyword confidence is ≥ 50%, and **neon cyan** for noise
  - Live NPU and DSP latency metrics (`DSP: 32.5 ms | NN: 0.25 ms`)
  - Cumulative inference count and buffer status (`INFERENCES: 1234  STATUS: ACTIVE`)
- **Class Probability Bar Chart (Top Right, 0-100%)**:
  - Header: `CLASS PROBABILITIES (0-100%):`
  - 5 horizontal progress bars and percentage labels (`%3d%%`) for `down`, `left`, `noise`, `right`, and `up`
- **Live Microphone Oscilloscope (Center)**:
  - Plots the latest 1024 PDM samples in high-visibility neon green (`d2_renderline`)
  - Clear zero-line reference (Y=382) visualizing signal dynamic range
- **RMS Volume Meter & Audio Subsystem Status (Bottom)**:
  - Cyan horizontal level bar displaying true acoustic volume via integer square-root algorithm
  - Bottom status line showing real-time metrics (`RMS: <val>  PK-PK: <val>  PDM IRQ: <cnt>  STATUS: NORMAL`)

### 1.4 Rock-Solid Display Stability (Zero Wobble / Flicker / White-Screen / Overlap)
- Incorporates all critical display timing and hardware fixes validated in `pdm_mic`:
  1. Pixel clock divisor 12 (PLL2 240MHz ÷ 12 = **20.0 MHz** pixel clock)
  2. Pin control drive strength: **High Drive**
  3. Stable Vsync parameters (1334 × 635, Hsync 10/300, Vsync 5/30)
  4. Safe power-on backlight sequencing (100 ms charge pump delay before backlight enable)
  5. Strict D/AVE 2D frame lifecycle management (`d2_startframe` / `d2_endframe` / `d2_flushframe`)
  6. SDRAM Triple Framebuffering (`0x68000000`, `0x68200000`, `0x68400000`)
  7. D/AVE 2D display list block size expansion (`d2_setdlistblocksize(g_d2_handle, 2048)`) preventing heap fragmentation
  8. Exact coordinate boundaries eliminating text overflow and string collisions

---

## 2. Directory Structure

```
pdm_npu/
├── .gitignore                     # Git ignore rules
├── CMakeLists.txt                 # CMake configuration
├── prj.conf                       # Zephyr Kconfig (CMSIS-DSP, C++17, GLCDC)
├── app.overlay                    # Devicetree overlay (GLCDC 20MHz, High Drive)
├── build.bat                      # One-click build batch script
├── flash.bat                      # One-click J-Link flash script
├── README_ja.md                   # Japanese documentation
├── README.md                      # English documentation
└── src/
    ├── main.cpp                   # Main program (UI task, AI task, Monitor task)
    ├── font5x7.h                  # 5x7 bitmap font data
    ├── dave2d/                    # TES D/AVE 2D graphics driver
    ├── drw/                       # Renesas DRW low-level driver
    ├── pdm/                       # PDM & DMAC audio driver
    ├── font/                      # Font library (bg_font_18)
    ├── npu/                       # Arm Ethos-U55 NPU driver
    ├── ruhmi/                     # RUHMI Ethos-U command stream and weights
    ├── model-parameters/          # Edge Impulse model metadata & parameters
    ├── tflite-model/              # EON compiled model
    └── edge-impulse-sdk/          # Edge Impulse C++ SDK
```

---

## 3. Architecture & Task Hierarchy

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
│   ・DSP (MFCC feature extraction): 32.5 ms (CMSIS-DSP)      │
│   ・NPU Inference: 0.25 ms (Arm Ethos-U55 hardware)         │
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
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\pdm_npu
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
