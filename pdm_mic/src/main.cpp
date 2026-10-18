/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdio.h>

#include "bsp_api.h"
#include "dave_driver.h"
#include "drw/r_drw_base.h"
#include "font5x7.h"
#include "pdm/pdm_audio.h"

#define DISPLAY_HSIZE   1024
#define DISPLAY_VSIZE   600
#define PIXEL_SIZE      2 // RGB565 (16-bit)
#define FB_SIZE         (DISPLAY_HSIZE * DISPLAY_VSIZE * PIXEL_SIZE) // 1,228,800 bytes

#define SDRAM_BASE_ADDR 0x68000000UL
#define CAMERA_RESET_PIN BSP_IO_PORT_07_PIN_09 // P709

/* Triple buffer pointers allocated in physical SDRAM */
static uint8_t *const g_fb[3] = {
    (uint8_t *)(SDRAM_BASE_ADDR),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE * 2)
};

static d2_device *g_d2_handle = NULL;
static volatile uint32_t g_render_frame_count = 0;

/* Cache flush override functions required by D/AVE 2D */
extern "C" {
d1_int_t d1_cacheflush(d1_device *handle, d1_int_t memtype)
{
    ARG_UNUSED(handle);
    ARG_UNUSED(memtype);
    SCB_CleanDCache();
    return 1;
}

d1_int_t d1_cacheblockflush(d1_device *handle, d1_int_t memtype, const void *ptr, d1_uint_t size)
{
    ARG_UNUSED(handle);
    ARG_UNUSED(memtype);
    SCB_CleanDCache_by_Addr((void *)ptr, (int32_t)size);
    return 1;
}
}

/* Custom hardware-accelerated 5x7 font rendering using D/AVE 2D (used for pre-rendering) */
static void draw_char_5x7(d2_device *handle, int x, int y, int scaling, int c, d2_color color)
{
    if (c < 32 || c > 126) return;
    const unsigned char *glyph = font5x7[c - 32];
    
    // Character background box (Dark charcoal: 0xFF101015)
    d2_setcolor(handle, 0, 0xFF101015);
    d2_renderbox(handle,
                 (d2_point)(x << 4),
                 (d2_point)(y << 4),
                 (d2_width)((6 * scaling) << 4),
                 (d2_width)((8 * scaling) << 4));
    
    d2_setcolor(handle, 0, color);
    for (int col = 0; col < 5; col++)
    {
        unsigned char line = glyph[col];
        for (int row = 0; row < 7; row++)
        {
            if (line & (1 << row))
            {
                d2_renderbox(handle, 
                             (d2_point)((x + col * scaling) << 4), 
                             (d2_point)((y + row * scaling) << 4), 
                             (d2_width)(scaling << 4), 
                             (d2_width)(scaling << 4));
            }
        }
    }
}

static void print_string_5x7(d2_device *handle, int x, int y, int scaling, const char *str, d2_color color)
{
    int cur_x = x;
    while (*str)
    {
        draw_char_5x7(handle, cur_x, y, scaling, *str, color);
        cur_x += 6 * scaling;
        str++;
    }
}

/* Fast CPU 5x7 bitmap font rendering for real-time overlay */
static void draw_char_5x7_cpu_scale2(uint16_t *fb, int pitch, int x, int y, int c, uint16_t color_565, uint16_t bg_565)
{
    if (c < 32 || c > 126) return;
    const unsigned char *glyph = font5x7[c - 32];

    for (int col = 0; col < 6; col++)
    {
        unsigned char line = (col < 5) ? glyph[col] : 0;
        int px0 = x + col * 2;
        int px1 = px0 + 1;
        
        if (px0 < 0 || px0 >= DISPLAY_HSIZE) continue;

        for (int row = 0; row < 8; row++)
        {
            bool pixel_on = (row < 7) && (line & (1 << row));
            uint16_t val = pixel_on ? color_565 : bg_565;
            
            int py0 = y + row * 2;
            int py1 = py0 + 1;
            
            if (py0 >= 0 && py0 < DISPLAY_VSIZE)
            {
                fb[py0 * pitch + px0] = val;
                if (px1 < DISPLAY_HSIZE) fb[py0 * pitch + px1] = val;
            }
            if (py1 >= 0 && py1 < DISPLAY_VSIZE)
            {
                fb[py1 * pitch + px0] = val;
                if (px1 < DISPLAY_HSIZE) fb[py1 * pitch + px1] = val;
            }
        }
    }
}

static void print_string_5x7_cpu(uint16_t *fb, int pitch, int x, int y, int scaling, const char *str, uint16_t color_565, uint16_t bg_565)
{
    int cur_x = x;
    while (*str)
    {
        if (scaling == 2)
        {
            draw_char_5x7_cpu_scale2(fb, pitch, cur_x, y, *str, color_565, bg_565);
        }
        else
        {
            for (int col = 0; col < 6; col++)
            {
                unsigned char line = (col < 5) ? font5x7[*str - 32][col] : 0;
                for (int col_s = 0; col_s < scaling; col_s++)
                {
                    int px = cur_x + col * scaling + col_s;
                    if (px < 0 || px >= DISPLAY_HSIZE) continue;
                    for (int row = 0; row < 8; row++)
                    {
                        bool pixel_on = (row < 7) && (line & (1 << row));
                        uint16_t val = pixel_on ? color_565 : bg_565;
                        for (int row_s = 0; row_s < scaling; row_s++)
                        {
                            int py = y + row * scaling + row_s;
                            if (py >= 0 && py < DISPLAY_VSIZE)
                            {
                                fb[py * pitch + px] = val;
                            }
                        }
                    }
                }
            }
        }
        cur_x += 6 * scaling;
        str++;
    }
}

/* Optional Camera (OV5640) I2C detection */
static void check_camera_ov5640(void)
{
    printk("Resetting Camera (CAMERA_RESET -> P709)...\n");
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(CAMERA_RESET_PIN, BSP_IO_LEVEL_LOW);
    k_msleep(100);
    R_BSP_PinWrite(CAMERA_RESET_PIN, BSP_IO_LEVEL_HIGH);
    R_BSP_PinAccessDisable();
    k_msleep(20);

    const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(iic1));
    if (!device_is_ready(i2c_dev)) {
        printk("I2C device not ready, skipping camera check.\n");
        return;
    }

    printk("Reading OV5640 Product ID registers via I2C...\n");
    uint8_t reg_addr[2] = {0x30, 0x0a};
    uint8_t pid[2] = {0, 0};

    int ret = i2c_write_read(i2c_dev, 0x3c, reg_addr, 2, pid, 2);
    if (ret == 0) {
        printk("Product ID Read: H = 0x%02X, L = 0x%02X\n", pid[0], pid[1]);
        if (pid[0] == 0x56 && (pid[1] == 0x40 || pid[1] == 0x41 || pid[1] == 0x4C)) {
            printk("SUCCESS: Camera connection verified! (OV5640 detected)\n");
        } else {
            printk("WARNING: Product ID mismatch (Read 0x%02X%02X)\n", pid[0], pid[1]);
        }
    } else {
        printk("Camera I2C communication returned %d (camera module disconnected or optional)\n", ret);
    }
}

/* Physical SDRAM self-test */
static void sdram_self_test(void)
{
    printk("Testing physical SDRAM at address 0x%08X...\n", (uint32_t)SDRAM_BASE_ADDR);
    volatile uint32_t *p_sdram = (volatile uint32_t *)SDRAM_BASE_ADDR;
    bool sdram_ok = true;

    for (uint32_t i = 0; i < 1024; i++) {
        p_sdram[i] = 0x55AA55AA ^ i;
    }

    SCB_CleanInvalidateDCache_by_Addr((void *)SDRAM_BASE_ADDR, 1024 * sizeof(uint32_t));

    for (uint32_t i = 0; i < 1024; i++) {
        if (p_sdram[i] != (0x55AA55AA ^ i)) {
            printk("SDRAM verification failed at index %d! Read: 0x%08X, Expected: 0x%08X\n",
                   i, p_sdram[i], 0x55AA55AA ^ i);
            sdram_ok = false;
            break;
        }
    }

    if (sdram_ok) {
        printk("SDRAM verification SUCCESS!\n");
    } else {
        printk("ERROR: SDRAM physical memory verification failed!\n");
    }

    SCB_CleanInvalidateDCache_by_Addr((void *)SDRAM_BASE_ADDR, 1024 * sizeof(uint32_t));
}

/* Set AXI Bus Slave Arbitration to Fixed Priority (Group 3) */
static void set_bus_arbitration(void)
{
    printk("=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===\n");
    for (int i = 0; i < 18; i++) {
        R_BUS->BUSS[i].CNT = 0x0013;
    }
}

/* Pre-render static borders and title in all 3 buffers */
static void prerender_static_ui(void)
{
    printk("Pre-rendering background and static borders to all 3 framebuffers...\n");
    for (int i = 0; i < 3; i++)
    {
        d2_startframe(g_d2_handle);
        d2_framebuffer(g_d2_handle, g_fb[i], DISPLAY_HSIZE, DISPLAY_HSIZE, DISPLAY_VSIZE, d2_mode_rgb565);
        d2_clear(g_d2_handle, 0xFF101015);

        /* Outer neon cyan border (0xFF00E5FF) */
        d2_setcolor(g_d2_handle, 0, 0xFF00E5FF);
        d2_renderline(g_d2_handle, 30 << 4, 30 << 4, (DISPLAY_HSIZE - 30) << 4, 30 << 4, 2 << 4, 0); // Top
        d2_renderline(g_d2_handle, 30 << 4, (DISPLAY_VSIZE - 30) << 4, (DISPLAY_HSIZE - 30) << 4, (DISPLAY_VSIZE - 30) << 4, 2 << 4, 0); // Bottom
        d2_renderline(g_d2_handle, 30 << 4, 30 << 4, 30 << 4, (DISPLAY_VSIZE - 30) << 4, 2 << 4, 0); // Left
        d2_renderline(g_d2_handle, (DISPLAY_HSIZE - 30) << 4, 30 << 4, (DISPLAY_HSIZE - 30) << 4, (DISPLAY_VSIZE - 30) << 4, 2 << 4, 0); // Right

        /* Title */
        print_string_5x7(g_d2_handle, 50, 50, 3, "EK-RA8P1 PDM MICROPHONE TEST (Zephyr RTOS)", 0xFF00E5FF);

        /* Waveform box outer & inner */
        d2_setcolor(g_d2_handle, 0, 0xFF3A3A4A);
        d2_renderbox(g_d2_handle, 50 << 4, 180 << 4, 924 << 4, 300 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF14141A);
        d2_renderbox(g_d2_handle, 51 << 4, 181 << 4, 922 << 4, 298 << 4);

        /* Volume bar box outer & inner */
        d2_setcolor(g_d2_handle, 0, 0xFF3A3A4A);
        d2_renderbox(g_d2_handle, 50 << 4, 510 << 4, 924 << 4, 30 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF14141A);
        d2_renderbox(g_d2_handle, 51 << 4, 511 << 4, 922 << 4, 28 << 4);

        d2_endframe(g_d2_handle);
        d2_flushframe(g_d2_handle);
        SCB_CleanInvalidateDCache_by_Addr((void *)g_fb[i], FB_SIZE);
    }
}

/* Thread 1: Main Audio Visualizer Graphics Rendering Loop */
#define TASK1_STACK_SIZE 32768
#define TASK1_PRIORITY   10

static void task1_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("\n=== Zephyr PDM Microphone Visualizer Test Start ===\n");

    /* Cold start stabilization wait */
    k_msleep(500);

    /* SDRAM self test */
    sdram_self_test();

    /* AXI bus arbitration */
    set_bus_arbitration();

    /* Camera verification (matches Tron test sequence) */
    check_camera_ov5640();

    /* Clear SDRAM framebuffers to black */
    printk("Clearing SDRAM framebuffers to black...\n");
    memset(g_fb[0], 0, FB_SIZE);
    memset(g_fb[1], 0, FB_SIZE);
    memset(g_fb[2], 0, FB_SIZE);
    SCB_CleanInvalidateDCache();

    /* Configure board MIPI/LCD multiplexer switch (P108 LOW matching Tron) */
    const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(ioport1));
    if (device_is_ready(gpio1)) {
        gpio_pin_configure(gpio1, 8, GPIO_OUTPUT_INACTIVE); // P108 LOW
    }

    /* Safe Power & Reset Sequencing:
     * 1. Ensure Backlight (DISP_BLEN -> P514) is initially OFF to prevent inrush brownouts & white screen.
     * 2. Perform a clean hardware reset on LCD panel (DISP_RST -> P606) while backlight is OFF.
     */
    const struct device *gpio5 = DEVICE_DT_GET(DT_NODELABEL(ioport5));
    const struct device *gpio6 = DEVICE_DT_GET(DT_NODELABEL(ioport6));

    if (device_is_ready(gpio5)) {
        gpio_pin_configure(gpio5, 14, GPIO_OUTPUT_INACTIVE); // Backlight OFF
    }

    if (device_is_ready(gpio6)) {
        gpio_pin_configure(gpio6, 6, GPIO_OUTPUT_ACTIVE);
        gpio_pin_set(gpio6, 6, 1);
        k_msleep(200);
        gpio_pin_set(gpio6, 6, 0); // Assert reset
        k_msleep(100);
        gpio_pin_set(gpio6, 6, 1); // De-assert reset
        k_msleep(200);
    }

    /* Initialize LCD Display device */
    printk("Initializing LCD (GLCDC)...\n");
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("ERROR: Display device is not ready!\n");
        return;
    }

    /* Initialize D/AVE 2D engine */
    printk("Initializing D/AVE 2D Graphics Engine...\n");
    g_d2_handle = d2_opendevice(0);
    if (!g_d2_handle) {
        printk("ERROR: Failed to open D/AVE 2D engine!\n");
        return;
    }
    d2_inithw(g_d2_handle, 0);

    /* 2D rendering parameters */
    d2_setblendmode(g_d2_handle, d2_bm_one, d2_bm_zero);
    d2_setalphamode(g_d2_handle, d2_am_constant);
    d2_setalpha(g_d2_handle, 0xff);
    d2_setantialiasing(g_d2_handle, 0);

    /* Pre-render background and static elements */
    prerender_static_ui();

    /* Initialize PDM microphone subsystem */
    int pdm_ret = pdm_audio_init();
    if (pdm_ret != 0) {
        printk("WARNING: PDM audio initialization returned %d\n", pdm_ret);
    }

    printk("Starting D/AVE 2D Rendering Loop (PDM Audio Visualizer)...\n");

    uint8_t draw_buf = 0;
    uint8_t pending_buf = 1;
    uint8_t display_buf = 2;
    int loop_cnt = 0;

    struct display_buffer_descriptor desc = {
        .buf_size = FB_SIZE,
        .width = DISPLAY_HSIZE,
        .height = DISPLAY_VSIZE,
        .pitch = DISPLAY_HSIZE,
    };

    /* Initial display_write starts GLCDC clocks and outputs black frame */
    (void)display_write(display_dev, 0, 0, &desc, g_fb[display_buf]);

    /* Allow LCD panel internal charge pumps (VGH/VGL/VCOM) to stabilize with active clocks */
    k_msleep(100);

    /* NOW turn Backlight ON safely - eliminates white screen freeze and inrush voltage dip */
    if (device_is_ready(gpio5)) {
        gpio_pin_set(gpio5, 14, 1);
        printk("LCD Backlight enabled (safe power-on sequencing complete).\n");
    }

    while (1) {
        g_render_frame_count++;

        /* Start D2D frame */
        d2_startframe(g_d2_handle);
        d2_framebuffer(g_d2_handle, g_fb[draw_buf], DISPLAY_HSIZE, DISPLAY_HSIZE, DISPLAY_VSIZE, d2_mode_rgb565);

        /* 1. Clear waveform box inner area */
        d2_setcolor(g_d2_handle, 0, 0xFF14141A);
        d2_renderbox(g_d2_handle, 51 << 4, 181 << 4, 922 << 4, 298 << 4);

        /* 2. Zero-line reference */
        d2_setcolor(g_d2_handle, 0, 0xFF2A2A35);
        d2_renderline(g_d2_handle, 51 << 4, 330 << 4, 973 << 4, 330 << 4, 1 << 4, 0);

        /* 3. Draw waveform */
        d2_setcolor(g_d2_handle, 0, 0xFF00FF66); // Neon spring green
        int step = 2;
        int prev_x = 51;
        int prev_y = 330;

        for (int i = 0; i < 922; i += step)
        {
            int idx = i * WAVEFORM_POINTS / 922;
            if (idx >= WAVEFORM_POINTS) idx = WAVEFORM_POINTS - 1;

            int16_t sample16 = g_waveform_history[idx];
            int32_t y_offset = ((int32_t)sample16 * 140) / 256;
            int y_coord = 330 - y_offset;
            int x_coord = 51 + i;

            if (y_coord < 181) y_coord = 181;
            if (y_coord > 479) y_coord = 479;

            if (i > 0)
            {
                d2_renderline(g_d2_handle, prev_x << 4, prev_y << 4, x_coord << 4, y_coord << 4, 2 << 4, 0);
            }
            prev_x = x_coord;
            prev_y = y_coord;
        }

        /* 4. Clear volume bar box inner area */
        d2_setcolor(g_d2_handle, 0, 0xFF14141A);
        d2_renderbox(g_d2_handle, 51 << 4, 511 << 4, 922 << 4, 28 << 4);

        /* 5. Render filled volume bar based on RMS (scale to 256) */
        int32_t vol_width = ((int32_t)g_rms_val * 922) / 256;
        if (vol_width > 922) vol_width = 922;

        if (vol_width > 0)
        {
            d2_setcolor(g_d2_handle, 0, 0xFF00E5FF); // Neon cyan
            d2_renderbox(g_d2_handle, 51 << 4, 511 << 4, vol_width << 4, 28 << 4);
        }

        /* 6. Text metrics using fast CPU rendering */
        char status_buf[64];
        if (g_pdm_start_err == 0 && g_pdm_open_err == 0) {
            snprintf(status_buf, sizeof(status_buf), "PDM STATUS: RUNNING   IRQ COUNT: %-8u", (unsigned int)g_pdm_event_count);
        } else {
            snprintf(status_buf, sizeof(status_buf), "PDM STATUS: ERROR (Open: 0x%04X, Start: 0x%04X)", (int)g_pdm_open_err, (int)g_pdm_start_err);
        }
        print_string_5x7_cpu((uint16_t *)g_fb[draw_buf], DISPLAY_HSIZE, 50, 95, 2, status_buf, 0xFFFF, 0x1082);

        char metrics_buf[64];
        snprintf(metrics_buf, sizeof(metrics_buf), "RMS VOLUME: %-6u PEAK-TO-PEAK: %-6d", (unsigned int)g_rms_val, (int)g_peak_to_peak);
        print_string_5x7_cpu((uint16_t *)g_fb[draw_buf], DISPLAY_HSIZE, 50, 125, 2, metrics_buf, 0xFFFF, 0x1082);

        /* End frame command recording */
        d2_endframe(g_d2_handle);

        /* Wait for GPU hardware rendering to complete */
        d2_flushframe(g_d2_handle);

        /* Pinpoint cache clean for CPU-rendered text area (Y: 90 to 145 = 55 scan lines) */
        SCB_CleanInvalidateDCache_by_Addr((void *)((uint16_t *)g_fb[draw_buf] + 90 * DISPLAY_HSIZE),
                                          55 * DISPLAY_HSIZE * sizeof(uint16_t));

        /* Monitor GLCDC Graphics 1 FIFO Underflow status */
        if (R_GLCDC->SYSCNT.STMON_b.L1UNDF) {
            printk("WARNING: GLCDC Graphics 1 FIFO Underflow detected!\n");
            R_GLCDC->SYSCNT.STCLR_b.L1UNDFCLR = 1U;
        }

        /* Submit to GLCDC with Vblank synchronization */
        int err = display_write(display_dev, 0, 0, &desc, g_fb[draw_buf]);
        if (err != 0) {
            printk("display_write error: %d\n", err);
        }

        k_yield();

        /* Buffer rotation (triple buffering) */
        uint8_t next_draw = display_buf;
        uint8_t next_pending = draw_buf;
        uint8_t next_display = pending_buf;

        draw_buf = next_draw;
        pending_buf = next_pending;
        display_buf = next_display;

        loop_cnt++;
    }
}

K_THREAD_DEFINE(task1_tid, TASK1_STACK_SIZE, task1_entry, NULL, NULL, NULL,
                TASK1_PRIORITY, 0, 0);

/* Thread 2: Asynchronous Performance & Audio Monitoring (Low Priority) */
#define TASK2_STACK_SIZE 2048
#define TASK2_PRIORITY   14

static void task2_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_msleep(5000);
        uint32_t fps = g_render_frame_count / 5;
        g_render_frame_count = 0;
        printk("FPS: %u | PDM Events: %u | RMS: %u | Pk-Pk: %d\n",
               fps, (unsigned int)g_pdm_event_count, (unsigned int)g_rms_val, (int)g_peak_to_peak);
    }
}

K_THREAD_DEFINE(task2_tid, TASK2_STACK_SIZE, task2_entry, NULL, NULL, NULL,
                TASK2_PRIORITY, 0, 0);

int main(void)
{
    printk("Starting Zephyr PDM Microphone Visualizer Application...\n");
    return 0;
}
