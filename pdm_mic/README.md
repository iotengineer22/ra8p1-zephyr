# EK-RA8P1 PDM Digital Microphone & Real-time Audio Visualizer (Zephyr RTOS)

This project ports the Renesas RA8P1 PDM digital microphone oscilloscope and audio visualizer program from μT-Kernel 3.0 (`tron_pdm_d2_test`) to **Zephyr RTOS**.

It captures 32 kHz, 20-bit audio from the on-board PDM digital microphone on the EK-RA8P1 kit using the Renesas PDM peripheral coupled with DMAC transfer. Audio waveforms and RMS volume metrics are rendered in real time on the 1024×600 RGB LCD at ~45-50 FPS using the TES D/AVE 2D (DRW) hardware graphics acceleration engine and external SDRAM triple-buffering.

---

## 1. Key Features

- **PDM Audio Capture (Unit 0, Channel 2)**:
  - Pins: `P812` (`MIC_CLK` / `pdmclk2`) and `P502` (`MIC_DAT` / `pdmdat2`).
  - Sampling Frequency: 32 kHz (target: 32,000 Hz, actual: 32,258 Hz).
  - 4th-order Sinc decimation filter (`sincrng = 9`, `sincdec = 62`), HPF, LPF, and compensation filters.
  - Sample Format: 20-bit signed PCM right-justified in 32-bit registers.
- **DMA-Accelerated Continuous Transfer (DMAC Ch 1)**:
  - Event `ELC_EVENT_PDM_DAT2` triggers block transfers of 1600 samples (50 ms frames) with zero CPU overhead.
  - DMAC completion interrupt (`ELC_EVENT_DMAC1_INT`) mapped to NVIC Vector 32 via Renesas ICU.
  - PDM error interrupt (`ELC_EVENT_PDM_ERR2`) mapped to NVIC Vector 33 via ICU.
  - Double-buffered ping-pong mechanism ensures continuous capture without audio drops.
- **Real-Time Audio DSP Statistics**:
  - 20-bit sign-extension: `(val << 12) >> 12`.
  - 16-bit normalization: `>> 4` (-32768 to +32767).
  - Peak-to-Peak amplitude calculation.
  - RMS volume calculation using fast integer square root algorithm (`integer_sqrt`).
  - 1024-point waveform history buffer (`g_waveform_history[1024]`).
- **Hardware-Accelerated 2D Graphics (TES D/AVE 2D / DRW)**:
  - Neon spring green (`0xFF00FF66`) oscilloscope waveform plotted with connected lines.
  - Neon cyan (`0xFF00E5FF`) dynamic RMS volume bar.
  - Pre-rendered static UI borders and titles into all 3 framebuffers to eliminate SDRAM bus bottleneck.
  - Region-specific dirty rect clearing.
- **GLCDC Display & Triple Buffering**:
  - Three physical framebuffers in external SDRAM (`0x68000000`, `0x68200000`, `0x68400000`) for 1024×600 RGB565 display.
  - Vblank synchronized buffer swapping (`display_write`) ensures zero screen tearing and flicker.
  - Fixed-priority AXI bus arbitration Group 3 (`R_BUS->BUSS[i].CNT = 0x0013`) guarantees GLCDC bandwidth.
- **CPU 5×7 Bitmap Font Overlay**:
  - Live status overlay (`PDM STATUS`, `IRQ COUNT`, `RMS VOLUME`, `PEAK-TO-PEAK`) rendered via CPU with pinpoint D-cache flushing to prevent GPU queue exhaustion.

---

## 2. Directory Structure

```
pdm_mic/
├── .gitignore             # Git ignore rules
├── CMakeLists.txt         # CMake build definition
├── prj.conf               # Zephyr Kconfig configuration
├── app.overlay            # DeviceTree overlay (GLCDC, DRW, SDRAM, GPIO)
├── build.bat              # One-click build script
├── flash.bat              # J-Link flash programming script
├── README_ja.md           # Japanese documentation
├── README.md              # English documentation
└── src/
    ├── main.cpp           # Main loop, D/AVE 2D visualizer, triple buffering
    ├── font5x7.h          # 5x7 bitmap font data
    ├── dave2d/            # TES D/AVE 2D graphics driver
    │   ├── inc/
    │   └── src/
    ├── drw/               # D/AVE 2D low-level Zephyr wrapper driver
    │   ├── r_drw_base.c
    │   ├── r_drw_irq.c
    │   ├── r_drw_memory.c
    │   └── r_drw_cfg.h
    └── pdm/               # Renesas RA PDM & DMAC driver
        ├── pdm_audio.h    # PDM audio subsystem header
        ├── pdm_audio.c    # Pin setup, ICU/NVIC routing, DSP, callback
        ├── r_pdm.h        # FSP PDM instance header
        ├── r_pdm_api.h    # FSP PDM API header
        ├── r_pdm.c        # FSP PDM driver source
        ├── r_pdm_cfg.h    # PDM configuration
        ├── r_dmac.c       # FSP DMAC driver source
        └── r_dmac_cfg.h   # DMAC configuration
```

---

## 3. Hardware Pin Mapping

| Signal | MCU Pin | Pin Function | Description |
|:---|:---|:---|:---|
| **MIC_CLK** | P812 | `IOPORT_PERIPHERAL_PDM` (PSEL 0x1B) | PDM Clock (`pdmclk2`) |
| **MIC_DAT** | P502 | `IOPORT_PERIPHERAL_PDM` (PSEL 0x1B) | PDM Data (`pdmdat2`) |
| **DISP_BLEN** | P514 | GPIO Output High | LCD Backlight Enable |
| **DISP_RST** | P606 | GPIO Output Pulse | LCD Reset Pulse |
| **CAMERA_RESET** | P709 | GPIO Output High | Camera Reset (Compatibility) |
| **LCD RGB** | Various | `IOPORT_PERIPHERAL_LCD_GRAPHICS` | 1024×600 Parallel RGB Bus |

---

## 4. How to Build & Flash

### Prerequisites
- OS: Windows 10 / 11
- Zephyr SDK: v1.0.1 (`C:\Users\ioten\zephyr-sdk-1.0.1`)
- Zephyr Workspace: `C:\Users\ioten\zephyrproject`
- SEGGER J-Link: v9.42 or later

### Build
Run `build.bat` from Command Prompt or PowerShell:
```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\pdm_mic
build.bat
```
Upon completion, the binaries `build\zephyr\zephyr.elf`, `zephyr.bin`, and `zephyr.hex` will be created.

### Flash
Connect the EK-RA8P1 board via USB (J-Link OB port) and run:
```cmd
flash.bat
```
The board will be programmed and automatically reset to run the visualizer application.

---

## 5. Serial Monitor Output

Open COM8 at 115200 bps (8N1) to inspect real-time log output:

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

---

## 6. Display Stabilization & Flicker/Jitter Elimination Techniques

To resolve the periodic vertical screen jitter/shaking and white-screen lockups upon USB reconnection encountered during porting, the following hardware and driver solutions were implemented:

### 1. Panel Pixel Clock Alignment (20 MHz)
- **Problem**: Zephyr's default `PLL2R` clock configuration (480 MHz) coupled with `output-clock-divisor = <6>` resulted in a **40 MHz** pixel clock (double Tron's 20 MHz). This doubled SDRAM read bandwidth demands by GLCDC to ~58 MB/s, starving the AXI bus and triggering FIFO underflow and vertical sync loss.
- **Solution**: Set `output-clock-divisor = <12>` in `app.overlay` ($240\text{ MHz} \div 12 = \mathbf{20\text{ MHz}}$). This matches Tron's clock frequency identically, completely eliminating bus congestion and underflows.

### 2. High Drive Strength on All LCD Pins (`drive-strength = "high"`)
- **Problem**: Pin drive strength defaulted to `low` in devicetree. Stray trace capacitance slowed signal rise/fall transitions, causing the panel timing controller to periodically misinterpret VSYNC and DE edges and shift by 1 line (vertical jitter).
- **Solution**: Added `drive-strength = "high";` to `glcdc_default` in `app.overlay`. All clock, sync, and RGB data signals now transition with maximum slew rate and zero edge jitter.

### 3. D/AVE 2D Pipeline Normalization
- **Problem**: Redundant `d2_startframe` calls at the end of the rendering loop caused dual display list swaps per frame, executing empty command buffers and disrupting the GPU-GLCDC synchronization phase.
- **Solution**: Aligned with the canonical Tron pipeline: `d2_startframe` at loop start, followed by `d2_endframe` $\rightarrow$ `d2_flushframe` at loop end.

### 4. Safe Power-On & Backlight Sequencing
- **Problem**: Turning on the backlight (P514) before GLCDC clocks and signals were stable caused current inrush brownouts and prevented panel charge pumps (VGH/VGL) from charging, locking the display into a white screen.
- **Solution**: Backlight is held OFF during boot and hardware reset (P606). Clocks are started via initial `display_write`, a 100 ms stabilization delay allows internal panel charge pumps to reach steady-state voltages, and only then is the backlight enabled.

