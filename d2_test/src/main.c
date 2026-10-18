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

#include "bsp_api.h"
#include "dave_driver.h"
#include "drw/r_drw_base.h"

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

/* Optional Camera (OV5640) I2C detection */
static void check_camera_ov5640(void)
{
    printk("Resetting Camera (CAMERA_RESET -> P709)...\n");
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(CAMERA_RESET_PIN, BSP_IO_LEVEL_LOW);
    k_msleep(100);
    R_BSP_PinWrite(CAMERA_RESET_PIN, BSP_IO_LEVEL_HIGH);
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
        printk("Camera I2C communication returned %d (camera module may be disconnected)\n", ret);
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

/* Thread 1: Main Graphics Rendering Loop */
#define TASK1_STACK_SIZE 32768
#define TASK1_PRIORITY   10

static void task1_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("\n=== Zephyr Camera I2C & LCD D2D Test Start ===\n");

    /* Cold start stabilization wait */
    k_msleep(500);

    /* SDRAM self test */
    sdram_self_test();

    /* AXI bus arbitration */
    set_bus_arbitration();

    /* Camera verification */
    check_camera_ov5640();

    /* Clear SDRAM framebuffers to black */
    printk("Clearing SDRAM framebuffers to black...\n");
    memset(g_fb[0], 0, FB_SIZE);
    memset(g_fb[1], 0, FB_SIZE);
    memset(g_fb[2], 0, FB_SIZE);
    SCB_CleanInvalidateDCache();

    /* AXI bus slave arbitration: GLCDC high priority (Group 3, Fixed) */
    printk("Setting Bus Slave Arbitration to Fixed Priority (Group 3)...\n");
    for (int i = 0; i < 18; i++) {
        R_BUS->BUSS[i].CNT = 0x0013;
    }

    /* Hardware reset sequence for LCD panel (DISP_RST -> P606) matching Tron */
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

    /* 2D rendering parameters */
    d2_setblendmode(g_d2_handle, d2_bm_one, d2_bm_zero);
    d2_setalphamode(g_d2_handle, d2_am_constant);
    d2_setalpha(g_d2_handle, 0xff);
    d2_setantialiasing(g_d2_handle, 0);

    int ball_x = 200;
    int ball_y = 200;
    int ball_dx = 10;
    int ball_dy = 8;
    int ball_r = 40;

    printk("Starting D/AVE 2D Rendering Loop (SDRAM Triple Buffer Bouncing Ball)...\n");

    uint8_t draw_buf = 0;
    uint8_t pending_buf = 1;
    uint8_t display_buf = 2;
    uint32_t loop_cnt = 0;

    struct display_buffer_descriptor desc = {
        .buf_size = FB_SIZE,
        .width = DISPLAY_HSIZE,
        .height = DISPLAY_VSIZE,
        .pitch = DISPLAY_HSIZE,
    };

    /* Initial display_write to start GLCDC cleanly */
    (void)display_write(display_dev, 0, 0, &desc, g_fb[display_buf]);
    k_msleep(30);

    int64_t last_fps_time = k_uptime_get();
    uint32_t fps_counter = 0;

    int prev_ball_x[3] = {-1, -1, -1};
    int prev_ball_y[3] = {-1, -1, -1};

    while (1) {
        fps_counter++;
        int64_t now = k_uptime_get();
        if (now - last_fps_time >= 1000) {
            printk("FPS: %u (frame time: %lld ms)\n", fps_counter, now - last_fps_time);
            fps_counter = 0;
            last_fps_time = now;
        }

        /* Start frame command recording */
        d2_startframe(g_d2_handle);

        /* Set render target framebuffer */
        d2_framebuffer(g_d2_handle, g_fb[draw_buf], DISPLAY_HSIZE, DISPLAY_HSIZE, DISPLAY_VSIZE, d2_mode_rgb565);

        if (prev_ball_x[draw_buf] < 0) {
            /* First time drawing to this buffer: clear screen to dark navy */
            d2_clear(g_d2_handle, 0xFF10101C);
        } else {
            /* Erase previous ball in this buffer with background color (radius + 3 for anti-fringe) */
            d2_setcolor(g_d2_handle, 0, 0xFF10101C);
            d2_rendercircle(g_d2_handle, prev_ball_x[draw_buf] << 4, prev_ball_y[draw_buf] << 4, (ball_r + 3) << 4, 0);
        }

        /* Border (30px margin) */
        d2_setcolor(g_d2_handle, 0, 0xFF00FF00);
        d2_renderline(g_d2_handle, 30 << 4, 30 << 4, (DISPLAY_HSIZE - 30) << 4, 30 << 4, 2 << 4, 0);
        d2_renderline(g_d2_handle, 30 << 4, (DISPLAY_VSIZE - 30) << 4, (DISPLAY_HSIZE - 30) << 4, (DISPLAY_VSIZE - 30) << 4, 2 << 4, 0);
        d2_renderline(g_d2_handle, 30 << 4, 30 << 4, 30 << 4, (DISPLAY_VSIZE - 30) << 4, 2 << 4, 0);
        d2_renderline(g_d2_handle, (DISPLAY_HSIZE - 30) << 4, 30 << 4, (DISPLAY_HSIZE - 30) << 4, (DISPLAY_VSIZE - 30) << 4, 2 << 4, 0);

        /* Static red circle */
        d2_setcolor(g_d2_handle, 0, 0xFFFF0000);
        d2_rendercircle(g_d2_handle, 300 << 4, 300 << 4, 60 << 4, 0);

        /* Static blue box */
        d2_setcolor(g_d2_handle, 0, 0xFF0000FF);
        d2_renderbox(g_d2_handle, 700 << 4, 200 << 4, 150 << 4, 150 << 4);

        /* Bouncing ball physics */
        ball_x += ball_dx;
        ball_y += ball_dy;

        if (ball_x - ball_r < 32 || ball_x + ball_r > (int)DISPLAY_HSIZE - 32) {
            ball_dx = -ball_dx;
            ball_x += ball_dx;
        }
        if (ball_y - ball_r < 32 || ball_y + ball_r > (int)DISPLAY_VSIZE - 32) {
            ball_dy = -ball_dy;
            ball_y += ball_dy;
        }

        /* Yellow bouncing ball */
        d2_setcolor(g_d2_handle, 0, 0xFFFFFF00);
        d2_rendercircle(g_d2_handle, ball_x << 4, ball_y << 4, ball_r << 4, 0);

        /* Remember ball position drawn in this buffer */
        prev_ball_x[draw_buf] = ball_x;
        prev_ball_y[draw_buf] = ball_y;

        /* End frame recording */
        d2_endframe(g_d2_handle);

        /* Clean cache so display lists in memory are visible to GPU */
        SCB_CleanInvalidateDCache();

        /* Wait for GPU hardware execution to completely finish */
        d2_flushframe(g_d2_handle);

        /* Clean & invalidate cache so pixels written by GPU are coherent */
        SCB_CleanInvalidateDCache();

        /* Submit frame buffer to GLCDC with Vblank wait */
        int err = display_write(display_dev, 0, 0, &desc, g_fb[draw_buf]);
        if (err != 0) {
            printk("display_write error: %d\n", err);
        }

        /* Post-Vblank cache coherence */
        SCB_CleanInvalidateDCache();

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
    /* Zephyr kernel starts threads automatically via K_THREAD_DEFINE */
    printk("Starting Zephyr Dave2D Test Application...\n");
    return 0;
}
