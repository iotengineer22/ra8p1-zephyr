/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <vector>

#include "bsp_api.h"
#include "ai_inference.h"
#include "camera_layer.h"
#include "camera_utils.h"
#include "time_counter.h"

#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "yolo_fastest_model_data.h"
#include "DetectorPostProcessing.hpp"
#include "DetectionResult.hpp"

extern "C" {
// Stored latest camera frame buffer from camera layer
extern uint8_t *p_camera_capture_buffer_stored;

// Register TFLM debug log callback
extern void RegisterDebugLogCallback(void (*cb)(const char* s));

int e_printf(const char *format, ...);
void update_detection_result(uint16_t index, signed short x, signed short y, signed short w, signed short h, int val_percent);
}

// Redirect TFLM debug log to Zephyr printk
static void ai_debug_log_callback(const char* s)
{
    printk("%s", s);
}

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

// Global shared variables
st_ai_detection_point_t g_ai_detection[AI_MAX_DETECTION_NUM] = {0};
st_ai_processing_time_t g_ai_processing_time = {0};
volatile uint32_t g_ai_inference_time_ms = 0;
volatile bool g_ai_result_new = false;
volatile bool g_ai_task_busy = false;

// Tensor Arena placed in external 64MB SDRAM (at offset 8MB: 0x68800000)
#define TENSOR_ARENA_ADDR 0x68800000UL
#define TENSOR_ARENA_SIZE (2048 * 1024) // 2MB arena for TFLM
static uint8_t *tensor_arena = (uint8_t *)TENSOR_ARENA_ADDR;

// 192x192 1ch int8 grayscale model input buffer
static int8_t model_input_buffer[AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL];

// YOLO-Fastest anchor definitions
const float anchor1[] = {38.f, 77.f, 47.f, 97.f, 61.f, 126.f};
const float anchor2[] = {14.f, 26.f, 19.f, 37.f, 28.f, 55.f};

// TFLM objects
static const tflite::Model* g_model = nullptr;
static tflite::MicroInterpreter* g_interpreter = nullptr;
static tflite::MicroMutableOpResolver<15>* g_resolver = nullptr;

// Semaphore to kick AI inference from display loop
static K_SEM_DEFINE(g_ai_kick_sem, 0, 1);

// RAII helper to automatically clear busy flag
struct AIBusyScope {
    AIBusyScope() { g_ai_task_busy = true; }
    ~AIBusyScope() { g_ai_task_busy = false; }
};

// Persistent detection results for smooth HUD overlay (hold for 3 seconds during CPU inference)
#define AI_DETECTION_HOLD_MS (3000)
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
        if (now - s_last_detection_timestamp > AI_DETECTION_HOLD_MS) {
            memset(s_persistent_detections, 0, sizeof(s_persistent_detections));
            s_persistent_count = 0;
        }
    }

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
            *out_max_score = s_persistent_max_score;
        }
        if (out_detections && max_count > 0) {
            memset(out_detections, 0, max_count * sizeof(st_ai_detection_point_t));
        }
        count = 0;
    }

    irq_unlock(key);
    return count;
}

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

void ai_inference_kick(void)
{
    if (!g_ai_task_busy) {
        k_sem_give(&g_ai_kick_sem);
    }
}

int ai_inference_init(void)
{
    printk("=== Initializing CPU TFLite Micro Subsystem (YOLO-Fastest) ===\n");

    // Connect TFLM debug log output
    RegisterDebugLogCallback(ai_debug_log_callback);

    // Initialize DWT cycle counter
    TimeCounter_Init();

    // Clear tensor arena in SDRAM
    memset(tensor_arena, 0, TENSOR_ARENA_SIZE);

    // Load model flatbuffer
    g_model = tflite::GetModel(yolo_fastest_192_face_v4_tflite);
    if (g_model->version() != TFLITE_SCHEMA_VERSION) {
        printk("ERROR: Model schema version %d does not match runtime %d\n",
               (int)g_model->version(), (int)TFLITE_SCHEMA_VERSION);
        return -1;
    }

    // Configure required CPU operators
    static tflite::MicroMutableOpResolver<15> resolver;
    resolver.AddConv2D();
    resolver.AddDepthwiseConv2D();
    resolver.AddLeakyRelu();
    resolver.AddAdd();
    resolver.AddReshape();
    resolver.AddConcatenation();
    resolver.AddPad();
    resolver.AddLogistic();
    resolver.AddMaxPool2D();
    resolver.AddResizeNearestNeighbor();
    g_resolver = &resolver;

    static tflite::MicroInterpreter interpreter(g_model, *g_resolver, tensor_arena, TENSOR_ARENA_SIZE);
    g_interpreter = &interpreter;

    TfLiteStatus allocate_status = g_interpreter->AllocateTensors();
    if (allocate_status != kTfLiteOk) {
        printk("ERROR: AllocateTensors() failed with status %d\n", (int)allocate_status);
        return -1;
    }

    TfLiteTensor* input_tensor = g_interpreter->input(0);
    printk("SUCCESS: TensorFlow Lite Micro CPU initialized successfully!\n");
    printk("Input tensor size: %d bytes, shape: %d x %d x %d\n",
           (int)input_tensor->bytes,
           (int)input_tensor->dims->data[1],
           (int)input_tensor->dims->data[2],
           (int)input_tensor->dims->data[3]);

    return 0;
}

void ai_inference_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    printk("=== AI Inference Thread Started (CPU-based YOLO-Fastest) ===\n");

    // Wait until main thread finishes initialization
    while (g_interpreter == nullptr) {
        k_msleep(50);
    }

    TfLiteTensor* input_tensor = g_interpreter->input(0);
    TfLiteTensor* output_tensor0 = g_interpreter->output(0);
    TfLiteTensor* output_tensor1 = g_interpreter->output(1);

    static uint32_t s_infer_cnt = 0;

    while (1) {
        k_sem_take(&g_ai_kick_sem, K_FOREVER);

        AIBusyScope busy_scope;

        if (p_camera_capture_buffer_stored == NULL) {
            continue;
        }

        int64_t start_uptime = k_uptime_get();

        // 1. Invalidate D-cache for camera buffer
        SCB_InvalidateDCache_by_Addr((void *)p_camera_capture_buffer_stored, 320 * 240 * 2);

        // Pre-processing: RGB565 -> int8 grayscale (192x192)
        vision_ai_app_err_t pre_err = image_rgb565_to_int8(
            p_camera_capture_buffer_stored,
            model_input_buffer,
            320, 240,
            AI_INPUT_IMAGE_WIDTH, AI_INPUT_IMAGE_HEIGHT
        );
        if (pre_err != VISION_AI_APP_SUCCESS) {
            printk("ERROR: Image pre-processing failed (0x%X)\n", pre_err);
            continue;
        }

        // Copy pre-processed image into TFLM input tensor
        memcpy(input_tensor->data.int8, model_input_buffer, input_tensor->bytes);
        SCB_CleanDCache_by_Addr((void *)input_tensor->data.int8, input_tensor->bytes);

        int64_t invoke_start_uptime = k_uptime_get();
        uint32_t pre_ms = (uint32_t)(invoke_start_uptime - start_uptime);
        g_ai_processing_time.ai_inference_pre_processing_time_ms = pre_ms;

        // 2. CPU Inference Invoke
        TfLiteStatus invoke_status = g_interpreter->Invoke();

        int64_t invoke_end_uptime = k_uptime_get();
        uint32_t inv_ms = (uint32_t)(invoke_end_uptime - invoke_start_uptime);
        g_ai_processing_time.ai_inference_time_ms = inv_ms;
        g_ai_inference_time_ms = inv_ms;

        if (invoke_status != kTfLiteOk) {
            printk("ERROR: CPU Inference Invoke failed (status %d)\n", (int)invoke_status);
            continue;
        }

        // 3. Post-Processing (YOLO-Fastest Decode & NMS)
        int64_t post_start_uptime = k_uptime_get();

        std::vector<arm::app::object_detection::DetectionResult> results;
        arm::app::object_detection::PostProcessParams postProcessParams {
            AI_INPUT_IMAGE_HEIGHT, AI_INPUT_IMAGE_WIDTH, AI_INPUT_IMAGE_WIDTH, anchor1, anchor2
        };
        postProcessParams.threshold = 0.35f;

        arm::app::DetectorPostProcess postProcess(output_tensor0, output_tensor1, results, postProcessParams);
        if (!postProcess.DoPostProcess()) {
            printk("ERROR: YOLO-Fastest post-processing failed\n");
            continue;
        }

        st_ai_detection_point_t local_detections[AI_MAX_DETECTION_NUM] = {0};
        int count = 0;
        for (size_t i = 0; i < results.size() && i < AI_MAX_DETECTION_NUM; ++i) {
            local_detections[i].m_x = (signed short)results[i].m_x0;
            local_detections[i].m_y = (signed short)results[i].m_y0;
            local_detections[i].m_w = (signed short)results[i].m_w;
            local_detections[i].m_h = (signed short)results[i].m_h;
            local_detections[i].m_val_percent = (int)(results[i].m_normalisedVal * 100.0);
            count++;
        }

        ai_update_detection_results(local_detections, count);

        int64_t total_end_uptime = k_uptime_get();
        uint32_t post_ms = (uint32_t)(total_end_uptime - post_start_uptime);
        uint32_t total_ms = (uint32_t)(total_end_uptime - start_uptime);

        g_ai_result_new = true;
        s_infer_cnt++;

        printk("[AI CPU #%lu] Found %d face(s) in %lu ms (Pre: %lu ms, Inv: %lu ms, Post: %lu ms)\n",
               (unsigned long)s_infer_cnt, count, (unsigned long)total_ms,
               (unsigned long)pre_ms, (unsigned long)inv_ms, (unsigned long)post_ms);

        for (int i = 0; i < count; i++) {
            printk("  Face %d: Box[%d, %d, %d, %d], Conf: %d%%\n",
                   i, local_detections[i].m_x, local_detections[i].m_y,
                   local_detections[i].m_w, local_detections[i].m_h,
                   local_detections[i].m_val_percent);
        }
    }
}
