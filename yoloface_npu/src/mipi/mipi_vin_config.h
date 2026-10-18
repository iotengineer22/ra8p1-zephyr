/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MIPI_VIN_CONFIG_H_
#define MIPI_VIN_CONFIG_H_

#include <stdint.h>
#include <stdbool.h>
#include "bsp_api.h"
#include "r_mipi_phy.h"
#include "r_mipi_csi.h"
#include "r_mipi_csi_api.h"
#include "r_vin.h"
#include "r_capture_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Interrupt vector numbers for MIPI and VIN on Cortex-M85 NVIC */
#define VIN_STATUS_IRQ_NUM    (73)
#define VIN_ERROR_IRQ_NUM     (74)
#define MIPICSI_RX_IRQ_NUM    (75)
#define MIPICSI_DL_IRQ_NUM    (76)
#define MIPICSI_VC_IRQ_NUM    (77)
#define MIPICSI_PM_IRQ_NUM    (78)
#define MIPICSI_GST_IRQ_NUM   (79)

/* Image geometry definitions matching 320x240 capture */
#define VIN_CFG_IMAGE_WIDTH    (320)
#define VIN_CFG_IMAGE_HEIGHT   (240)
#define VIN_CFG_IMAGE_STRIDE   (320)
#define VIN_CFG_BYTES_PER_LINE (640)
#define VIN_BYTES_PER_FRAME    (VIN_CFG_BYTES_PER_LINE * VIN_CFG_IMAGE_HEIGHT) // 153,600 bytes

/* Capture Frame Buffers (128-byte aligned as required by VIN DMA) */
extern uint8_t vin_image_buffer_1[VIN_BYTES_PER_FRAME];
extern uint8_t vin_image_buffer_2[VIN_BYTES_PER_FRAME];
extern uint8_t vin_image_buffer_3[VIN_BYTES_PER_FRAME];

/* MIPI PHY Instance */
extern const mipi_phy_instance_t g_cam_display_mipi_phy;
extern mipi_phy_ctrl_t           g_cam_display_mipi_phy_ctrl;
extern const mipi_phy_cfg_t      g_cam_display_mipi_phy_cfg;

/* MIPI CSI Instance */
extern const mipi_csi_instance_t g_cam_mipi_csi;
extern mipi_csi_instance_ctrl_t  g_cam_mipi_csi_ctrl;
extern const mipi_csi_cfg_t      g_cam_mipi_csi_cfg;

/* VIN Instance */
extern const capture_instance_t  g_cam_vin;
extern vin_instance_ctrl_t       g_cam_vin_ctrl;
extern const capture_cfg_t       g_cam_vin_cfg;

/* Callback declarations */
void cam_vin_callback(capture_callback_args_t *p_args);
void cam_mipi_csi_callback(mipi_csi_callback_args_t *p_args);

/* Interrupt initialization */
void mipi_vin_irq_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MIPI_VIN_CONFIG_H_ */
