/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CAMERA_LAYER_H_
#define CAMERA_LAYER_H_

#include <stdint.h>
#include <stdbool.h>
#include "bsp_api.h"
#include "camera_layer_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ov_reg {
    uint16_t reg_num;
    uint8_t  value;
} st_ov_reg_t;

/* Camera Buffer */
extern uint8_t camera_capture_image_rgb565[CAMERA_CAPTURE_IMAGE_WIDTH * CAMERA_CAPTURE_IMAGE_HEIGHT * CAMERA_IMAGE_BYTE_PER_PIXEL];
extern uint32_t camera_capture_image_rgb565_size;
extern uint8_t *p_camera_capture_buffer_stored;

/* Camera APIs */
int  camera_init(bool use_test_mode);
void camera_image_buffer_initialize(void);
void camera_capture_start(void);
void camera_capture_post_process(void);

/* Low-level sensor register I2C functions */
bool rdSensorReg16_8(uint16_t regID, uint8_t *regDat);
bool wrSensorReg16_8(int regID, int regDat);

#ifdef __cplusplus
}
#endif

#endif /* CAMERA_LAYER_H_ */
