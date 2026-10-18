#ifndef AI_INFERENCE_H_
#define AI_INFERENCE_H_

#include <stdint.h>
#include <stdbool.h>
#include "bsp_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "common_util.h"

// AI input image size: 192x192 1ch grayscale
#define AI_INPUT_IMAGE_WIDTH          (192)
#define AI_INPUT_IMAGE_HEIGHT         (192)
#define AI_INPUT_IMAGE_BYTE_PER_PIXEL (1)

// Maximum number of detected faces
#define AI_MAX_DETECTION_NUM          (20)

// Processing time measurement structure
typedef struct st_ai_processing_time_t {
    uint32_t ai_inference_pre_processing_time_ms;
    uint32_t ai_inference_time_ms;
} st_ai_processing_time_t;

// Global shared variables
extern st_ai_detection_point_t g_ai_detection[AI_MAX_DETECTION_NUM];
extern st_ai_processing_time_t g_ai_processing_time;
extern volatile uint32_t g_ai_inference_time_ms;
extern volatile bool g_ai_result_new;
extern volatile bool g_ai_task_busy;

// Thread-safe persistent detection result API (HUD snapshot)
void ai_update_detection_results(const st_ai_detection_point_t *detections, int count);
int ai_get_detection_snapshot(st_ai_detection_point_t *out_detections, int max_count, int *out_max_score);

// Initialize CPU AI subsystem
int ai_inference_init(void);

// Zephyr thread entry
void ai_inference_thread_entry(void *p1, void *p2, void *p3);

// Kick inference from display loop
void ai_inference_kick(void);

#ifdef __cplusplus
}
#endif

#endif /* AI_INFERENCE_H_ */
