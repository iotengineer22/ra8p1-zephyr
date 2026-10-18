/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "bsp_api.h"
#include "ai_inference.h"
#include "camera_layer.h"
#include "camera_utils.h"
#include "time_counter.h"
#include "ethosu_irq.h"

#include "ethosu_driver.h"
#include "rm_ethosu.h"
#include "rm_ethosu_api.h"

// External declarations for NPU face detection pipeline
extern "C" {
// Stored latest camera frame buffer
extern uint8_t *p_camera_capture_buffer_stored;

// Face detection function from face_detection/MainLoop_obj.cc
bool main_loop_face_detection(void);
}

// Global shared variables
st_ai_detection_point_t g_ai_detection[AI_MAX_DETECTION_NUM] = {0};
st_ai_processing_time_t g_ai_processing_time = {0};
volatile uint32_t g_ai_inference_time_ms = 0;
volatile bool g_ai_result_new = false;
volatile bool g_ai_task_busy = false;

// RAII helper to automatically clear busy flag
struct AIBusyScope {
    AIBusyScope() { g_ai_task_busy = true; }
    ~AIBusyScope() { g_ai_task_busy = false; }
};

// Model input buffer: 192x192 1ch int8 grayscale placed in on-chip SRAM1 at 0x22170000
#define NPU_MODEL_INPUT_SRAM_ADDR (0x22170000UL)
int8_t *model_buffer_int8 = (int8_t *)NPU_MODEL_INPUT_SRAM_ADDR;
uint32_t model_buffer_int8_size = AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL;

// Processing time measurement instance referenced by MainLoop_obj.cc
processinf_time_info_t application_processing_time;


// Semaphore to kick AI inference asynchronously from display loop
static K_SEM_DEFINE(g_ai_kick_sem, 0, 1);

// Redirect e_printf to Zephyr printk
extern "C" int e_printf(const char *format, ...)
{
    char buf[160];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    printk("%s", buf);
    return 0;
}

// Solid time-based detection result persistence
#define AI_DETECTION_HOLD_MS (1500) // Hold detections for 1.5 seconds to guarantee stable, continuous display
static st_ai_detection_point_t s_persistent_detections[AI_MAX_DETECTION_NUM] = {0};
static volatile int s_persistent_count = 0;
static volatile int s_persistent_max_score = 0;
static int64_t s_last_detection_timestamp = 0;

void ai_update_detection_results(const st_ai_detection_point_t *detections, int count)
{
    int64_t now = k_uptime_get();
    unsigned int key = irq_lock();

    if (count > 0) {
        int max_score = 0;
        for (int i = 0; i < count && i < AI_MAX_DETECTION_NUM; i++) {
            s_persistent_detections[i] = detections[i];
            if (detections[i].m_val_percent > max_score) {
                max_score = detections[i].m_val_percent;
            }
        }
        for (int i = count; i < AI_MAX_DETECTION_NUM; i++) {
            memset(&s_persistent_detections[i], 0, sizeof(st_ai_detection_point_t));
        }
        s_persistent_count = count;
        s_persistent_max_score = max_score;
        s_last_detection_timestamp = now;
    } else {
        /* If no detection in this inference cycle, hold existing detections for AI_DETECTION_HOLD_MS */
        if (now - s_last_detection_timestamp > AI_DETECTION_HOLD_MS) {
            memset(s_persistent_detections, 0, sizeof(s_persistent_detections));
            s_persistent_count = 0;
            /* Note: preserve s_persistent_max_score so Score remains readable on HUD */
        }
    }

    // Also update legacy g_ai_detection array for compatibility
    for (int i = 0; i < AI_MAX_DETECTION_NUM; i++) {
        g_ai_detection[i] = s_persistent_detections[i];
    }

    irq_unlock(key);
}

int ai_get_detection_snapshot(st_ai_detection_point_t *out_detections, int max_count, int *out_max_score)
{
    int64_t now = k_uptime_get();
    int count = 0;
    unsigned int key = irq_lock();

    if ((now - s_last_detection_timestamp <= AI_DETECTION_HOLD_MS) && (s_persistent_count > 0)) {
        count = s_persistent_count;
        if (out_max_score) {
            *out_max_score = s_persistent_max_score;
        }
        if (out_detections && max_count > 0) {
            int copy_count = (count < max_count) ? count : max_count;
            for (int i = 0; i < copy_count; i++) {
                out_detections[i] = s_persistent_detections[i];
            }
            for (int i = copy_count; i < max_count; i++) {
                memset(&out_detections[i], 0, sizeof(st_ai_detection_point_t));
            }
        }
    } else {
        if (out_max_score) {
            *out_max_score = s_persistent_max_score; // Preserve last score for display
        }
        if (out_detections && max_count > 0) {
            memset(out_detections, 0, max_count * sizeof(st_ai_detection_point_t));
        }
        count = 0;
    }

    irq_unlock(key);
    return count;
}

// Callback invoked by DetectorPostProcessing to update face detection coordinates (legacy fallback)
extern "C" void update_detection_result(uint16_t index, signed short x, signed short y, signed short w, signed short h, int val_percent)
{
    if (index < AI_MAX_DETECTION_NUM) {
        g_ai_detection[index].m_x = x;
        g_ai_detection[index].m_y = y;
        g_ai_detection[index].m_w = w;
        g_ai_detection[index].m_h = h;
        g_ai_detection[index].m_val_percent = val_percent;
    }
}

// Function to kick AI inference from display thread
void ai_inference_kick(void)
{
    if (!g_ai_task_busy) {
        k_sem_give(&g_ai_kick_sem);
    }
}

// Initialize Arm Ethos-U55 NPU driver
int ai_inference_init(void)
{
    printk("Initializing Arm Ethos-U55 NPU Subsystem...\n");

    /* Connect NPU IRQ to Zephyr NVIC */
    ethosu_irq_connect();

    /* Open Ethos-U55 NPU via Renesas FSP driver */
    fsp_err_t err = RM_ETHOSU_Open(&g_rm_ethosu0_ctrl, &g_rm_ethosu0_cfg);
    if (FSP_SUCCESS != err) {
        printk("ERROR: Failed to open Arm Ethos-U55 NPU (0x%X)\n", err);
        return -1;
    }

    printk("SUCCESS: Arm Ethos-U55 NPU driver initialized successfully!\n");

    /* Initialize DWT cycle counter for precise latency measurement */
    TimeCounter_Init();

    return 0;
}

// AI Inference Zephyr Thread
void ai_inference_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("=== AI Inference Thread Started (NPU-accelerated YOLO-Fastest) ===\n");

    while (1) {
        /* Wait for inference kick from display thread */
        k_sem_take(&g_ai_kick_sem, K_FOREVER);

        AIBusyScope busy_scope;

        if (p_camera_capture_buffer_stored == NULL) {
            continue;
        }

        static bool s_first_ai = true;
        if (s_first_ai) {
            printk("[AI] Kicked, converting image to int8...\n");
        }

        /* 1. Image Pre-processing:
         * Crop and scale 320x240 camera frame (YUV422/RGB565 format) to 192x192 int8 grayscale
         */
        SCB_InvalidateDCache_by_Addr((void *)p_camera_capture_buffer_stored, 320 * 240 * 2);

        vision_ai_app_err_t pre_err = image_rgb565_to_int8(
            p_camera_capture_buffer_stored,
            model_buffer_int8,
            320, 240,
            AI_INPUT_IMAGE_WIDTH, AI_INPUT_IMAGE_HEIGHT
        );

        if (pre_err != VISION_AI_APP_SUCCESS) {
            printk("ERROR: Image pre-processing failed (0x%X)\n", pre_err);
            continue;
        }

        if (s_first_ai) {
            printk("[AI] Image converted, invoking main_loop_face_detection...\n");
        }

        /* 2. Execute NPU inference and post-processing */
        bool success = main_loop_face_detection();

        if (s_first_ai) {
            printk("[AI] main_loop_face_detection finished (success=%d, time=%lu ms)\n",
                   success, (unsigned long)g_ai_inference_time_ms);
            s_first_ai = false;
        }

        if (success) {
            g_ai_result_new = true;
        } else {
            printk("ERROR: NPU face detection execution failed!\n");
        }
    }

}
