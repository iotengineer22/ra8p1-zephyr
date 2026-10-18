/*
 * SPDX-FileCopyrightText: Copyright 2022 Arm Limited and/or its affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <cstdio>
#include "log_macros.h"
#include "bsp_api.h"
#include "common_util.h"
#include "DetectorPostProcessing.hpp"
#include "DetectionResult.hpp"
#include "ai_inference.h"

extern "C" {
#include "time_counter.h"
#include "wrapper.h"
int e_printf(const char *format, ...);
void update_detection_result(uint16_t index, signed short  x, signed short  y, signed short  w, signed short  h, int val_percent);
void ai_update_detection_results(const st_ai_detection_point_t *detections, int count);
extern volatile uint32_t g_ai_inference_time_ms;
extern float g_max_objectness_b0;
extern float g_max_objectness_b1;
}

#define IMAGE_DATA_SIZE  (AI_INPUT_IMAGE_WIDTH * AI_INPUT_IMAGE_HEIGHT * AI_INPUT_IMAGE_BYTE_PER_PIXEL)
extern int8_t *model_buffer_int8;


int CompareOutput(const uint8_t* reference, const uint8_t* actual_output, const uint64_t size_in_elements, const float epsilon) {
    int mismatches = 0;
    for (uint32_t i = 0; i < size_in_elements; i += 1) {
        float diff = (actual_output[i] - reference[i]);
        if (diff > epsilon) {
            mismatches += 1;
        }
    }
    return mismatches;
}
const float anchor1[] = {38, 77, 47, 97, 61, 126};
const float anchor2[] = {14, 26, 19, 37, 28, 55 };

// Global static buffers to prevent function local C++ guard initialization locks (avoiding Bus Fault)
// and to avoid quantization parameter overwrite between the two tensors.
// TfLiteFloatArray and TfLiteIntArray use flexible array members data[] which do not allocate storage,
// so we define custom struct wrappers with explicit data arrays to allocate actual memory.
static struct {
    int size;
    float data[1];
} g_scale_array_buf[2];

static struct {
    int size;
    int data[1];
} g_zero_point_array_buf[2];

static TfLiteAffineQuantization g_quantization[2];

void create_int8_tensor(TfLiteTensor* tensor, int8_t* input_ptr, float scale, int zero_point, int idx) {
    // Set type to int8
    tensor->type = kTfLiteInt8;

    // Assign the existing input pointer
    tensor->data.int8 = input_ptr;

    // Set correct bytes for YOLO-Fastest output layers (648 and 2592)
    tensor->bytes = (idx == 0) ? 648 : 2592;

    // Set dims to NULL as requested
    tensor->dims = NULL;

    // Set scale using the decoupled array buffer structure (cast to TfLiteFloatArray*)
    g_scale_array_buf[idx].size = 1;
    g_scale_array_buf[idx].data[0] = scale;
    g_quantization[idx].scale = (TfLiteFloatArray*)&g_scale_array_buf[idx];

    // Set zero point using the decoupled array buffer structure (cast to TfLiteIntArray*)
    g_zero_point_array_buf[idx].size = 1;
    g_zero_point_array_buf[idx].data[0] = zero_point;
    g_quantization[idx].zero_point = (TfLiteIntArray*)&g_zero_point_array_buf[idx];

    g_quantization[idx].quantized_dimension = 0;  // Per-tensor quantization

    tensor->quantization.type = kTfLiteAffineQuantization;
    tensor->quantization.params = &g_quantization[idx];

    // Set allocation type
    tensor->allocation_type = kTfLiteArenaRw;

    // Set other fields
    tensor->is_variable = false;
}

static bool PresentInferenceResult(const std::vector<arm::app::object_detection::DetectionResult>& results)
{
    st_ai_detection_point_t local_results[AI_MAX_DETECTION_NUM] = {0};
    int count = 0;

    for (size_t i = 0; i < results.size() && i < AI_MAX_DETECTION_NUM; ++i) {
        local_results[i].m_x = (signed short)results[i].m_x0;
        local_results[i].m_y = (signed short)results[i].m_y0;
        local_results[i].m_w = (signed short)results[i].m_w;
        local_results[i].m_h = (signed short)results[i].m_h;
        local_results[i].m_val_percent = (int)(results[i].m_normalisedVal * 100.0);
        count++;
    }

    ai_update_detection_results(local_results, count);
    return true;
}


/*********************************************************************************************************************
 *  main_loop_face_detection function: sets up the application context with a model object and then calls the
 *  ObjectDetectionHandler" function to perform the face detection.
 *  @param[IN]   None
 *  @retval      true	successful execution.
 *  			 false	handler call failed.
***********************************************************************************************************************/
extern "C" bool main_loop_face_detection()
{

    /* Copy the AI input image to tensor arena */
    memcpy(mera_input_ptr(), model_buffer_int8, IMAGE_DATA_SIZE);

    // Sync input image buffer to physical memory so NPU DMA can read it
    SCB_CleanDCache_by_Addr((uint32_t *)mera_input_ptr(), IMAGE_DATA_SIZE);

    volatile uint32_t old_counter =  TimeCounter_CurrentCountGet();

    /* Execute AI inference */
    mera_invoke();

    uint32_t new_counter = TimeCounter_CurrentCountGet();

    // Invalidate D-cache for output tensors so CPU reads directly from physical memory
    SCB_InvalidateDCache_by_Addr((uint32_t *)mera_output1_ptr(), 648);
    SCB_InvalidateDCache_by_Addr((uint32_t *)mera_output2_ptr(), 2592);

    uint32_t diff_cycles = new_counter - old_counter;
    // 競合するライブラリ関数をバイパスするため、直接ミリ秒を計算する
    application_processing_time.ai_inference_time_ms = (uint32_t)(((uint64_t)diff_cycles * 1000) / 480000000);
    g_ai_inference_time_ms = application_processing_time.ai_inference_time_ms;

    int8_t* output0 = (int8_t*)mera_output1_ptr();
    int8_t* output1 = (int8_t*)mera_output2_ptr();

    std::vector<arm::app::object_detection::DetectionResult> results;
    arm::app::object_detection::PostProcessParams postProcessParams {
        AI_INPUT_IMAGE_HEIGHT, AI_INPUT_IMAGE_WIDTH, AI_INPUT_IMAGE_WIDTH, anchor1, anchor2
    };
    results.clear();

    TfLiteTensor outputTensor0;
    TfLiteTensor outputTensor1;

    // Need to read quatization params from tflite model (netron)
    create_int8_tensor(&outputTensor0, output0, 0.13408391177654266, 47, 0);
    create_int8_tensor(&outputTensor1, output1, 0.18535925447940826, 10, 1);


    postProcessParams.threshold = 0.35f;

    arm::app::DetectorPostProcess postProcess = arm::app::DetectorPostProcess(&outputTensor0, &outputTensor1,
            results, postProcessParams);

    if (!postProcess.DoPostProcess()) {
        e_printf("[PostProcess] DoPostProcess failed!\n");
        return false;
    }


    static uint32_t s_log_cnt = 0;
    static uint32_t s_detect_log_cnt = 0;
    if (results.size() > 0) {
        if ((s_detect_log_cnt++ % 15) == 0) {
            e_printf("[AI] Face detected: %d (Score: %d%%, Inf: %lu ms, MaxObj: B0=%.1f%%, B1=%.1f%%)\n",
                     (int)results.size(), (int)(results[0].m_normalisedVal * 100.0),
                     (unsigned long)g_ai_inference_time_ms,
                     (double)(g_max_objectness_b0 * 100.0f), (double)(g_max_objectness_b1 * 100.0f));
        }
    } else {
        s_detect_log_cnt = 0;
        if ((s_log_cnt++ % 15) == 0) {
            int8_t *in_p = (int8_t *)mera_input_ptr();
            int32_t sum = 0;
            int8_t min_v = 127;
            int8_t max_v = -128;
            for (int p = 0; p < IMAGE_DATA_SIZE; p += 32) {
                int8_t val = in_p[p];
                sum += val;
                if (val < min_v) min_v = val;
                if (val > max_v) max_v = val;
            }
            int avg_v = sum / (IMAGE_DATA_SIZE / 32);
            int8_t center_pix = in_p[96 * 192 + 96];

            e_printf("[AI Heartbeat] Inf: %lu ms | Img[min:%d max:%d avg:%d center:%d] | MaxObj[B0:%.1f%% B1:%.1f%%]\n",
                     (unsigned long)g_ai_inference_time_ms,
                     min_v, max_v, avg_v, center_pix,
                     (double)(g_max_objectness_b0 * 100.0f), (double)(g_max_objectness_b1 * 100.0f));
        }
    }

    if (!PresentInferenceResult(results)) {
        return false;
    }

    return true;
}
