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

/* Edge Impulse C++ SDK */
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "edge-impulse-sdk/dsp/numpy.hpp"

/* Dummy UART pointer for Edge Impulse porting layer */
extern "C" const struct device *uart = NULL;

#define DISPLAY_HSIZE   1024
#define DISPLAY_VSIZE   600
#define PIXEL_SIZE      2 // RGB565 (16-bit)
#define FB_SIZE         (DISPLAY_HSIZE * DISPLAY_VSIZE * PIXEL_SIZE) // 1,228,800 bytes

#define SDRAM_BASE_ADDR 0x68000000UL

/* Triple buffer pointers allocated in physical SDRAM */
static uint8_t *const g_fb[3] = {
    (uint8_t *)(SDRAM_BASE_ADDR),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE),
    (uint8_t *)(SDRAM_BASE_ADDR + FB_SIZE * 2)
};

static d2_device *g_d2_handle = NULL;
static volatile uint32_t g_render_frame_count = 0;

/* AI Detection Results Shared State */
static char g_ai_detected_buf[64] = "DETECTED: -";
static char g_ai_timing_buf[64]   = "DSP: - ms | CPU: - ms";
static int  g_class_probs[EI_CLASSIFIER_LABEL_COUNT] = {0};
static volatile int g_ai_inference_count = 0;
static volatile int g_best_class_idx = -1;
static volatile int g_best_prob = 0;

/* Audio get_data callback for Edge Impulse (reads from 16kHz circular buffer) */
static int get_audio_data(size_t offset, size_t length, float *out_ptr)
{
    for (size_t i = 0; i < length; i++)
    {
        uint32_t idx = (g_audio_buffer_write_ptr + offset + i) % AUDIO_BUFFER_SIZE;
        out_ptr[i] = (float)g_audio_buffer[idx];
    }
    return 0;
}

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

/* Custom hardware-accelerated 5x7 font rendering using D/AVE 2D */
static void draw_char_5x7(d2_device *handle, int x, int y, int scaling, int c, d2_color color)
{
    if (c < 32 || c > 126) return;
    const unsigned char *glyph = font5x7[c - 32];

    d2_setcolor(handle, 0, color);
    for (int col = 0; col < 5; col++)
    {
        unsigned char line = glyph[col];
        int row = 0;
        while (row < 7)
        {
            if (line & (1 << row))
            {
                int run_start = row;
                while (row < 7 && (line & (1 << row)))
                {
                    row++;
                }
                int run_len = row - run_start;
                d2_renderbox(handle,
                             (d2_point)((x + col * scaling) << 4),
                             (d2_point)((y + run_start * scaling) << 4),
                             (d2_width)(scaling << 4),
                             (d2_width)((run_len * scaling) << 4));
            }
            else
            {
                row++;
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

/* Pre-render static background onto all 3 framebuffers */
static void prerender_static_ui(void)
{
    for (int buf_idx = 0; buf_idx < 3; buf_idx++)
    {
        d2_startframe(g_d2_handle);
        d2_framebuffer(g_d2_handle, g_fb[buf_idx], DISPLAY_HSIZE, DISPLAY_HSIZE, DISPLAY_VSIZE, d2_mode_rgb565);

        /* 1. Deep Space Navy Background (0xFF0C0E14) */
        d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
        d2_renderbox(g_d2_handle, 0, 0, DISPLAY_HSIZE << 4, DISPLAY_VSIZE << 4);

        /* 2. Top Header Card (Y: 10, H: 65, W: 964) */
        d2_setcolor(g_d2_handle, 0, 0xFF141724);
        d2_renderbox(g_d2_handle, 30 << 4, 10 << 4, 964 << 4, 65 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF2A2E44);
        d2_renderline(g_d2_handle, 30 << 4, 10 << 4, 994 << 4, 10 << 4, 2 << 4, 0);
        d2_renderline(g_d2_handle, 30 << 4, 75 << 4, 994 << 4, 75 << 4, 2 << 4, 0);

        print_string_5x7(g_d2_handle, 45, 18, 2, "EK-RA8P1 ZEPHYR RTOS + CORTEX-M85 CPU AUDIO DETECT", 0xFF00FFCC);
        print_string_5x7(g_d2_handle, 45, 45, 2, "Hardware: Cortex-M85 CPU (480MHz, CMSIS-NN) | PDM Mic 32kHz", 0xFF88A0C0);

        /* 3. AI Detection & Results Card (Y: 85, H: 175, W: 964) */
        d2_setcolor(g_d2_handle, 0, 0xFF141724);
        d2_renderbox(g_d2_handle, 30 << 4, 85 << 4, 964 << 4, 175 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF2A2E44);
        d2_renderbox(g_d2_handle, 30 << 4, 85 << 4, 964 << 4, 175 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF10121D);
        d2_renderbox(g_d2_handle, 32 << 4, 87 << 4, 960 << 4, 171 << 4);

        print_string_5x7(g_d2_handle, 45, 95, 2, "AI KEYWORD DETECT (5-CLASS CPU)", 0xFF00E5FF);

        /* Static Card outline for Detected Result (Left half: X: 45, Y: 125, W: 450, H: 120) */
        d2_setcolor(g_d2_handle, 0, 0xFF1E2235);
        d2_renderbox(g_d2_handle, 45 << 4, 125 << 4, 450 << 4, 120 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF0F111A);
        d2_renderbox(g_d2_handle, 47 << 4, 127 << 4, 446 << 4, 116 << 4);

        /* Right half: Class Probabilities Bar Chart Header (X: 515, Y: 95) */
        print_string_5x7(g_d2_handle, 515, 95, 2, "CLASS PROBABILITIES (0-100%):", 0xFFE0E0E0);

        /* Static labels for the 5 classes */
        const char *classes[5] = { "down", "left", "noise", "right", "up" };
        for (int c = 0; c < 5; c++)
        {
            int row_y = 125 + c * 24;
            print_string_5x7(g_d2_handle, 515, row_y + 2, 2, classes[c], 0xFFB0B8D0);
            /* Bar frame outline */
            d2_setcolor(g_d2_handle, 0, 0xFF2A2E44);
            d2_renderbox(g_d2_handle, 590 << 4, row_y << 4, 320 << 4, 18 << 4);
            d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
            d2_renderbox(g_d2_handle, 592 << 4, (row_y + 2) << 4, 316 << 4, 14 << 4);
        }

        /* 4. Live Audio Waveform Box (Y: 270, H: 200, W: 964) */
        d2_setcolor(g_d2_handle, 0, 0xFF141724);
        d2_renderbox(g_d2_handle, 30 << 4, 270 << 4, 964 << 4, 200 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF2A2E44);
        d2_renderbox(g_d2_handle, 30 << 4, 270 << 4, 964 << 4, 200 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
        d2_renderbox(g_d2_handle, 32 << 4, 272 << 4, 960 << 4, 196 << 4);

        print_string_5x7(g_d2_handle, 45, 280, 2, "LIVE PDM MICROPHONE OSCILLOSCOPE (1024 SAMPLES)", 0xFF00FF66);

        /* 5. Audio Volume & Subsystem Status (Y: 480, H: 105, W: 964) */
        d2_setcolor(g_d2_handle, 0, 0xFF141724);
        d2_renderbox(g_d2_handle, 30 << 4, 480 << 4, 964 << 4, 105 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF2A2E44);
        d2_renderbox(g_d2_handle, 30 << 4, 480 << 4, 964 << 4, 105 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
        d2_renderbox(g_d2_handle, 32 << 4, 482 << 4, 960 << 4, 101 << 4);

        print_string_5x7(g_d2_handle, 45, 490, 2, "AUDIO LEVEL (RMS VOLUME METER):", 0xFFE0E0E0);

        /* Volume bar background groove */
        d2_setcolor(g_d2_handle, 0, 0xFF202435);
        d2_renderbox(g_d2_handle, 45 << 4, 515 << 4, 934 << 4, 22 << 4);
        d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
        d2_renderbox(g_d2_handle, 47 << 4, 517 << 4, 930 << 4, 18 << 4);

        d2_endframe(g_d2_handle);
        d2_flushframe(g_d2_handle);
    }
}

/* Thread 1: High-Performance UI Render Task (Priority 10) */
#define TASK1_STACK_SIZE 8192
#define TASK1_PRIORITY   10

static void task1_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Configure board MIPI/LCD multiplexer switch (P108 LOW matching Tron) */
    const struct device *gpio1 = DEVICE_DT_GET(DT_NODELABEL(ioport1));
    if (device_is_ready(gpio1)) {
        gpio_pin_configure(gpio1, 8, GPIO_OUTPUT_INACTIVE); // P108 LOW
    }

    /* Safe Power & Reset Sequencing: Backlight OFF -> Reset -> Display Open -> Clocks -> Backlight ON */
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
    d2_setdlistblocksize(g_d2_handle, 2048);

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

    printk("Starting D/AVE 2D Rendering Loop (PDM Audio + CPU Visualizer)...\n");

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

    /* Allow LCD panel internal charge pumps to stabilize with active clocks */
    k_msleep(100);

    /* Turn Backlight ON safely */
    if (device_is_ready(gpio5)) {
        gpio_pin_set(gpio5, 14, 1);
        printk("LCD Backlight enabled (safe power-on sequencing complete).\n");
    }

    while (1) {
        g_render_frame_count++;

        /* Start D2D frame */
        d2_startframe(g_d2_handle);
        d2_framebuffer(g_d2_handle, g_fb[draw_buf], DISPLAY_HSIZE, DISPLAY_HSIZE, DISPLAY_VSIZE, d2_mode_rgb565);

        /* 1. Clear Detected Result Inner Box (X: 48, Y: 128, W: 444, H: 114) */
        d2_setcolor(g_d2_handle, 0, 0xFF0F111A);
        d2_renderbox(g_d2_handle, 48 << 4, 128 << 4, 444 << 4, 114 << 4);

        /* 2. Render AI Detection Text inside Result Box via D/AVE 2D */
        d2_color text_col = (g_best_prob >= 50 && g_best_class_idx != 2) ? 0xFF00FF66 : 0xFF00E5FF;
        print_string_5x7(g_d2_handle, 58, 140, 3, g_ai_detected_buf, text_col);
        print_string_5x7(g_d2_handle, 58, 178, 2, g_ai_timing_buf, 0xFFCECECE);

        char infer_stat[64];
        snprintf(infer_stat, sizeof(infer_stat), "INFERENCES: %-6d  STATUS: %s",
                 g_ai_inference_count, g_audio_buffer_ready ? "ACTIVE" : "INIT");
        print_string_5x7(g_d2_handle, 58, 208, 2, infer_stat, 0xFF88A0B0);

        /* 3. Class Probabilities Bars and Percentage text */
        for (int c = 0; c < 5; c++)
        {
            int row_y = 125 + c * 24;
            d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
            d2_renderbox(g_d2_handle, 593 << 4, (row_y + 3) << 4, 314 << 4, 12 << 4);

            int prob = g_class_probs[c];
            if (prob > 100) prob = 100;
            if (prob > 0)
            {
                int bar_w = (prob * 314) / 100;
                d2_color bar_color = (c == g_best_class_idx && prob >= 50) ? 0xFF00FF66 : 0xFF00A2FF;
                d2_setcolor(g_d2_handle, 0, bar_color);
                d2_renderbox(g_d2_handle, 593 << 4, (row_y + 3) << 4, bar_w << 4, 12 << 4);
            }

            /* Clear & render percentage text next to bar */
            char p_str[16];
            snprintf(p_str, sizeof(p_str), "%3d%%", prob);
            d2_setcolor(g_d2_handle, 0, 0xFF10121D);
            d2_renderbox(g_d2_handle, 915 << 4, row_y << 4, 60 << 4, 18 << 4);
            print_string_5x7(g_d2_handle, 920, row_y + 1, 2, p_str, 0xFFFFFFFF);
        }

        /* 4. Clear Waveform Inner Area (X: 45, Y: 305, W: 934, H: 155) */
        d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
        d2_renderbox(g_d2_handle, 45 << 4, 305 << 4, 934 << 4, 155 << 4);

        /* Zero-line reference (Y: 382) */
        d2_setcolor(g_d2_handle, 0, 0xFF22283A);
        d2_renderline(g_d2_handle, 45 << 4, 382 << 4, 979 << 4, 382 << 4, 1 << 4, 0);

        /* Draw Oscilloscope Waveform */
        d2_setcolor(g_d2_handle, 0, 0xFF00FF66); // Neon spring green
        int step = 2;
        int prev_x = 45;
        int prev_y = 382;

        for (int i = 0; i < 934; i += step)
        {
            int idx = i * WAVEFORM_POINTS / 934;
            if (idx >= WAVEFORM_POINTS) idx = WAVEFORM_POINTS - 1;

            int16_t sample16 = g_waveform_history[idx];
            int32_t y_offset = ((int32_t)sample16 * 70) / 256;
            int y_coord = 382 - y_offset;
            int x_coord = 45 + i;

            if (y_coord < 308) y_coord = 308;
            if (y_coord > 456) y_coord = 456;

            if (i > 0)
            {
                d2_renderline(g_d2_handle, prev_x << 4, prev_y << 4, x_coord << 4, y_coord << 4, 2 << 4, 0);
            }
            prev_x = x_coord;
            prev_y = y_coord;
        }

        /* 5. Clear Volume Bar Inner Area */
        d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
        d2_renderbox(g_d2_handle, 48 << 4, 518 << 4, 928 << 4, 16 << 4);

        /* 6. Render Filled Volume Bar based on RMS */
        int32_t vol_width = ((int32_t)g_rms_val * 928) / 256;
        if (vol_width > 928) vol_width = 928;
        if (vol_width > 0)
        {
            d2_setcolor(g_d2_handle, 0, 0xFF00E5FF); // Neon cyan
            d2_renderbox(g_d2_handle, 48 << 4, 518 << 4, vol_width << 4, 16 << 4);
        }

        /* 7. Clear & Render Audio Subsystem Status at Bottom */
        char audio_status[128];
        snprintf(audio_status, sizeof(audio_status),
                 "RMS: %-5u  PK-PK: %-6d  PDM IRQ: %-8u  STATUS: %s",
                 (unsigned int)g_rms_val, (int)g_peak_to_peak, (unsigned int)g_pdm_event_count,
                 (g_pdm_open_err == 0 && g_pdm_start_err == 0) ? "NORMAL" : "ERROR");
        d2_setcolor(g_d2_handle, 0, 0xFF0C0E14);
        d2_renderbox(g_d2_handle, 45 << 4, 550 << 4, 934 << 4, 22 << 4);
        print_string_5x7(g_d2_handle, 45, 552, 2, audio_status, 0xFF88A0C0);

        /* End frame command recording & execute GPU rendering */
        d2_endframe(g_d2_handle);
        d2_flushframe(g_d2_handle);

        /* Monitor GLCDC Graphics 1 FIFO Underflow */
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
        printk("FPS: %u | PDM Events: %u | RMS: %u | Pk-Pk: %d | AI Inferences: %d\n",
               fps, (unsigned int)g_pdm_event_count, (unsigned int)g_rms_val, (int)g_peak_to_peak, g_ai_inference_count);
    }
}

K_THREAD_DEFINE(task2_tid, TASK2_STACK_SIZE, task2_entry, NULL, NULL, NULL,
                TASK2_PRIORITY, 0, 0);

/* Thread 3: Cortex-M85 CPU Voice Inference Thread (Priority 11) */
#define TASK_AI_STACK_SIZE 32768
#define TASK_AI_PRIORITY   11

static void task_ai_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Allow display and PDM drivers to stabilize */
    k_msleep(1500);

    printk("=== Starting Cortex-M85 CPU Voice Detection Thread (CMSIS-NN)... ===\n");

    while (1)
    {
        /* Run inference every 250ms (4 times per second) */
        k_msleep(250);

        if (!g_audio_buffer_ready)
        {
            continue;
        }

        /* Setup signal struct for Edge Impulse */
        signal_t signal;
        signal.total_length = AUDIO_BUFFER_SIZE;
        signal.get_data = &get_audio_data;

        ei_impulse_result_t result = { 0 };

        /* Run classifier on Cortex-M85 CPU via CMSIS-NN EON compiled model */
        EI_IMPULSE_ERROR res = run_classifier(&signal, &result, false);

        if (res == EI_IMPULSE_OK)
        {
            g_ai_inference_count++;

            /* Find class with highest probability */
            int max_idx = -1;
            float max_prob = 0.0f;
            for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++)
            {
                if (result.classification[i].value > max_prob)
                {
                    max_prob = result.classification[i].value;
                    max_idx = (int)i;
                }
                g_class_probs[i] = (int)(result.classification[i].value * 100.0f);
            }

            if (max_idx >= 0)
            {
                g_best_class_idx = max_idx;
                g_best_prob = (int)(max_prob * 100.0f);

                snprintf(g_ai_detected_buf, sizeof(g_ai_detected_buf), "DETECTED: %s (%d%%)",
                         result.classification[max_idx].label,
                         g_best_prob);

                int dsp_ms = (int)(result.timing.dsp_us / 1000);
                int dsp_us = (int)(result.timing.dsp_us % 1000);
                int cpu_ms = (int)(result.timing.classification_us / 1000);
                int cpu_us = (int)(result.timing.classification_us % 1000);

                snprintf(g_ai_timing_buf, sizeof(g_ai_timing_buf),
                         "DSP: %d.%03d ms | CPU: %d.%03d ms",
                         dsp_ms, dsp_us, cpu_ms, cpu_us);

                printk("[AI-CPU] %s | %s\n", g_ai_detected_buf, g_ai_timing_buf);
            }
        }
        else
        {
            printk("[AI-CPU] run_classifier failed with %d\n", res);
            snprintf(g_ai_detected_buf, sizeof(g_ai_detected_buf), "DETECTED: ERROR (%d)", res);
        }
    }
}

K_THREAD_DEFINE(task_ai_tid, TASK_AI_STACK_SIZE, task_ai_entry, NULL, NULL, NULL,
                TASK_AI_PRIORITY, 0, 0);

int main(void)
{
    printk("Starting Zephyr PDM Microphone + Cortex-M85 CPU Voice Classifier...\n");
    return 0;
}
