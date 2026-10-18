[日本語版 (README_ja.md)](README_ja.md)

# Renesas RA8 D/AVE 2D Graphics & LCD Application for Zephyr RTOS (`d2_test`)

## 1. Overview
This project is a native **Zephyr RTOS** port of the μT-Kernel 3.0 graphics test application (`tron_d2_test`) designed for the **Renesas EK-RA8P1** evaluation kit (Arm Cortex-M85 @ 1.0 GHz).

It demonstrates tear-free, flicker-free, and high-performance **48–49 FPS** 2D vector graphics rendering on a **1024x600 TFT-LCD panel** by leveraging:
- The Renesas **D/AVE 2D (DRW)** hardware graphics accelerator
- The **GLCDC (Global LCD Controller)**
- A **Triple-Buffer Framebuffer architecture in 64MB External SDRAM**
- Optimized **differential / dirty rectangle erasing** to prevent SDRAM bus saturation

In addition, the application performs physical SDRAM verification, I2C communication checks for an OV5640 camera sensor, and robust hardware display sequencing.

---

## 2. Key Features & Architectural Highlights

### 1. D/AVE 2D (DRW) Hardware Acceleration
- Offloads vector geometry rasterization (bouncing balls, anti-fringed circles, boxes, and borders) entirely to the Dave2D hardware engine.
- Rendering commands are recorded into GPU display lists and executed asynchronously.
- The Zephyr application thread synchronizes with GPU hardware execution completion using an interrupt-driven semaphore (`k_sem`).

### 2. Triple-Buffering in External SDRAM
- Three full-resolution 16-bit RGB565 framebuffers (1024 × 600 × 2 bytes = ~1.2MB each, total 3.6MB) are allocated in external SDRAM starting at `0x68000000`.
- Automatic memory initialization is handled by Zephyr's native SDRAM driver (`CONFIG_MEMC_RENESAS_RA_SDRAM=y`).
- Page flipping is synchronized with the GLCDC vertical blanking (Vblank) / line detection interrupt via `display_write()`, completely eliminating visual tearing.

### 3. Differential / Dirty Ball Erasing (Zero Bus Contention)
- **The Problem**: A naive full-screen clear (`d2_clear`) requires writing 1.2MB of continuous bursts to SDRAM every frame. At 48 FPS, this saturates the external SDRAM bus, delaying GLCDC DMA reads and causing FIFO underflow (manifesting as black horizontal tearing bands).
- **The Solution**: 
  - Each framebuffer is cleared completely only once on startup.
  - On subsequent frames, only the previous ball bounding region (recorded 3 frames prior for that specific buffer) is erased using background-colored geometry (~50KB per frame).
  - This reduces SDRAM write traffic by **over 24× (1.2MB $\rightarrow$ ~50KB)**, completely eliminating bus contention and guaranteeing zero black bands across the entire display.

### 4. AXI Bus Slave Arbitration Configuration
- Configures the MCU's internal AXI bus matrix (`R_BUS->BUSS[i].CNT = 0x0013`) to **Fixed Priority (Group 3 = GLCDC)**.
- This guarantees that display controller DMA reads are always prioritized over general bus masters, preventing display FIFO underflows during concurrent memory accesses.

### 5. Robust Hardware LCD Reset Sequence
- Implements a dedicated reset sequence on `DISP_RST` (P606): `High (500ms) -> Low (200ms) -> High (500ms)`.
- This ensures the internal LCD panel timing controller (TCON) and panel PLL are fully stabilized before the GLCDC video clock stream begins.

### 6. Cortex-M85 Data Cache Coherency
- Explicit D-Cache flush and invalidate operations (`SCB_CleanInvalidateDCache`) are performed before GPU display list dispatch, after GPU completion, and following Vblank buffer flips to ensure strict coherency between the CPU, GPU, and GLCDC DMA across cacheable SDRAM.

### 7. Multi-Threaded Execution
- **`task_1` (Main Graphics Thread, Stack: 32KB, Priority: 10)**:
  - Cold-start power stabilization (500ms delay).
  - External SDRAM physical read/write self-test.
  - AXI bus arbitration setup.
  - Camera OV5640 reset and I2C Product ID probe (`0x5640`).
  - LCD hardware reset, GLCDC initialization, and backlight activation.
  - D/AVE 2D engine initialization and continuous 48–49 FPS rendering loop.
- **`task_2` (Watchdog / Monitor Thread, Stack: 1KB, Priority: 10)**:
  - Outputs heartbeat status (`task 2 running...`) every 7 seconds.

---

## 3. Hardware Requirements & Specifications

| Component | Specification |
|---|---|
| **MCU Board** | Renesas EK-RA8P1 |
| **Microcontroller** | R7KA8P1KFLCAC (Arm Cortex-M85 @ 1.0 GHz, 2MB Flash, 1MB SRAM) |
| **Display Panel** | RTKLCDPAR1S00001BE (1024×600 TFT-LCD, Parallel RGB888 interface) |
| **External Memory** | 64MB SDRAM (32-bit bus, Base address: `0x68000000`) |
| **Camera (Optional)** | Omnivision OV5640 CMOS Image Sensor |
| **Debugger** | On-board SEGGER J-Link OB |

---

## 4. Pin Mapping Summary

| Signal Name | Pin / Port | Function | Description |
|---|---|---|---|
| `DISP_BLEN` | P514 | GPIO Output | LCD Backlight Enable (Active High) |
| `DISP_RST` | P606 | GPIO Output | LCD Panel Hardware Reset (Active Low) |
| `LCDC_CLK` | P515 | Peripheral | LCD Pixel Clock (40 MHz) |
| `LCDC_TCON0` | P806 | Peripheral | Vertical Sync (VSYNC) |
| `LCDC_TCON1` | P805 | Peripheral | Horizontal Sync (HSYNC) |
| `LCDC_TCON2` | P807 | Peripheral | Data Enable (DE) |
| `LCDC_DATA[00..23]` | Various | Peripheral | 24-bit Parallel RGB888 Video Bus |
| `CAMERA_RESET` | P709 | GPIO Output | OV5640 Camera Reset (Active Low) |
| `IIC1_SDA` | P511 | Peripheral | Camera I2C Data |
| `IIC1_SCL` | P512 | Peripheral | Camera I2C Clock |

---

## 5. Project Directory Structure

```text
d2_test/
├── CMakeLists.txt          # Zephyr CMake build configuration
├── prj.conf                # Kconfig options (SDRAM, GLCDC, I2C, Cache, Heap)
├── app.overlay             # Devicetree overlay (GLCDC timings, DRW, SDRAM, pins)
├── build.bat               # Windows one-click automated build script
├── flash.bat               # Automated J-Link flashing script
├── README.md               # English Documentation (this file)
├── README_ja.md            # Japanese Documentation (日本語版)
└── src/
    ├── main.c              # Application main, threads, and Dave2D render loop
    └── dave2d/             # Renesas D/AVE 2D (DRW) driver library
        ├── inc/            # Dave2D headers (dave_driver.h, etc.)
        └── src/            # Dave2D source code & Zephyr OS abstraction layer
```

---

## 6. How to Build and Flash

### Prerequisites
1. **Zephyr Development Environment**: Installed at `%HOMEPATH%\zephyrproject` with active Python virtual environment (`.venv`).
2. **SEGGER J-Link Software**: Installed (e.g., `C:\Program Files\SEGGER\JLink_V942`).
3. Connect the EK-RA8P1 board to the host PC via the J-Link OB USB port (`J10`).

### Method 1: Using the Automated Scripts (Recommended)
Open a standard Windows Command Prompt (`cmd.exe`) and navigate to the project directory:

```cmd
cd C:\Users\ioten\OneDrive\デスクトップ\tron\github\zephyr\d2_test

:: 1. Build the application
build.bat

:: 2. Flash to EK-RA8P1 board
flash.bat
```

### Method 2: Manual Build with West CLI
```cmd
set "PYTHONUTF8=1"
call "%USERPROFILE%\zephyrproject\.venv\Scripts\activate.bat"
cd /d "%USERPROFILE%\zephyrproject"

:: Build
west build -b ek_ra8p1/r7ka8p1kflcac/cm85 -s "C:\Users\ioten\tron_zephyr\d2_test" -d "%USERPROFILE%\zephyrproject\build_d2_test" -p always

:: Flash
west flash -d "%USERPROFILE%\zephyrproject\build_d2_test"
```

---

## 7. Execution Output & Verification

Open a serial terminal (e.g., Tera Term, PuTTY) connected to the EK-RA8P1 J-Link CDC UART port:
- **Baud Rate**: 115200 bps
- **Data**: 8-bit, Parity: None, Stop: 1-bit, Flow Control: None

### Typical Serial Console Output
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

### Expected Visual Output on LCD
- **Resolution**: 1024 × 600
- **Background**: Solid dark navy (`0xFF10101C`)
- **Outer Border**: Bright green 2px border placed 30px inward from the screen edge
- **Static Objects**:
  - Red filled circle (radius 60px) at $(300, 300)$
  - Blue filled rectangle ($150 \times 150$ px) at $(700, 200)$
- **Dynamic Animation**:
  - Yellow filled ball (radius 40px) smoothly bouncing off the green borders at 48–49 FPS with no tearing, no stuttering, and zero black bands.
