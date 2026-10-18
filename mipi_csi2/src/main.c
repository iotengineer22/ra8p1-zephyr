/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "bsp_api.h"
#include "dave_driver.h"
#include "r_drw_base.h"
#include "mipi_vin_config.h"
#include "camera_layer.h"

#define DISPLAY_HSIZE 1024
#define DISPLAY_VSIZE 600
#define FB_SIZE       (DISPLAY_HSIZE * DISPLAY_VSIZE * 2) /* RGB565: 1,228,800 bytes */

/* Triple Framebuffers in External 64MB SDRAM */
#define SDRAM_BASE_ADDR 0x68000000
static uint8_t *const g_fb[3] = {
    (uint8_t *)(SDRAM_BASE_ADDR),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE * 2)
};

static d2_device *g_d2_handle = NULL;

/* Cache flush override functions required by Dave2D */
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

/* Set AXI Bus Slave Arbitration to Fixed Priority (Group 3 = GLCDC Priority) */
static void set_bus_arbitration(void)
{
    printk("=== Setting Bus Slave Arbitration to Fixed Priority (Group 3) ===\n");
    for (int i = 0; i < 18; i++) {
        R_BUS->BUSS[i].CNT = 0x0013;
    }
}

/* Thread 1: Main Camera Capture & LCD Display Loop */
#define TASK1_STACK_SIZE 32768
#define TASK1_PRIORITY   10

static void task1_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("\n=== Zephyr MIPI-CSI2 Camera to LCD Display Application Start ===\n");

    /* Cold start stabilization wait */
    k_msleep(500);

    /* SDRAM self-test */
    sdram_self_test();

    /* AXI bus arbitration setup */
    set_bus_arbitration();

    /* Clear SDRAM framebuffers to black */
    printk("Clearing SDRAM framebuffers to black...\n");
    memset(g_fb[0], 0, FB_SIZE);
    memset(g_fb[1], 0, FB_SIZE);
    memset(g_fb[2], 0, FB_SIZE);
    SCB_CleanInvalidateDCache();

    /* Robust Hardware reset sequence for LCD panel (DISP_RST -> P606) */
    const struct device *gpio6 = DEVICE_DT_GET(DT_NODELABEL(ioport6));
    if (device_is_ready(gpio6)) {
        gpio_pin_configure(gpio6, 6, GPIO_OUTPUT_ACTIVE);
        gpio_pin_set(gpio6, 6, 1);
        k_msleep(500);
        gpio_pin_set(gpio6, 6, 0);
        k_msleep(200);
        gpio_pin_set(gpio6, 6, 1);
        k_msleep(500);
    }

    /* Initialize LCD Display device */
    printk("Initializing LCD (GLCDC)...\n");
    const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        printk("ERROR: Display device is not ready!\n");
        return;
    }

    /* Enable LCD Backlight */
    display_blanking_off(display_dev);
    printk("LCD Backlight enabled.\n");

    /* Initialize D/AVE 2D engine */
    printk("Initializing D/AVE 2D Graphics Engine...\n");
    g_d2_handle = d2_opendevice(0);
    if (!g_d2_handle) {
        printk("ERROR: Failed to open D/AVE 2D engine!\n");
        return;
    }
    d2_inithw(g_d2_handle, 0);

    d2_setblendmode(g_d2_handle, d2_bm_one, d2_bm_zero);
    d2_setalphamode(g_d2_handle, d2_am_constant);
    d2_setalpha(g_d2_handle, 0xff);
    d2_setantialiasing(g_d2_handle, 0);

    /* Initialize MIPI-CSI2 Camera (OV5640) */
    if (camera_init(false) != 0) {
        printk("WARNING: Camera initialization failed in normal mode! Retrying in test mode...\n");
        if (camera_init(true) != 0) {
            printk("FATAL: Camera initialization failed in both modes! Halting capture.\n");
            while (1) {
                k_msleep(1000);
            }
        }
    }

    camera_image_buffer_initialize();
    camera_capture_start();
    printk("SUCCESS: Camera capture started.\n");

    uint8_t draw_buf = 0;
    uint8_t pending_buf = 1;
    uint8_t display_buf = 2;
    uint32_t loop_cnt = 0;

    struct display_buffer_descriptor desc = {
        .buf_size = FB_SIZE,
        .width    = DISPLAY_HSIZE,
        .height   = DISPLAY_VSIZE,
        .pitch    = DISPLAY_HSIZE,
    };

    /* Initial display_write to start GLCDC cleanly */
    (void)display_write(display_dev, 0, 0, &desc, g_fb[display_buf]);
    k_msleep(30);

    int64_t last_fps_time = k_uptime_get();
    uint32_t fps_counter = 0;
    bool initial_clear_done[3] = {false, false, false};

    printk("Entering Real-time Camera Display Loop (Bilinear Upscale 320x240 -> 800x600)...\n");

    while (1) {
        fps_counter++;
        int64_t now = k_uptime_get();
        if (now - last_fps_time >= 1000) {
            printk("FPS: %u | Capture Buffer: 0x%08X\n",
                   fps_counter, (uint32_t)p_camera_capture_buffer_stored);
            fps_counter = 0;
            last_fps_time = now;
        }

        if (p_camera_capture_buffer_stored != NULL) {
            /* Synchronize data cache for captured camera buffer */
            SCB_InvalidateDCache_by_Addr(p_camera_capture_buffer_stored, (int32_t)(320 * 240 * 2));

            /* Start GPU frame command recording */
            d2_startframe(g_d2_handle);

            /* Set render target framebuffer */
            d2_framebuffer(g_d2_handle, g_fb[draw_buf], DISPLAY_HSIZE, DISPLAY_HSIZE, DISPLAY_VSIZE, d2_mode_rgb565);

            /* Clear margins to black once per buffer */
            if (!initial_clear_done[draw_buf]) {
                d2_clear(g_d2_handle, 0xFF000000);
                initial_clear_done[draw_buf] = true;
            }

            /* Set camera frame as texture source */
            d2_setblitsrc(g_d2_handle, (void *)p_camera_capture_buffer_stored, 320, 320, 240, d2_mode_rgb565);

            /* Bilinear upscale: 320x240 -> 800x600, Centered at X=112, Y=0 */
            d2_blitcopy(g_d2_handle,
                        320, 240,
                        0, 0,
                        800 << 4, 600 << 4,  /* Target width and height (16.4 fixed-point) */
                        112 << 4, 0 << 4,    /* Destination X and Y (centered) */
                        d2_tm_filter);       /* Bilinear filtering */

            /* Decorative border around the active video area */
            d2_setcolor(g_d2_handle, 0, 0xFF00FF00); // Green outline
            d2_renderline(g_d2_handle, 112 << 4, 0 << 4, 912 << 4, 0 << 4, 2 << 4, 0);
            d2_renderline(g_d2_handle, 112 << 4, 599 << 4, 912 << 4, 599 << 4, 2 << 4, 0);
            d2_renderline(g_d2_handle, 112 << 4, 0 << 4, 112 << 4, 599 << 4, 2 << 4, 0);
            d2_renderline(g_d2_handle, 912 << 4, 0 << 4, 912 << 4, 599 << 4, 2 << 4, 0);

            /* End frame command recording */
            d2_endframe(g_d2_handle);

            /* Clean cache so display lists are visible to GPU */
            SCB_CleanInvalidateDCache();

            /* Wait for GPU hardware execution to finish */
            d2_flushframe(g_d2_handle);

            /* Clean & invalidate cache for coherent SDRAM access */
            SCB_CleanInvalidateDCache();

            /* Submit frame buffer to GLCDC with Vblank synchronization */
            int err = display_write(display_dev, 0, 0, &desc, g_fb[draw_buf]);
            if (err != 0) {
                printk("display_write error: %d\n", err);
            }

            SCB_CleanInvalidateDCache();
        } else {
            /* Still waiting for the first camera frame from VIN DMA */
            k_msleep(10);
        }

        /* Triple buffer rotation */
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

/* Thread 2: Periodic Heartbeat / Monitoring */
#define TASK2_STACK_SIZE 1024
#define TASK2_PRIORITY   10

static void task2_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        printk("task 2 running...\n");
        k_msleep(7000);
    }
}

K_THREAD_DEFINE(task2_tid, TASK2_STACK_SIZE, task2_entry, NULL, NULL, NULL,
                TASK2_PRIORITY, 0, 0);

int main(void)
{
    printk("Starting Zephyr MIPI-CSI2 Camera Application...\n");
    return 0;
}
