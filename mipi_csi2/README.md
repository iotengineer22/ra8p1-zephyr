[日本語版 (README_ja.md)](README_ja.md)

# Renesas RA8 MIPI-CSI2 Camera to LCD Display Application for Zephyr RTOS (`mipi_csi2`)

## 1. Overview

This project is a native **Zephyr RTOS** port of the μT-Kernel 3.0 MIPI-CSI2 camera application (`tron_mipi_test_ori`) designed for the **Renesas EK-RA8P1** evaluation kit (Arm Cortex-M85 @ 1.0 GHz).

It demonstrates real-time, zero-copy, and high-performance **~48–51 FPS** video streaming from an **OmniVision OV5640 CMOS Image Sensor** over a **2-lane MIPI-CSI2** interface to a **1024×600 TFT-LCD panel** by leveraging:
- Renesas **MIPI D-PHY & MIPI-CSI2** Hardware Controller
- Renesas **Video Input (VIN)** DMA Engine with Triple-Buffering in On-Chip SRAM
- Renesas **D/AVE 2D (DRW)** hardware graphics accelerator for real-time bilinear upscale (320×240 $\rightarrow$ 800×600)
- Renesas **GLCDC (Global LCD Controller)** with Triple-Buffering in 64MB External SDRAM
- Hardware **GPT12** timer supplying a 24.0 MHz XCLK clock to the image sensor

---

## 2. Key Architectural Highlights & Features

```
 +--------------------------------------------------------------------------------+
 |                           Renesas EK-RA8P1 Board                               |
 |                                                                                |
 |  +-------------------+  MIPI-CSI2  +----------------------------------------+  |
 |  |  OmniVision       |  (2 Lanes)  |  Renesas RA8P1 MCU (Cortex-M85 @ 1GHz) |  |
 |  |  OV5640 Camera    |===========> |  [MIPI D-PHY] -> [MIPI CSI2 Receiver]  |  |
 |  |  (320x240 YUV422) |             |                     |                  |  |
 |  +-------------------+             |              [VIN DMA Engine]          |  |
 |      ^    ^    ^                   +---------------------|------------------+  |
 |      |    |    +--- CAM_RST (P709)                       v                     |
 |      |    +-------- MIPI_IF_EN (P108, LOW)    Triple Framebuffers           |
 |      +------------- 24MHz XCLK (GPT12, P501)  (On-Chip SRAM: 153.6 KB x 3)  |
 |                                                          |                     |
 |                                                          v                     |
 |                                            +---------------------------+       |
 |                                            | D/AVE 2D Hardware GPU     |       |
 |                                            | - Bilinear Upscaling      |       |
 |                                            |   (320x240 -> 800x600)    |       |
 |                                            | - Green Decorative Border |       |
 |                                            +---------------------------+       |
 |                                                          |                     |
 |                                                          v                     |
 |  +--------------------+     Parallel RGB888  +---------------------------+     |
 |  | 1024x600 TFT-LCD   | <=================== | GLCDC Display Controller  |     |
 |  | (Centered 800x600) |                      | Triple FB in 64MB SDRAM   |     |
 |  +--------------------+                      +---------------------------+     |
 +--------------------------------------------------------------------------------+
```

### 1. High-Performance MIPI-CSI2 & VIN Video Pipeline
- **MIPI D-PHY / CSI-2 Receiver**: Configured for 2-lane High-Speed (HS) reception of YUV422 (8-bit) raw video frames.
- **VIN DMA Controller**: Captures 320×240 YUV422 frames directly into 128-byte aligned triple buffers allocated in internal SRAM (`0x22000000`), eliminating frame-drop and CPU memory overhead.
- **Interrupt Handling**: Fully wired to Zephyr's NVIC interrupt vector table (`VIN_IRQ`, `VIN_ERR`, `MIPICSI_RX`, `MIPICSI_DL`, `MIPICSI_VC`, `MIPICSI_PM`, `MIPICSI_GST`) with core event routing.

### 2. Real-Time Bilinear Texture Upscaling via D/AVE 2D
- Hardware GPU accelerates image scaling from native 320×240 resolution up to 800×600 using `d2_blitcopy` with **bilinear texture interpolation** (`d2_tm_filter`).
- Centers the 800×600 video feed precisely onto the 1024×600 LCD screen ($X=112, Y=0$) with a clean decorative outline, achieving silky-smooth rendering without CPU burden.

### 3. Dedicated Camera Clock & Pin Sequencing
- **GPT12 24.0 MHz XCLK Generation**: Timer channel 12 (`P501` / `GTIOC12A`) generates a stable 24 MHz CMOS clock required for OV5640 internal PLLs and SCCB digital blocks.
- **MIPI Switch Enable (`P108`)**: Active-low configuration enables the high-speed multiplexer routing differential MIPI traces directly to the camera connector.
- **Hardware Sensor Reset (`P709`)**: Precise Power-On and Reset sequencing ensures proper camera startup before I2C initialization.

### 4. Triple-Buffering & AXI Bus Arbitration
- **External SDRAM Framebuffers**: Three 1024×600 16-bit RGB565 display buffers (~3.6MB total) reside in 64MB SDRAM (`0x68000000`).
- **AXI Bus Matrix Tuning**: Slave arbitration priority is configured to **Fixed Priority (Group 3 = GLCDC)** via `R_BUS->BUSS[i].CNT = 0x0013`, guaranteeing tear-free display output without FIFO underflow.
- **Zero Visual Tearing**: Buffer switching is synchronized with GLCDC Vblank line events.

### 5. Multi-Threaded Architecture
- **`task_1` (Main Camera & Graphics Thread, Stack: 32KB, Priority: 10)**:
  - External SDRAM read/write test.
  - LCD hardware reset and GLCDC initialization.
  - D/AVE 2D graphics engine startup.
  - Camera XCLK, reset sequence, OV5640 I2C registers, and VIN capture launch.
  - Continuous **~48–51 FPS** display upscale and rendering loop.
- **`task_2` (System Heartbeat Thread, Stack: 1KB, Priority: 10)**:
  - Outputs heartbeat logs every 7 seconds to monitor background system health.

---

## 3. Hardware Requirements & Specifications

| Component | Specification |
|---|---|
| **MCU Board** | Renesas EK-RA8P1 |
| **Microcontroller** | R7KA8P1KFLCAC (Arm Cortex-M85 @ 1.0 GHz, 2MB Flash, 1MB SRAM) |
| **Camera Module** | OmniVision OV5640 CMOS Sensor (2-lane MIPI CSI-2, 320×240 YUV422) |
| **Display Panel** | RTKLCDPAR1S00001BE (1024×600 TFT-LCD, Parallel RGB888 interface) |
| **External Memory** | 64MB SDRAM (32-bit bus, Base address: `0x68000000`) |
| **Debugger** | On-board SEGGER J-Link OB |

---

## 4. Pin Mapping Summary

| Signal Name | Pin / Port | Function | Description |
|---|---|---|---|
| `CAM_XCLK` | P501 | GPT12 (GTIOC12A) | 24.0 MHz External Clock for Camera |
| `MIPI_IF_EN` | P108 | GPIO Output | MIPI Multiplexer Enable (Active Low) |
| `CAM_RST` | P709 | GPIO Output | OV5640 Sensor Hardware Reset (Active Low) |
| `IIC1_SDA` | P511 | Peripheral | Camera I2C Data (SCCB Interface) |
| `IIC1_SCL` | P512 | Peripheral | Camera I2C Clock (SCCB Interface) |
| `DISP_BLEN` | P514 | GPIO Output | LCD Backlight Enable (Active High) |
| `DISP_RST` | P606 | GPIO Output | LCD Panel Hardware Reset (Active Low) |
| `LCDC_CLK` | P515 | Peripheral | LCD Pixel Clock (~40 MHz) |
| `LCDC_TCON[0..3]`| Various | Peripheral | LCD Timing Control (VSYNC, HSYNC, DE) |
| `LCDC_DATA[00..23]` | Various | Peripheral | 24-bit Parallel RGB888 Video Bus |

---

## 5. Project Directory Structure

```text
mipi_csi2/
├── CMakeLists.txt          # Zephyr CMake build configuration
├── prj.conf                # Kconfig options (SDRAM, GLCDC, I2C, Cache, Heap)
├── app.overlay             # Devicetree overlay (GLCDC timings, DRW, SDRAM, pins)
├── build.bat               # Windows one-click automated build script
├── flash.bat               # Automated J-Link flashing script
├── README.md               # English Documentation (this file)
├── README_ja.md            # Japanese Documentation
├── src/
│   ├── main.c              # Application entry, SDRAM test, and display loop
│   ├── camera/
│   │   ├── camera_layer.c  # OV5640 init, XCLK generation, I2C driver
│   │   ├── camera_layer.h  # Camera layer API definitions
│   │   └── camera_layer_config.h # Camera resolution and format configuration
│   ├── mipi/
│   │   ├── mipi_vin_config.c # FSP MIPI D-PHY, CSI-2, VIN instances & ISRs
│   │   ├── mipi_vin_config.h # Vector allocations and buffer declarations
│   │   ├── r_mipi_csi.c    # FSP MIPI-CSI2 driver
│   │   ├── r_mipi_phy.c    # FSP MIPI D-PHY driver
│   │   └── r_vin.c         # FSP Video Input driver
│   ├── dave2d/             # D/AVE 2D hardware graphics driver library
│   └── drw/                # Zephyr DRW driver binding & ISR wrapper
```

---

## 6. Build & Execution

### 1. Build the Application
Open PowerShell or Command Prompt, navigate to the project directory, and execute `build.bat`:
```bat
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\mipi_csi2
.\build.bat
```

### 2. Flash to the Target Board
Connect the Renesas EK-RA8P1 board via micro-USB (J-Link OB port) and run `flash.bat`:
```bat
.\flash.bat
```

### 3. Serial Terminal Output Example
Connect to the J-Link Virtual COM port at **115200 baud, 8N1**:
```text
*** Booting Zephyr OS build v4.4.0-12700-g8dafb9a897da ***
Starting Zephyr MIPI-CSI2 Camera Application...

=== Zephyr MIPI-CSI2 Camera to LCD Display Application Start ===
task 2 running...
Testing physical SDRAM at address 0x68000000...
SDRAM verification SUCCESS!
=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===
Clearing SDRAM framebuffers to black...
Initializing LCD (GLCDC)...
LCD Backlight enabled.
Initializing D/AVE 2D Graphics Engine...
Starting Camera Subsystem Initialization...
Starting GPT12 Clock for Camera XCLK on P501...
Releasing Camera Reset (P709 -> HIGH)...
[Camera Init] Product ID High: 0x56, Low: 0x40
SUCCESS: Camera connection verified! (OV5640 detected)
Loading OV5640 Configuration Table...
Configuring OV5640 PLL and internal clocks...
Connecting MIPI & VIN Interrupts to NVIC...
Opening Video Input (VIN) Controller...
SUCCESS: Camera stream enabled.
SUCCESS: Camera capture started.
Entering Real-time Camera Display Loop (Bilinear Upscale 320x240 -> 800x600)...
[VIN CB] First frame captured successfully!
FPS: 51 | Capture Buffer: 0x22026200
FPS: 48 | Capture Buffer: 0x22026200
FPS: 48 | Capture Buffer: 0x2204BA00
FPS: 48 | Capture Buffer: 0x2204BA00
FPS: 48 | Capture Buffer: 0x22000A00
```

---

## 7. Memory Utilization (RAM / ROM Usage)

Memory footprint report generated upon building the application (Zephyr RTOS Kernel + Renesas FSP + D/AVE 2D + Video Pipeline):

| Memory Region | Target Address & Description | Total Capacity | Used Memory | Usage (%) | Free Margin |
|:---|:---|:---:|:---:|:---:|:---:|
| **ROM (Code Flash)** | On-chip Flash (`0x02000000`)<br>Program text, ISR table, rodata | 2,048 KB<br>(2.0 MB) | **69,456 B**<br>(~67.8 KB) | **3.31%** | ~1,980 KB<br>(96.69%) |
| **RAM (Internal SRAM)** | On-chip SRAM (`0x22000000`)<br>Kernel BSS, stacks, heap, VIN DMA buffers | 1,024 KB<br>(1.0 MB) | **728,897 B**<br>(~711.8 KB) | **69.51%** | ~312 KB<br>(30.49%) |
| **External SDRAM** | External 32-bit SDRAM (`0x68000000`)<br>GLCDC display triple-buffers (1024×600 RGB565 × 3) | 64 MB | **3,686,400 B**<br>(~3.52 MB) | **5.49%** | ~60.48 MB<br>(94.51%) |

> [!NOTE]
> **Breakdown of Internal SRAM (RAM) Consumption**:
> - **VIN Camera Capture Triple Framebuffers**: `320 × 240 × 2 Bytes × 3 buffers = 460,800 Bytes` (~**43.9%** of total SRAM). Direct DMA into high-speed on-chip SRAM guarantees zero-latency, zero-copy video streaming.
> - **Zephyr System Heap**: `65,620 Bytes` (`CONFIG_HEAP_MEM_POOL_SIZE=65536`).
> - **Thread & Interrupt Stacks**: ~`43,000 Bytes` (Task 1: 32 KB, Main thread: 8 KB, ISR stack: 2 KB, Task 2: 1 KB).
> - **Kernel & Driver State / Static BSS**: ~`159,477 Bytes`.

---

## 8. License Information

This project is distributed under a composite permissive open-source license model suitable for commercial use, modification, and product integration without copyleft source disclosure obligations:

| Component | Target Files / Scope | License | Copyright Holder |
|:---|:---|:---|:---|
| **Zephyr RTOS Core** | Kernel, Drivers, CMSIS, HAL | **Apache-2.0** | The Zephyr Project / Linux Foundation |
| **Renesas FSP Modules** | `src/mipi/` (VIN, MIPI-CSI2, PHY)<br>`src/drw/` (D/AVE 2D glue) | **BSD-3-Clause** | Renesas Electronics Corporation |
| **D/AVE 2D Driver** | `src/dave2d/` (2D accelerator driver) | **Renesas Software License** (Royalty-free) | Renesas Electronics Europe / TES Electronic Solutions |
| **Application Layer** | `src/main.c`, `src/camera/`, `src/mipi/mipi_vin_config.*` | **Apache-2.0** / **BSD-3-Clause** | Project Contributors |

> [!NOTE]
> **Complete Independence from μT-Kernel (T-License 2.0)**:
> While originally adapted from μT-Kernel 3.0 source code (`tron_mipi_test_ori`), all kernel tasks, synchronization primitives, and driver bindings have been completely rewritten to native Zephyr RTOS APIs. No T-Kernel code or T-License 2.0 copyleft obligations apply to this project.
