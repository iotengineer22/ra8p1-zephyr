/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>
#include <string.h>

#include "bsp_api.h"
#include "dave_driver.h"
#include "r_drw_base.h"
#include "mipi_vin_config.h"
#include "camera_layer.h"
#include "ai_inference.h"
#include "bg_font_18_full.h"

#define DISPLAY_HSIZE 1024
#define DISPLAY_VSIZE 600
#define FB_SIZE       (DISPLAY_HSIZE * DISPLAY_VSIZE * 2) /* RGB565: 1,228,800 bytes */

/* Triple Framebuffers in External 64MB SDRAM */
#define SDRAM_BASE_ADDR 0x68000000UL
static uint8_t *const g_fb[3] = {
    (uint8_t *)(SDRAM_BASE_ADDR),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE * 2)
};

static d2_device *g_d2_handle = NULL;

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

/* Thread 1: Main Camera Capture, D/AVE 2D LCD Display & AI Kick Loop */
#define TASK_DISP_STACK_SIZE 32768
#define TASK_DISP_PRIORITY   10

static void task_disp_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("\n=== Zephyr YOLO Face Detection (Cortex-M85 CPU + MIPI Camera) Start ===\n");

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

    /* Initialize CPU TFLite Micro Subsystem */
    if (ai_inference_init() != 0) {
        printk("ERROR: CPU TFLite Micro initialization failed!\n");
    }

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

    printk("Entering Real-time Camera Display Loop (Bilinear Upscale 320x240 -> 800x600)...\n");

    while (1) {
        fps_counter++;
        int64_t now = k_uptime_get();
        static int s_disp_face_cnt = 0;
        static int s_disp_score = 0;
        if (now - last_fps_time >= 1000) {
            printk("FPS: %u | CamBuf: 0x%08X | Faces: %d | Score: %d%% | AI: %lu ms\n",
                   fps_counter, (uint32_t)p_camera_capture_buffer_stored,
                   s_disp_face_cnt, s_disp_score, (unsigned long)g_ai_inference_time_ms);
            fps_counter = 0;
            last_fps_time = now;
        }

        if (p_camera_capture_buffer_stored != NULL) {

            /* Synchronize data cache for captured camera buffer */
            SCB_InvalidateDCache_by_Addr(p_camera_capture_buffer_stored, (int32_t)(320 * 240 * 2));

            /* 1. Start GPU frame command recording */
            d2_startframe(g_d2_handle);

            /* Set render target framebuffer */
            d2_framebuffer(g_d2_handle, g_fb[draw_buf], DISPLAY_HSIZE, DISPLAY_HSIZE, DISPLAY_VSIZE, d2_mode_rgb565);

            /* Clear left unused margin (X: 0 to 111) */
            d2_setcolor(g_d2_handle, 0, 0xFF000000);
            d2_renderbox(g_d2_handle, 0 << 4, 0 << 4, 112 << 4, 600 << 4);

            /* Set camera frame as texture source */
            d2_setblitsrc(g_d2_handle, (void *)p_camera_capture_buffer_stored, 320, 320, 240, d2_mode_rgb565);

            /* Bilinear upscale: 320x240 -> 800x600, Centered at X=112, Y=0 */
            d2_blitcopy(g_d2_handle,
                        320, 240,
                        0, 0,
                        800 << 4, 600 << 4,  /* Target width and height (16.4 fixed-point) */
                        112 << 4, 0 << 4,    /* Destination X and Y (centered) */
                        d2_tm_filter);       /* Bilinear filtering */

            /* 2. Snapshot AI detection results for this frame */
            st_ai_detection_point_t frame_detections[AI_MAX_DETECTION_NUM];
            int max_face_score = 0;
            int active_face_count = ai_get_detection_snapshot(frame_detections, AI_MAX_DETECTION_NUM, &max_face_score);

            /* Draw detected face bounding boxes with D/AVE 2D */
            d2_setcolor(g_d2_handle, 0, 0xFF00FF00); // Bright green
            for (int i = 0; i < active_face_count; i++) {
                if (frame_detections[i].m_w > 0 && frame_detections[i].m_h > 0) {
                    /* Convert from 192x192 crop coordinate to LCD 800x600 coordinate */
                    float fx = (float)frame_detections[i].m_x * 3.125f + 212.0f;
                    float fy = (float)frame_detections[i].m_y * 3.125f;
                    float fw = (float)frame_detections[i].m_w * 3.125f;
                    float fh = (float)frame_detections[i].m_h * 3.125f;

                    d2_point x1 = (d2_point)(fx * 16.0f);
                    d2_point y1 = (d2_point)(fy * 16.0f);
                    d2_point x2 = (d2_point)((fx + fw) * 16.0f);
                    d2_point y2 = (d2_point)((fy + fh) * 16.0f);

                    d2_width border_width = 3 * 16; // 3px width for solid visibility

                    d2_renderline(g_d2_handle, x1, y1, x2, y1, border_width, 0); // Top
                    d2_renderline(g_d2_handle, x2, y1, x2, y2, border_width, 0); // Right
                    d2_renderline(g_d2_handle, x2, y2, x1, y2, border_width, 0); // Bottom
                    d2_renderline(g_d2_handle, x1, y2, x1, y1, border_width, 0); // Left
                }
            }

            /* End frame command recording */
            d2_endframe(g_d2_handle);

            /* CRITICAL FIX FOR PERSISTENT HUD:
             * D/AVE 2D has a built-in double-buffered command queue. By design, d2_endframe()
             * only closes the buffer and waits for the PREVIOUS frame. The commands recorded
             * above (camera blitcopy & bboxes) are NOT dispatched to hardware until d2_startframe().
             * Calling d2_startframe() here IMMEDIATELY dispatches the current frame's commands,
             * and d2_flushframe() ensures the GPU completely finishes rendering to g_fb[draw_buf]
             * BEFORE the CPU draws the HUD overlay! This prevents the GPU from overwriting the HUD
             * in the next frame.
             */
            d2_startframe(g_d2_handle);
            d2_flushframe(g_d2_handle);

            /* Invalidate CPU D-cache so CPU sees fresh pixels written by GPU */
            SCB_CleanInvalidateDCache();

            /* CPU Direct Software Text & Overlay Rendering onto g_fb[draw_buf] */
            uint16_t *fb16 = (uint16_t *)g_fb[draw_buf];

            /* Top-Left UI Status Panel: Always display AI Time, Face Detection Score, and Face Count */
            static int s_persistent_score = 0;
            static int s_persistent_faces = 0;
            if (max_face_score > 0) {
                s_persistent_score = max_face_score;
            }
            if (active_face_count > 0) {
                s_persistent_faces = active_face_count;
            }

            char time_str[32];
            snprintf(time_str, sizeof(time_str), "AI Time: %lu ms", (unsigned long)g_ai_inference_time_ms);
            char score_str[32];
            if (s_persistent_score > 0) {
                snprintf(score_str, sizeof(score_str), "Score  : %d%%", s_persistent_score);
            } else {
                snprintf(score_str, sizeof(score_str), "Score  : 0%%");
            }
            char count_str[32];
            if (s_persistent_faces > 0) {
                snprintf(count_str, sizeof(count_str), "Faces  : %d", s_persistent_faces);
            } else {
                snprintf(count_str, sizeof(count_str), "Faces  : 0");
            }

            /* Panel geometry: X=118, Y=8, W=216, H=78 */
            lcd_fill_rect_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, 118, 8, 216, 78, 0x0000); // Black background
            lcd_draw_rect_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, 118, 8, 216, 78, 0x07E0); // Bright green border outer
            lcd_draw_rect_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, 119, 9, 214, 76, 0x07E0); // Bright green border inner (2px thick)

            lcd_draw_string_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, 124, 12, time_str, 0x07E0, true, 0x0000);
            lcd_draw_string_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, 124, 36, score_str, 0x07E0, true, 0x0000);
            lcd_draw_string_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, 124, 60, count_str, 0x07E0, true, 0x0000);

            /* Per-Face Confidence Badges above detected bounding boxes */
            for (int i = 0; i < active_face_count; i++) {
                if (frame_detections[i].m_w > 0 && frame_detections[i].m_h > 0) {
                    float fx = (float)frame_detections[i].m_x * 3.125f + 212.0f;
                    float fy = (float)frame_detections[i].m_y * 3.125f;

                    char val_str[16];
                    snprintf(val_str, sizeof(val_str), "%d%%", frame_detections[i].m_val_percent);

                    int tag_x = (int)fx;
                    int tag_y = (int)fy - 25;
                    if (tag_y < 8) {
                        tag_y = (int)fy + 6;
                    }

                    int str_w = lcd_get_string_width(val_str);
                    int tag_w = str_w + 8;
                    int tag_h = 24;

                    /* Background tag pill */
                    lcd_fill_rect_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, tag_x, tag_y, tag_w, tag_h, 0x0000);
                    lcd_draw_rect_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, tag_x, tag_y, tag_w, tag_h, 0x07E0);
                    lcd_draw_string_rgb565(fb16, DISPLAY_HSIZE, DISPLAY_VSIZE, tag_x + 4, tag_y + 1, val_str, 0x07E0, false, 0);
                }
            }

            s_disp_face_cnt = s_persistent_faces;
            s_disp_score = s_persistent_score;

            /* Clean D-Cache so CPU writes to SDRAM framebuffer are committed before GLCDC read */
            SCB_CleanDCache();

            /* Submit frame buffer to GLCDC with Vblank synchronization */
            int err = display_write(display_dev, 0, 0, &desc, g_fb[draw_buf]);
            if (err != 0) {
                printk("display_write error: %d\n", err);
            }

            /* Kick AI inference asynchronously (non-blocking) */
            ai_inference_kick();
        } else {
            /* Still waiting for the first camera frame from VIN DMA */
            k_msleep(10);
        }

        k_yield();


        /* Triple buffer rotation */
        uint8_t next_draw    = display_buf;
        uint8_t next_pending = draw_buf;
        uint8_t next_display = pending_buf;

        draw_buf    = next_draw;
        pending_buf = next_pending;
        display_buf = next_display;

        loop_cnt++;
    }
}

/* Thread 2: Heartbeat Monitor Thread */
#define TASK2_STACK_SIZE 1024
#define TASK2_PRIORITY   10

static void task2_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        k_msleep(7000);
        printk("task 2 running...\n");
    }
}

/* Thread definitions */
K_THREAD_DEFINE(task_disp_tid, TASK_DISP_STACK_SIZE, task_disp_entry, NULL, NULL, NULL,
                TASK_DISP_PRIORITY, 0, 0);

K_THREAD_DEFINE(task_ai_tid, 32768, ai_inference_thread_entry, NULL, NULL, NULL,
                11, 0, 0);

K_THREAD_DEFINE(task2_tid, TASK2_STACK_SIZE, task2_entry, NULL, NULL, NULL,
                TASK2_PRIORITY, 0, 0);
