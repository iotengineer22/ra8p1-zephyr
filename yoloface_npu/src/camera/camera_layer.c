/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "bsp_api.h"
#include "r_gpt.h"
#include "r_timer_api.h"

#include "camera_layer.h"
#include "camera_layer_config.h"
#include "arducam.h"
#include "mipi_vin_config.h"

#define BSP_I2C_SLAVE_ADDR_CAMERA   (0x3C)

#define REG_PRODUCT_ID_H            (0x300a)
#define REG_PRODUCT_ID_L            (0x300b)

#define CORRECT_PRODUCT_ID_H        (0x56)
#define CORRECT_PRODUCT_ID_L_REV1A  (0x40)
#define CORRECT_PRODUCT_ID_L_REV2A  (0x41)
#define CORRECT_PRODUCT_ID_L_REV2C  (0x4C)

#define CONFIG_TABLE_END_DETECT     (0xFFFF)
#define REQUEST_SOFTWARE_WAIT       (0xAAAA)

#define X_OV5640_PHYSICAL_PIXEL_SIZE (2624)
#define Y_OV5640_PHYSICAL_PIXEL_SIZE (1964)
#define X_ISP_INPUT_SIZE             (2560)
#define Y_ISP_INPUT_SIZE             (1920)

#define X_ADDR_START ((X_OV5640_PHYSICAL_PIXEL_SIZE - X_ISP_INPUT_SIZE) / 2)
#define Y_ADDR_START ((Y_OV5640_PHYSICAL_PIXEL_SIZE - Y_ISP_INPUT_SIZE) / 2)
#define X_ADDR_END   (X_ADDR_START + X_ISP_INPUT_SIZE)
#define Y_ADDR_END   (Y_ADDR_START + Y_ISP_INPUT_SIZE)
#define X_OFFSET     0x0010
#define Y_OFFSET     0x0010

/* Static camera capture buffer */
uint8_t camera_capture_image_rgb565[CAMERA_CAPTURE_IMAGE_WIDTH * CAMERA_CAPTURE_IMAGE_HEIGHT * CAMERA_IMAGE_BYTE_PER_PIXEL] __attribute__((aligned(8)));
uint32_t camera_capture_image_rgb565_size = sizeof(camera_capture_image_rgb565);
uint8_t *p_camera_capture_buffer_stored = NULL;

static const struct device *g_camera_i2c_dev = NULL;

/* Low-level sensor register read/write using Zephyr I2C */
bool wrSensorReg16_8(int regID, int regDat)
{
    if (!g_camera_i2c_dev) {
        return false;
    }
    uint8_t buf[3] = {
        (uint8_t)(regID >> 8),
        (uint8_t)(regID & 0xFF),
        (uint8_t)(regDat & 0xFF)
    };
    int ret = i2c_write(g_camera_i2c_dev, buf, 3, BSP_I2C_SLAVE_ADDR_CAMERA);
    if (ret != 0) {
        printk("I2C write err %d (reg 0x%04X = 0x%02X)\n", ret, (unsigned int)regID, (unsigned int)regDat);
        return false;
    }
    return true;
}

bool rdSensorReg16_8(uint16_t regID, uint8_t *regDat)
{
    if (!g_camera_i2c_dev || !regDat) {
        return false;
    }
    uint8_t addr[2] = {
        (uint8_t)(regID >> 8),
        (uint8_t)(regID & 0xFF)
    };
    int ret = i2c_write_read(g_camera_i2c_dev, BSP_I2C_SLAVE_ADDR_CAMERA, addr, 2, regDat, 1);
    if (ret != 0) {
        printk("I2C write_read err %d (reg 0x%04X)\n", ret, regID);
        return false;
    }
    return true;
}

/* GPT12 timer for 24MHz camera clock (XCLK) */
static gpt_instance_ctrl_t g_cam_clk_ctrl;
static const gpt_extended_cfg_t g_cam_clk_extend = {
    .gtioca = { .output_enabled = true, .stop_level = GPT_PIN_LEVEL_LOW },
    .gtiocb = { .output_enabled = false, .stop_level = GPT_PIN_LEVEL_LOW },
    .gtioca_polarity = GPT_GTIOC_POLARITY_NORMAL,
    .gtiocb_polarity = GPT_GTIOC_POLARITY_NORMAL,
};

static const timer_cfg_t g_cam_clk_cfg = {
    .mode              = TIMER_MODE_PERIODIC,
    .period_counts     = 10, /* 240MHz / 10 = 24MHz */
    .duty_cycle_counts = 5,  /* 50% duty */
    .source_div        = (timer_source_div_t)0,
    .channel           = 12,
    .p_callback        = NULL,
    .p_context         = NULL,
    .p_extend          = &g_cam_clk_extend,
    .cycle_end_ipl     = (BSP_IRQ_DISABLED),
    .cycle_end_irq     = FSP_INVALID_VECTOR,
};

static const timer_instance_t g_cam_clk = {
    .p_ctrl = &g_cam_clk_ctrl,
    .p_cfg  = &g_cam_clk_cfg,
    .p_api  = &g_timer_on_gpt,
};

/* Camera initialization table for OV5640 */
static const st_ov_reg_t cam_config_table_normal_mode[] = {
    { 0x3103, 0x11 },
    { 0x3008, 0x82 },
    { REQUEST_SOFTWARE_WAIT, 0xFF },
    { 0x3008, 0x42 },
    { 0x3103, 0x03 },
    { REQUEST_SOFTWARE_WAIT, 5 },

    { 0x3017, 0x00 }, { 0x3018, 0x00 },
    { 0x3034, 0x10 | 8 }, // MIPI 8-bit mode

    { 0x3037, 0x13 }, // PLL
    { 0x302c, 0xc2 },
    { 0x3108, 0x01 },

    { 0x3630, 0x2e }, { 0x3632, 0xe2 }, { 0x3633, 0x23 }, { 0x3621, 0xe0 },
    { 0x3704, 0xa0 }, { 0x3703, 0x5a }, { 0x3715, 0x78 }, { 0x3717, 0x01 },
    { 0x370b, 0x60 }, { 0x3705, 0x1a }, { 0x3905, 0x02 }, { 0x3906, 0x10 },
    { 0x3901, 0x0a }, { 0x3731, 0x12 }, { 0x3600, 0x08 }, { 0x3601, 0x33 },
    { 0x302d, 0x60 }, { 0x3620, 0x52 }, { 0x371b, 0x20 }, { 0x471c, 0x50 },
    { 0x3a18, 0x00 }, { 0x3a19, 0xf8 }, { 0x3635, 0x1c }, { 0x3634, 0x40 },
    { 0x3622, 0x01 }, { 0x3c04, 0x28 }, { 0x3c05, 0x98 }, { 0x3c06, 0x00 },
    { 0x3c07, 0x08 }, { 0x3c08, 0x00 }, { 0x3c09, 0x1c }, { 0x3c0a, 0x9c },
    { 0x3c0b, 0x40 },

    /* Mirror and Flip */
    { 0x3820, 0x41 },
    { 0x3821, 0x01 },

    /* Image windowing */
    { 0x3800, ((X_ADDR_START >> 8) & 0xFF) },
    { 0x3801, (X_ADDR_START & 0xFF) },
    { 0x3802, ((Y_ADDR_START >> 8) & 0xFF) },
    { 0x3803, (Y_ADDR_START & 0xFF) },
    { 0x3804, ((X_ADDR_END >> 8) & 0xFF) },
    { 0x3805, (X_ADDR_END & 0xFF) },
    { 0x3806, ((Y_ADDR_END >> 8) & 0xFF) },
    { 0x3807, (Y_ADDR_END & 0xFF) },

    /* Pre-scaling offset */
    { 0x3810, ((X_OFFSET >> 8) & 0xFF) },
    { 0x3811, (X_OFFSET & 0xFF) },
    { 0x3812, ((Y_OFFSET >> 8) & 0xFF) },
    { 0x3813, (Y_OFFSET & 0xFF) },

    /* Output size (320x240) */
    { 0x3808, ((CAMERA_ACTIVE_IMAGE_WIDTH >> 8) & 0xFF) },
    { 0x3809, (CAMERA_ACTIVE_IMAGE_WIDTH & 0xFF) },
    { 0x380a, ((CAMERA_ACTIVE_IMAGE_HEIGHT >> 8) & 0xFF) },
    { 0x380b, (CAMERA_ACTIVE_IMAGE_HEIGHT & 0xFF) },

    { 0x3814, 0x31 }, { 0x3815, 0x31 }, { 0x3708, 0x64 }, { 0x4001, 0x02 },
    { 0x4005, 0x1a }, { 0x3000, 0x00 }, { 0x3002, 0x1c }, { 0x3004, 0xff },
    { 0x3006, 0xc3 },

    { 0x300e, 0x44 }, // MIPI 2-lane, enable
    { 0x302e, 0x08 },
    { 0x4300, 0x32 }, // YUV 422 (8bit), YUYV
    { 0x501f, 0x00 },
    { 0x4407, 0x04 },
    { 0x5000, 0xa7 },

    { 0x3406, 0x01 }, { 0x3400, 0x06 }, { 0x3401, 0x80 }, { 0x3402, 0x04 },
    { 0x3403, 0x00 }, { 0x3404, 0x06 }, { 0x3405, 0x00 },
    { 0x5180, 0xff }, { 0x5181, 0xf2 }, { 0x5182, 0x00 }, { 0x5183, 0x14 },
    { 0x5184, 0x25 }, { 0x5185, 0x24 }, { 0x5186, 0x16 }, { 0x5187, 0x16 },
    { 0x5188, 0x16 }, { 0x5189, 0x62 }, { 0x518a, 0x62 }, { 0x518b, 0xf0 },
    { 0x518c, 0xb2 }, { 0x518d, 0x50 }, { 0x518e, 0x30 }, { 0x518f, 0x30 },
    { 0x5190, 0x50 }, { 0x5191, 0xf8 }, { 0x5192, 0x04 }, { 0x5193, 0x70 },
    { 0x5194, 0xf0 }, { 0x5195, 0xf0 }, { 0x5196, 0x03 }, { 0x5197, 0x01 },
    { 0x5198, 0x04 }, { 0x5199, 0x12 }, { 0x519a, 0x04 }, { 0x519b, 0x00 },
    { 0x519c, 0x06 }, { 0x519d, 0x82 }, { 0x519e, 0x38 },
    { 0x5381, 0x1e }, { 0x5382, 0x5b }, { 0x5383, 0x14 }, { 0x5384, 0x06 },
    { 0x5385, 0x82 }, { 0x5386, 0x88 }, { 0x5387, 0x7c }, { 0x5388, 0x60 },
    { 0x5389, 0x1c }, { 0x538a, 0x01 }, { 0x538b, 0x98 },
    { 0x5300, 0x08 }, { 0x5301, 0x30 }, { 0x5302, 0x3f }, { 0x5303, 0x10 },
    { 0x5304, 0x08 }, { 0x5305, 0x30 }, { 0x5306, 0x18 }, { 0x5307, 0x28 },
    { 0x5309, 0x08 }, { 0x530a, 0x30 }, { 0x530b, 0x04 }, { 0x530c, 0x06 },
    { 0x5480, 0x01 }, { 0x5481, 0x06 }, { 0x5482, 0x12 }, { 0x5483, 0x24 },
    { 0x5484, 0x4a }, { 0x5485, 0x58 }, { 0x5486, 0x65 }, { 0x5487, 0x72 },
    { 0x5488, 0x7d }, { 0x5489, 0x88 }, { 0x548a, 0x92 }, { 0x548b, 0xa3 },
    { 0x548c, 0xb2 }, { 0x548d, 0xc8 }, { 0x548e, 0xdd }, { 0x548f, 0xf0 },
    { 0x5490, 0x15 },

    { 0x5580, 0x06 },
    { 0x5583, 0x40 },
    { 0x5584, 0x20 },
    { 0x5589, 0x10 },
    { 0x558a, 0x00 },
    { 0x558b, 0xf8 },
    { 0x5585, 0x00 },
    { 0x5586, 0x20 },
    { 0x5587, 0x00 },
    { 0x5588, 0x01 },
    { 0x501d, 0x40 },

    { 0x5000, 0xa7 }, { 0x5800, 0x20 }, { 0x5801, 0x19 }, { 0x5802, 0x17 },
    { 0x5803, 0x16 }, { 0x5804, 0x18 }, { 0x5805, 0x21 }, { 0x5806, 0x0F },
    { 0x5807, 0x0A }, { 0x5808, 0x07 }, { 0x5809, 0x07 }, { 0x580a, 0x0A },
    { 0x580b, 0x0C }, { 0x580c, 0x0A }, { 0x580d, 0x03 }, { 0x580e, 0x01 },
    { 0x580f, 0x01 }, { 0x5810, 0x03 }, { 0x5811, 0x09 }, { 0x5812, 0x0A },
    { 0x5813, 0x03 }, { 0x5814, 0x01 }, { 0x5815, 0x01 }, { 0x5816, 0x03 },
    { 0x5817, 0x08 }, { 0x5818, 0x10 }, { 0x5819, 0x0A }, { 0x581a, 0x06 },
    { 0x581b, 0x06 }, { 0x581c, 0x08 }, { 0x581d, 0x0E }, { 0x581e, 0x22 },
    { 0x581f, 0x18 }, { 0x5820, 0x13 }, { 0x5821, 0x12 }, { 0x5822, 0x16 },
    { 0x5823, 0x1E }, { 0x5824, 0x64 }, { 0x5825, 0x2A }, { 0x5826, 0x2C },
    { 0x5827, 0x2A }, { 0x5828, 0x46 }, { 0x5829, 0x2A }, { 0x582a, 0x26 },
    { 0x582b, 0x24 }, { 0x582c, 0x26 }, { 0x582d, 0x2A }, { 0x582e, 0x28 },
    { 0x582f, 0x42 }, { 0x5830, 0x40 }, { 0x5831, 0x42 }, { 0x5832, 0x08 },
    { 0x5833, 0x28 }, { 0x5834, 0x26 }, { 0x5835, 0x24 }, { 0x5836, 0x26 },
    { 0x5837, 0x2A }, { 0x5838, 0x44 }, { 0x5839, 0x4A }, { 0x583a, 0x2C },
    { 0x583b, 0x2a }, { 0x583c, 0x46 }, { 0x583d, 0xCE },
    { 0x5688, 0x22 }, { 0x5689, 0x22 }, { 0x568a, 0x42 }, { 0x568b, 0x24 },
    { 0x568c, 0x42 }, { 0x568d, 0x24 }, { 0x568e, 0x22 }, { 0x568f, 0x22 },
    { 0x5025, 0x00 }, { 0x3a0f, 0x30 }, { 0x3a10, 0x28 }, { 0x3a1b, 0x30 },
    { 0x3a1e, 0x28 }, { 0x3a11, 0x61 }, { 0x3a1f, 0x10 },

    { 0x4800, 0x24 },
    { 0x3007, 0XFB },

    /* Timing control */
    { 0x380c, 0x0b }, { 0x380d, 0x1c }, { 0x380e, 0x07 }, { 0x380f, 0xb0 },

    { 0x3c01, 0xb4 }, { 0x3c00, 0x04 }, { 0x3a08, 0x00 }, { 0x3a09, 0x93 },
    { 0x3a0e, 0x06 }, { 0x3a0a, 0x00 }, { 0x3a0b, 0x7b }, { 0x3a0d, 0x08 },
    { 0x3a00, 0x3c }, { 0x3a02, 0x05 }, { 0x3a03, 0xc4 }, { 0x3a14, 0x05 },
    { 0x3a15, 0xc4 }, { 0x3618, 0x00 }, { 0x3612, 0x29 }, { 0x3708, 0x64 },
    { 0x3709, 0x52 }, { 0x370c, 0x03 },
    { 0x4004, 0x02 }, { 0x4713, 0x03 }, { 0x460b, 0x35 }, { 0x460c, 0x22 },
    { 0x4837, 0x0a },
    { 0x3824, 0x01 },
    { 0x5001, 0xa3 },

    { 0x3406, 0x00 }, { 0x3503, 0x00 },

    /* Wake up */
    { 0x3008, 0x02 },
    { REQUEST_SOFTWARE_WAIT, 5 },

    /* Disable test patterns */
    { 0x503D, 0x00 },
    { 0x4741, 0x00 },

    /* End marker */
    { CONFIG_TABLE_END_DETECT, 0x00FF }
};

static const st_ov_reg_t cam_config_table_test_mode[] = {
    { 0x503D, 0x80 }, // Color bar test pattern
    { CONFIG_TABLE_END_DETECT, 0x00FF }
};

static int bsp_camera_write_array(const st_ov_reg_t *array)
{
    uint8_t read_value = 0;
    while (array->reg_num != CONFIG_TABLE_END_DETECT) {
        if (array->reg_num == REQUEST_SOFTWARE_WAIT) {
            k_msleep(array->value);
        } else {
            if (!wrSensorReg16_8(array->reg_num, array->value)) {
                printk("I2C Write Failed at reg 0x%04X = 0x%02X\n", array->reg_num, array->value);
                return -1;
            }
#if CAMERA_CONFIG_WRITE_VERIFY
            rdSensorReg16_8(array->reg_num, &read_value);
            if (read_value != array->value) {
                // Some registers are write-only or auto-cleared, don't fail hard
            }
#endif
        }
        array++;
    }
    return 0;
}

static int ov5640_configure_clocks(void)
{
    printk("Configuring OV5640 PLL and internal clocks...\n");
    /* 0x3035: Sys Clock Div: 1, MIPI Clock Div: 2 -> 0x12 */
    wrSensorReg16_8(0x3035, 0x12);

    /* 0x3036: PLL Multiplier: 140 -> 0x8C */
    wrSensorReg16_8(0x3036, 140);

    /* 0x3037: PLL 2x-root-div: Enabled (1<<4), PLL Pre-div: 3 (3<<0) -> 0x13 */
    wrSensorReg16_8(0x3037, 0x13);

    /* 0x3108: system divider -> 0x01 */
    wrSensorReg16_8(0x3108, 0x01);

    return 0;
}

static void ov5640_mipi_virtual_channel_set(uint32_t vchannel)
{
    uint8_t tmp = 0;
    rdSensorReg16_8(0x4814, &tmp);
    tmp &= (uint8_t)~(3 << 6);
    tmp |= (uint8_t)(vchannel << 6);
    wrSensorReg16_8(0x4814, tmp);
}

static void ov5640_stream_on(void)
{
    wrSensorReg16_8(0x4202, 0x00);
}

static void ov5640_stream_off(void)
{
    wrSensorReg16_8(0x4202, 0x0F);
}

void camera_image_buffer_initialize(void)
{
    memset(camera_capture_image_rgb565, 0, sizeof(camera_capture_image_rgb565));
    SCB_CleanDCache_by_Addr((uint8_t *)camera_capture_image_rgb565, (int32_t)sizeof(camera_capture_image_rgb565));
}

void camera_capture_start(void)
{
    R_VIN_CaptureStart(&g_cam_vin_ctrl, NULL);
}

void camera_capture_post_process(void)
{
    if (p_camera_capture_buffer_stored) {
        SCB_InvalidateDCache_by_Addr(p_camera_capture_buffer_stored, (int32_t)camera_capture_image_rgb565_size);
        memcpy(&camera_capture_image_rgb565[0], (void *)p_camera_capture_buffer_stored, camera_capture_image_rgb565_size);
    }
}

/* Callback from VIN interrupt on each captured frame */
void cam_vin_callback(capture_callback_args_t *p_args)
{
    vin_event_t event = (vin_event_t)p_args->event;
    vin_interrupt_status_t interrupt_status;
    interrupt_status.mask = p_args->interrupt_status;
    static int first_frame_logged = 0;

    if (event == VIN_EVENT_ERROR) {
        static int error_count = 0;
        if (error_count < 10) {
            printk("[VIN CB] Error event: %d, Status: 0x%08X\n", (int)event, (unsigned int)interrupt_status.mask);
            error_count++;
        }
    }

    if (event == VIN_EVENT_NOTIFY) {
        if (interrupt_status.bits.frame_complete) {
            if (!first_frame_logged) {
                printk("[VIN CB] First frame captured successfully!\n");
                first_frame_logged = 1;
            }
            p_camera_capture_buffer_stored = p_args->p_buffer;
        }
    }
}

void cam_mipi_csi_callback(mipi_csi_callback_args_t *p_args)
{
    printk("[MIPI CB] Event: %d\n", (int)p_args->event);
}

int camera_init(bool use_test_mode)
{
    printk("Starting Camera Subsystem Initialization...\n");

    /* 1. Initialize Zephyr I2C master device */
    g_camera_i2c_dev = DEVICE_DT_GET(DT_NODELABEL(iic1));
    if (!device_is_ready(g_camera_i2c_dev)) {
        printk("ERROR: I2C device (iic1) is not ready!\n");
        return -1;
    }

    /* 2. Configure GPIO pins for Camera XCLK, Reset, and MIPI Multiplexer */
    R_BSP_PinAccessEnable();

    /* P501: CAM_XCLK (GTIOC12A) -> 24MHz clock output from GPT12 */
    /* IOPORT_CFG_DRIVE_HS_HIGH (0x800) | IOPORT_CFG_PERIPHERAL_PIN (0x10000) | IOPORT_PERIPHERAL_GPT1 (0x03000000) */
    R_BSP_PinCfg(BSP_IO_PORT_05_PIN_01, 0x00000800 | 0x00010000 | (0x03UL << 24));

    /* P108: MIPI_IF_EN -> Output LOW to enable board multiplexer to camera interface */
    /* IOPORT_CFG_PORT_DIRECTION_OUTPUT (0x04) | IOPORT_CFG_PORT_OUTPUT_LOW (0x00) */
    R_BSP_PinCfg(BSP_IO_PORT_01_PIN_08, 0x00000004 | 0x00000000);
    R_BSP_PinWrite(BSP_IO_PORT_01_PIN_08, BSP_IO_LEVEL_LOW);

    /* P709: CAM_RST -> Output LOW initially to hold in reset */
    /* IOPORT_CFG_PORT_DIRECTION_OUTPUT (0x04) | IOPORT_CFG_PORT_OUTPUT_LOW (0x00) */
    R_BSP_PinCfg(BSP_IO_PORT_07_PIN_09, 0x00000004 | 0x00000000);
    R_BSP_PinWrite(BSP_IO_PORT_07_PIN_09, BSP_IO_LEVEL_LOW);

    R_BSP_PinAccessDisable();

    /* 3. Start 24MHz Camera XCLK via GPT12 */
    printk("Starting GPT12 Clock for Camera XCLK on P501...\n");
    fsp_err_t err = R_GPT_Open(&g_cam_clk_ctrl, &g_cam_clk_cfg);
    if (FSP_SUCCESS == err) {
        R_GPT_Start(&g_cam_clk_ctrl);
    } else {
        printk("ERROR: Failed to open GPT12 clock (0x%X)\n", err);
        return -1;
    }

    /* Wait with 24MHz clock running while sensor is held in reset */
    k_msleep(100);

    /* Release camera reset (P709 -> HIGH) */
    printk("Releasing Camera Reset (P709 -> HIGH)...\n");
    R_BSP_PinAccessEnable();
    R_BSP_PinWrite(BSP_IO_PORT_07_PIN_09, BSP_IO_LEVEL_HIGH);
    R_BSP_PinAccessDisable();

    /* Wait for camera sensor internal oscillator and SCCB interface to stabilize */
    k_msleep(50);

    /* 4. Verify OV5640 communication via I2C with retry */
    uint8_t pid_h = 0, pid_l = 0;
    bool i2c_ok = false;
    for (int retry = 0; retry < 5; retry++) {
        if (rdSensorReg16_8(REG_PRODUCT_ID_H, &pid_h) && rdSensorReg16_8(REG_PRODUCT_ID_L, &pid_l)) {
            i2c_ok = true;
            break;
        }
        k_msleep(20);
    }

    if (!i2c_ok) {
        printk("ERROR: Camera I2C communication failed! Check wiring.\n");
        return -1;
    }

    printk("[Camera Init] Product ID High: 0x%02X, Low: 0x%02X\n", pid_h, pid_l);
    if (pid_h != CORRECT_PRODUCT_ID_H ||
        (pid_l != CORRECT_PRODUCT_ID_L_REV1A && pid_l != CORRECT_PRODUCT_ID_L_REV2A && pid_l != CORRECT_PRODUCT_ID_L_REV2C)) {
        printk("ERROR: Camera Product ID mismatch! (Expected 0x5640/41/4C)\n");
        return -1;
    }
    printk("SUCCESS: Camera connection verified! (OV5640 detected)\n");

    /* 5. Load OV5640 register configuration */
    printk("Loading OV5640 Configuration Table...\n");
    if (bsp_camera_write_array(cam_config_table_normal_mode) != 0) {
        printk("ERROR: Failed to write camera register configuration!\n");
        return -1;
    }

    ov5640_configure_clocks();
    ov5640_mipi_virtual_channel_set(0);

    if (use_test_mode) {
        printk("Enabling Camera ISP Test Pattern Mode (Color Bars)...\n");
        bsp_camera_write_array(cam_config_table_test_mode);
    }

    ov5640_stream_off();
    k_msleep(5);

    /* 6. Connect MIPI-CSI and VIN interrupts to Zephyr NVIC */
    printk("Connecting MIPI & VIN Interrupts to NVIC...\n");
    mipi_vin_irq_init();

    /* 7. Open Video Input (VIN) Controller (hierarchically opens MIPI-CSI and D-PHY) */
    printk("Opening Video Input (VIN) Controller...\n");
    err = R_VIN_Open(&g_cam_vin_ctrl, &g_cam_vin_cfg);
    if (FSP_SUCCESS != err) {
        printk("ERROR: Failed to open VIN (0x%X)\n", err);
        return -1;
    }

    k_msleep(5);

    /* 8. Enable camera stream output */
    ov5640_stream_on();
    printk("SUCCESS: Camera stream enabled.\n");

    return 0;
}
