/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include "mipi_vin_config.h"
#include "bsp_elc.h"

/* MIPI PHY Timing Parameters */
#define MIPI_PHY_CLKSTPT   ((uint16_t)1982)
#define MIPI_PHY_CLKBFHT   ((uint8_t)16 + 1)
#define MIPI_PHY_CLKKPT    ((uint8_t)34 + 4)
#define MIPI_PHY_GOLPBKT   ((uint16_t)45)

#define MIPI_PHY_TINIT     (75001)
#define MIPI_PHY_TCLKPREP  (10)
#define MIPI_PHY_TCLKSETT  (18)
#define MIPI_PHY_TCLKMISS  (2)
#define MIPI_PHY_THSPREP   (9)
#define MIPI_PHY_THSETT    (15)
#define MIPI_PHY_TCLKTRAIL (12)
#define MIPI_PHY_TCLKPOST  (45)
#define MIPI_PHY_TCLKPRE   (15)
#define MIPI_PHY_TCLKZERO  (68)
#define MIPI_PHY_THSEXIT   (1)
#define MIPI_PHY_THSTRAIL  (11)
#define MIPI_PHY_THSZERO   (17)
#define MIPI_PHY_TLPEXIT   (12)

const mipi_phy_timing_t g_cam_display_mipi_phy_timing = {
    .t_init                  = 0x3FFFF & (uint32_t)MIPI_PHY_TINIT,
    .dphytim2_b.t_clk_prep   = (uint32_t)MIPI_PHY_TCLKPREP,
    .dphytim2_b.t_clk_settle = (uint32_t)MIPI_PHY_TCLKSETT,
    .dphytim2_b.t_clk_miss   = (uint32_t)MIPI_PHY_TCLKMISS,
    .dphytim3_b.t_hs_prep    = (uint32_t)MIPI_PHY_THSPREP,
    .dphytim3_b.t_hs_sett    = (uint32_t)MIPI_PHY_THSETT,
    .dphytim4_b.t_clk_trail  = (uint32_t)MIPI_PHY_TCLKTRAIL,
    .dphytim4_b.t_clk_post   = (uint32_t)MIPI_PHY_TCLKPOST,
    .dphytim4_b.t_clk_pre    = (uint32_t)MIPI_PHY_TCLKPRE,
    .dphytim4_b.t_clk_zero   = (uint32_t)MIPI_PHY_TCLKZERO,
    .dphytim5_b.t_hs_exit    = (uint32_t)MIPI_PHY_THSEXIT,
    .dphytim5_b.t_hs_trail   = (uint32_t)MIPI_PHY_THSTRAIL,
    .dphytim5_b.t_hs_zero    = (uint32_t)MIPI_PHY_THSZERO,
    .t_lp_exit               = (uint32_t)MIPI_PHY_TLPEXIT,
};

mipi_phy_ctrl_t g_cam_display_mipi_phy_ctrl;
const mipi_phy_cfg_t g_cam_display_mipi_phy_cfg = {
    .pll_settings = {
        .div      = 3 - 1,
        .pll_div  = 0,
        .mul_int  = 125 - 1,
        .mul_frac = 0,
    },
    .lp_divisor = 5 - 1,
    .p_timing   = &g_cam_display_mipi_phy_timing,
    .dsi_mode   = 0,
};

const mipi_phy_instance_t g_cam_display_mipi_phy = {
    .p_ctrl = &g_cam_display_mipi_phy_ctrl,
    .p_cfg  = &g_cam_display_mipi_phy_cfg,
    .p_api  = &g_mipi_phy,
};

/* MIPI CSI-2 Configuration */
mipi_csi_instance_ctrl_t g_cam_mipi_csi_ctrl;

const mipi_csi_cfg_t g_cam_mipi_csi_cfg = {
    .p_mipi_phy_instance = &g_cam_display_mipi_phy,

    .ctrl_data.control_0_bits.lane_count                = 2,
    .ctrl_data.control_0_bits.zero_length_packet_output = false,
    .ctrl_data.control_0_bits.err_frame_notify          = 1,
    .ctrl_data.control_0_bits.reserved_packet_reception = 1,
    .ctrl_data.control_0_bits.generic_rule_mode         = 1,
    .ctrl_data.control_0_bits.ecc_check_24_bits         = 1,
    .ctrl_data.control_0_bits.descramble_enable         = 0,

    .ctrl_data.control_2_bits.frrclk = 10,
    .ctrl_data.control_2_bits.frrskw = 10,

    .option_data.data_type_enable = (mipi_csi_rx_data_enable_t)(MIPI_CSI_RX_DATA_ENABLE_YUV422_8_BIT | 0x0),

    .interrupt_cfg.receive_cfg.ipl = 2,
    .interrupt_cfg.receive_cfg.irq = (IRQn_Type)MIPICSI_RX_IRQ_NUM,

    .interrupt_cfg.data_lane_cfg.ipl = 2,
    .interrupt_cfg.data_lane_cfg.irq = (IRQn_Type)MIPICSI_DL_IRQ_NUM,

    .interrupt_cfg.virtual_channel_cfg.ipl = 2,
    .interrupt_cfg.virtual_channel_cfg.irq = (IRQn_Type)MIPICSI_VC_IRQ_NUM,

    .interrupt_cfg.power_management_cfg.ipl = 2,
    .interrupt_cfg.power_management_cfg.irq = (IRQn_Type)MIPICSI_PM_IRQ_NUM,

    .interrupt_cfg.short_packet_cfg.ipl = 2,
    .interrupt_cfg.short_packet_cfg.irq = (IRQn_Type)MIPICSI_GST_IRQ_NUM,

    .interrupt_cfg.receive_enable_mask = 0x0,
    .interrupt_cfg.data_lane_enable_mask[0] = (R_MIPI_CSI_DLIE0_RULE_Msk | R_MIPI_CSI_DLIE0_EULE_Msk |
                                               R_MIPI_CSI_DLIE0_EESE_Msk | R_MIPI_CSI_DLIE0_ECTE_Msk),
    .interrupt_cfg.data_lane_enable_mask[1] = (R_MIPI_CSI_DLIE0_RULE_Msk | R_MIPI_CSI_DLIE0_EULE_Msk |
                                               R_MIPI_CSI_DLIE0_EESE_Msk | R_MIPI_CSI_DLIE0_ECTE_Msk),
    .interrupt_cfg.virtual_channel_enable_mask[0] = (R_MIPI_CSI_VCST0_MLF_Msk | R_MIPI_CSI_VCST0_ECD_Msk |
                                                     R_MIPI_CSI_VCST0_CRC_Msk | R_MIPI_CSI_VCST0_IDE_Msk |
                                                     R_MIPI_CSI_VCST0_WCE_Msk | R_MIPI_CSI_VCST0_ECC_Msk |
                                                     R_MIPI_CSI_VCST0_FRS_Msk | R_MIPI_CSI_VCST0_FRD_Msk |
                                                     R_MIPI_CSI_VCST0_OVF_Msk),
    .interrupt_cfg.power_management_enable_mask = 0x0,
    .interrupt_cfg.short_packet_enable_mask     = (R_MIPI_CSI_GSIE_GOVE_Msk | 0x0),

    .p_callback = cam_mipi_csi_callback,
    .p_context  = NULL,
    .p_extend   = NULL,
};

const mipi_csi_instance_t g_cam_mipi_csi = {
    .p_ctrl = &g_cam_mipi_csi_ctrl,
    .p_cfg  = &g_cam_mipi_csi_cfg,
    .p_api  = &g_mipi_csi,
};

/* VIN Capture Frame Buffers (128-byte aligned) */
uint8_t vin_image_buffer_1[VIN_BYTES_PER_FRAME] __attribute__((aligned(128)));
uint8_t vin_image_buffer_2[VIN_BYTES_PER_FRAME] __attribute__((aligned(128)));
uint8_t vin_image_buffer_3[VIN_BYTES_PER_FRAME] __attribute__((aligned(128)));

vin_instance_ctrl_t g_cam_vin_ctrl;

const vin_extended_cfg_t g_cam_vin_cfg_extend = {
    .p_mipi_csi_instance = &g_cam_mipi_csi,

    .input_ctrl.cfg_bits.color_space_convert_bypass = 0,
    .input_ctrl.cfg_bits.interlace_mode             = VIN_INTERLACE_MODE_ODD_EVEN_FIELD_CAPTURE,
    .input_ctrl.cfg_bits.big_endian                 = 0,
    .input_ctrl.cfg_bits.dithering_mode             = VIN_DITHERING_MODE_WITH_ADDITION,
    .input_ctrl.cfg_bits.input_mode                 = VIN_INPUT_FORMAT_YCBCR422_8_BIT,
    .input_ctrl.cfg_bits.lut_enable                 = 0,
    .input_ctrl.cfg_bits.dithering_direction        = false,
    .input_ctrl.cfg_bits.yuv444_conversion          = VIN_YUV444_CONVERSION_MODE_DATA_EXTEND,
    .input_ctrl.cfg_bits.scaling_enable             = false,
    .input_ctrl.cfg_bits.pixel_data_clipping        = VIN_PIXEL_DATA_CLIPPING_DEFAULT,

    .input_ctrl.preclip.line_start  = 0,
    .input_ctrl.preclip.line_end    = 239,
    .input_ctrl.preclip.pixel_start = 0,
    .input_ctrl.preclip.pixel_end   = 319,

    .input_ctrl.csi_mode_bits.virtual_channel    = 0,
    .input_ctrl.csi_mode_bits.data_type          = VIN_DATA_TYPE_YUV422_8_BIT,
    .input_ctrl.csi_mode_bits.sign_extend_disable= 1,

    .input_ctrl.csi_detect_bits.field_detect_enable      = 1,
    .input_ctrl.csi_detect_bits.even_field_detect_enable = 1,
    .input_ctrl.csi_detect_bits.even_field_number        = 0,

    .input_ctrl.image_stride = VIN_CFG_IMAGE_STRIDE,

    .output_ctrl.image_buffer = {
        vin_image_buffer_1,
        vin_image_buffer_2,
        vin_image_buffer_3,
    },
    .output_ctrl.use_runtime_buffer = 0,

    .conversion_ctrl.data_mode_bits.data_conversion_mode  = VIN_CONVERSION_MODE_NONE,
    .conversion_ctrl.data_mode_bits.alpha_bit_value       = 1,
    .conversion_ctrl.data_mode_bits.output_data_byte_swap = 1,
    .conversion_ctrl.data_mode_bits.extend_rgb_converted_data = 0,
    .conversion_ctrl.data_mode_bits.yc_data_transform_enable  = 0,
    .conversion_ctrl.data_mode_bits.yc_transform_mode     = VIN_YC_TRANSFORM_MODE_Y_CBCR,
    .conversion_ctrl.data_mode_bits.rgb8888_alpha_value   = 0xAA,

    .conversion_data.uv_address = 0x0,

    .conversion_data.yc_rgb_conversion_setting_1_bits.y_mul             = 4767,
    .conversion_data.yc_rgb_conversion_setting_1_bits.round_down_disable= 0,
    .conversion_data.yc_rgb_conversion_setting_2_bits.csub2             = 2048,
    .conversion_data.yc_rgb_conversion_setting_2_bits.ysub2             = 256,
    .conversion_data.yc_rgb_conversion_setting_3_bits.cgrmul2           = 3330,
    .conversion_data.yc_rgb_conversion_setting_3_bits.rcrmul2           = 6537,
    .conversion_data.yc_rgb_conversion_setting_4_bits.gcbmul2           = 1605,
    .conversion_data.yc_rgb_conversion_setting_4_bits.bcbmul2           = 8261,

    .conversion_data.uds_ctrl_bits.ne_bcb               = 0,
    .conversion_data.uds_ctrl_bits.ne_gy                = 0,
    .conversion_data.uds_ctrl_bits.ne_rcr               = 0,
    .conversion_data.uds_ctrl_bits.pixel_interpolation  = 0,
    .conversion_data.uds_ctrl_bits.bilinear_advanced    = 1,
    .conversion_data.uds_ctrl_bits.scale_up_pixel_count = 0,

    .conversion_data.uds_scale_bits.vertical_mask   = 4096,
    .conversion_data.uds_scale_bits.horizontal_mask = 4096,

    .conversion_data.uds_bwidth_bits.bwidth_v = 64,
    .conversion_data.uds_bwidth_bits.bwidth_h = 64,

    .conversion_data.uds_clipping_bits.cl_vsize = 240,
    .conversion_data.uds_clipping_bits.cl_hsize = 320,

    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_1_bits.lrp = 263,
    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_2_bits.lgp = 516,
    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_2_bits.lbp = 100,
    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_3_bits.lap = 256,
    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_3_bits.lhen = 0,
    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_3_bits.lsft = 10,
    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_3_bits.persistent_bit0 = 1,
    .conversion_data.rgb_to_yuv_conversion_settings[0].setting_3_bits.persistent_bit1 = 1,

    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_1_bits.lrp = -152,
    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_2_bits.lgp = -298,
    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_2_bits.lbp = 450,
    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_3_bits.lap = 2048,
    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_3_bits.lhen = 0,
    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_3_bits.lsft = 10,
    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_3_bits.persistent_bit0 = 1,
    .conversion_data.rgb_to_yuv_conversion_settings[1].setting_3_bits.persistent_bit1 = 1,

    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_1_bits.lrp = 450,
    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_2_bits.lgp = -377,
    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_2_bits.lbp = -73,
    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_3_bits.lap = 2048,
    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_3_bits.lhen = 0,
    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_3_bits.lsft = 10,
    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_3_bits.persistent_bit0 = 1,
    .conversion_data.rgb_to_yuv_conversion_settings[2].setting_3_bits.persistent_bit1 = 1,

    .interrupt_cfg.status_enable_mask = (R_VIN_IE_FME_Msk | 0x0),
    .interrupt_cfg.scanline_compare_value = 0,

    .interrupt_cfg.status.ipl = 2,
    .interrupt_cfg.status.irq = (IRQn_Type)VIN_STATUS_IRQ_NUM,

    .interrupt_cfg.error.ipl = 2,
    .interrupt_cfg.error.irq = (IRQn_Type)VIN_ERROR_IRQ_NUM,
};

const capture_cfg_t g_cam_vin_cfg = {
    .x_capture_start_pixel = 0xFFFF,
    .x_capture_pixels      = 0xFFFF,
    .y_capture_start_pixel = 0xFFFF,
    .y_capture_pixels      = 0xFFFF,
    .bytes_per_pixel       = 0xFF,
    .p_callback            = cam_vin_callback,
    .p_context             = NULL,
    .p_extend              = &g_cam_vin_cfg_extend,
};

const capture_instance_t g_cam_vin = {
    .p_ctrl = &g_cam_vin_ctrl,
    .p_cfg  = &g_cam_vin_cfg,
    .p_api  = &g_capture_on_vin,
};

/* ISR Prototypes from FSP */
extern void vin_status_isr(void);
extern void vin_error_isr(void);
extern void mipi_csi_rx_isr(void);
extern void mipi_csi_dl_isr(void);
extern void mipi_csi_vc_isr(void);
extern void mipi_csi_pm_isr(void);
extern void mipi_csi_gst_isr(void);

/* Zephyr ISR Wrappers */
static void vin_status_isr_entry(const void *param)
{
    ARG_UNUSED(param);
    vin_status_isr();
}

static void vin_error_isr_entry(const void *param)
{
    ARG_UNUSED(param);
    vin_error_isr();
}

static void mipi_csi_rx_isr_entry(const void *param)
{
    ARG_UNUSED(param);
    mipi_csi_rx_isr();
}

static void mipi_csi_dl_isr_entry(const void *param)
{
    ARG_UNUSED(param);
    mipi_csi_dl_isr();
}

static void mipi_csi_vc_isr_entry(const void *param)
{
    ARG_UNUSED(param);
    mipi_csi_vc_isr();
}

static void mipi_csi_pm_isr_entry(const void *param)
{
    ARG_UNUSED(param);
    mipi_csi_pm_isr();
}

static void mipi_csi_gst_isr_entry(const void *param)
{
    ARG_UNUSED(param);
    mipi_csi_gst_isr();
}

/* Connect all MIPI-CSI and VIN interrupts to Zephyr NVIC and RA ICU */
void mipi_vin_irq_init(void)
{
    /* VIN Status (Frame Complete) */
    R_ICU->IELSR[VIN_STATUS_IRQ_NUM] = (uint32_t)ELC_EVENT_VIN_IRQ;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_VIN_IRQ);
    IRQ_CONNECT(VIN_STATUS_IRQ_NUM, 2, vin_status_isr_entry, NULL, 0);
    irq_enable(VIN_STATUS_IRQ_NUM);

    /* VIN Error */
    R_ICU->IELSR[VIN_ERROR_IRQ_NUM] = (uint32_t)ELC_EVENT_VIN_ERR;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_VIN_ERR);
    IRQ_CONNECT(VIN_ERROR_IRQ_NUM, 2, vin_error_isr_entry, NULL, 0);
    irq_enable(VIN_ERROR_IRQ_NUM);

    /* MIPI CSI RX */
    R_ICU->IELSR[MIPICSI_RX_IRQ_NUM] = (uint32_t)ELC_EVENT_MIPICSI_RX;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_MIPICSI_RX);
    IRQ_CONNECT(MIPICSI_RX_IRQ_NUM, 2, mipi_csi_rx_isr_entry, NULL, 0);
    irq_enable(MIPICSI_RX_IRQ_NUM);

    /* MIPI CSI Data Lane */
    R_ICU->IELSR[MIPICSI_DL_IRQ_NUM] = (uint32_t)ELC_EVENT_MIPICSI_DL;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_MIPICSI_DL);
    IRQ_CONNECT(MIPICSI_DL_IRQ_NUM, 2, mipi_csi_dl_isr_entry, NULL, 0);
    irq_enable(MIPICSI_DL_IRQ_NUM);

    /* MIPI CSI Virtual Channel */
    R_ICU->IELSR[MIPICSI_VC_IRQ_NUM] = (uint32_t)ELC_EVENT_MIPICSI_VC;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_MIPICSI_VC);
    IRQ_CONNECT(MIPICSI_VC_IRQ_NUM, 2, mipi_csi_vc_isr_entry, NULL, 0);
    irq_enable(MIPICSI_VC_IRQ_NUM);

    /* MIPI CSI Power Management */
    R_ICU->IELSR[MIPICSI_PM_IRQ_NUM] = (uint32_t)ELC_EVENT_MIPICSI_PM;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_MIPICSI_PM);
    IRQ_CONNECT(MIPICSI_PM_IRQ_NUM, 2, mipi_csi_pm_isr_entry, NULL, 0);
    irq_enable(MIPICSI_PM_IRQ_NUM);

    /* MIPI CSI Generic Short Packet */
    R_ICU->IELSR[MIPICSI_GST_IRQ_NUM] = (uint32_t)ELC_EVENT_MIPICSI_GST;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_MIPICSI_GST);
    IRQ_CONNECT(MIPICSI_GST_IRQ_NUM, 2, mipi_csi_gst_isr_entry, NULL, 0);
    irq_enable(MIPICSI_GST_IRQ_NUM);
}
