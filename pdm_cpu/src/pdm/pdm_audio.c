/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "bsp_api.h"
#include "r_ioport.h"
#include "r_pdm_api.h"
#include "r_pdm.h"
#include "r_transfer_api.h"
#include "r_dmac.h"
#include "pdm_audio.h"

#define DMAC1_IRQ_NUM        32
#define DMAC1_IRQ_PRIORITY   12

#define PDM_ERR_IRQ_NUM      33
#define PDM_ERR_IRQ_PRIORITY 12

#define PDM2_CALCULATED_SINCRNG_VALUE (9)
#define PDM2_CALCULATED_SINCDEC_VALUE (62)

/* Double buffered PCM raw data */
static int32_t g_pcm32_buffer[FRAME_SAMPLES * 2] __aligned(64);

int16_t g_waveform_history[WAVEFORM_POINTS] = {0};
volatile uint32_t g_pdm_event_count = 0;
volatile uint32_t g_rms_val = 0;
volatile int32_t  g_peak_to_peak = 0;
volatile bool     g_pdm_data_ready = false;
volatile int32_t  g_pcm32_debug_samples[4] = {0};
volatile int32_t  g_pdm_open_err = 0;
volatile int32_t  g_pdm_start_err = 0;
int16_t           g_audio_buffer[AUDIO_BUFFER_SIZE] = {0};
volatile uint32_t g_audio_buffer_write_ptr = 0;
volatile bool     g_audio_buffer_ready = false;
static uint8_t    g_pcm32_frame_idx = 0;

/* Forward declarations */
void pdm_rxi_dmac_isr(dmac_callback_args_t *args);
void dmac_int_isr(void);
void pdm_err_isr(void);

extern pdm_instance_ctrl_t g_pdm0_ctrl;

/* Integer square root helper */
static uint32_t integer_sqrt(uint32_t val)
{
    uint32_t temp, g = 0;
    uint32_t b = 0x80000000;
    while (b > 0)
    {
        temp = g + b;
        if (val >= temp)
        {
            val -= temp;
            g = temp + b;
        }
        g >>= 1;
        b >>= 2;
    }
    return g;
}

/* PDM Callback triggered when DMAC finishes transferring a block of 1600 samples */
void pdm_callback(pdm_callback_args_t *p_args)
{
    if (p_args->event == PDM_EVENT_DATA)
    {
        uint32_t src_offset = g_pcm32_frame_idx * FRAME_SAMPLES;
        int32_t *p_read = &g_pcm32_buffer[src_offset];

        /* Invalidate cache before reading DMA-updated buffer */
        SCB_InvalidateDCache_by_Addr((void *)p_read, FRAME_SAMPLES * sizeof(int32_t));

        /* Keep raw samples for debug inspection */
        for (int k = 0; k < 4; k++) {
            g_pcm32_debug_samples[k] = p_read[k];
        }

        int32_t min_val = 2147483647;
        int32_t max_val = -2147483648;
        uint64_t sum_squares = 0;

        for (uint32_t i = 0; i < FRAME_SAMPLES; i++)
        {
            int32_t val = p_read[i];

            /* Sign-extend 20-bit signed PCM data (right-justified) to 32-bit signed integer */
            int32_t extended = (val << 12) >> 12;

            /* Normalize 20-bit to 16-bit signed range (-32768 to 32767) by 4-bit right shift */
            int16_t sample16 = (int16_t)(extended >> 4);

            if (extended < min_val) min_val = extended;
            if (extended > max_val) max_val = extended;
            sum_squares += (int64_t)sample16 * sample16;

            if (i < WAVEFORM_POINTS)
            {
                g_waveform_history[i] = sample16;
            }

            /* Downsample by 2: append 16-bit PCM values to circular buffer at 16kHz */
            if ((i % 2) == 0)
            {
                g_audio_buffer[g_audio_buffer_write_ptr] = sample16;
                uint32_t next_ptr = g_audio_buffer_write_ptr + 1;
                if (next_ptr >= AUDIO_BUFFER_SIZE)
                {
                    next_ptr = 0;
                    g_audio_buffer_ready = true;
                }
                g_audio_buffer_write_ptr = next_ptr;
            }
        }

        g_peak_to_peak = (max_val - min_val) >> 4;
        g_rms_val = integer_sqrt((uint32_t)(sum_squares / FRAME_SAMPLES));

        g_pdm_event_count++;
        g_pdm_data_ready = true;
        g_pcm32_frame_idx ^= 1U;
    }
    else if (p_args->event == PDM_EVENT_ERROR)
    {
        printk("PDM ERROR EVENT: 0x%08X\n", (unsigned int)p_args->error);
    }
}

/* DMAC Transfer Instance Configuration */
dmac_instance_ctrl_t g_transfer1_ctrl;
transfer_info_t g_transfer1_info =
{
    .transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_INCREMENTED,
    .transfer_settings_word_b.repeat_area    = TRANSFER_REPEAT_AREA_SOURCE,
    .transfer_settings_word_b.irq            = TRANSFER_IRQ_END,
    .transfer_settings_word_b.chain_mode     = TRANSFER_CHAIN_MODE_DISABLED,
    .transfer_settings_word_b.src_addr_mode  = TRANSFER_ADDR_MODE_FIXED,
    .transfer_settings_word_b.size           = TRANSFER_SIZE_4_BYTE,
    .transfer_settings_word_b.mode           = TRANSFER_MODE_BLOCK,
    .p_dest                                  = (void *)NULL,
    .p_src                                   = (void const *)NULL,
    .num_blocks                              = 0,
    .length                                  = 0,
};

const dmac_extended_cfg_t g_transfer1_extend =
{
    .offset            = 1,
    .src_buffer_size   = 1,
    .irq               = (IRQn_Type)DMAC1_IRQ_NUM,
    .ipl               = (DMAC1_IRQ_PRIORITY),
    .channel           = 1,
    .p_callback        = pdm_rxi_dmac_isr,
    .p_context         = &g_pdm0_ctrl,
    .activation_source = ELC_EVENT_PDM_DAT2,
};

const transfer_cfg_t g_transfer1_cfg =
{
    .p_info   = &g_transfer1_info,
    .p_extend = &g_transfer1_extend,
};

const transfer_instance_t g_transfer1 =
{
    .p_ctrl = &g_transfer1_ctrl,
    .p_cfg  = &g_transfer1_cfg,
    .p_api  = &g_transfer_on_dmac,
};

/* PDM Driver Instance Configuration */
pdm_instance_ctrl_t g_pdm0_ctrl;

const pdm_extended_cfg_t g_pdm0_cfg_extend =
{
    .clock_div = PDM_CLOCK_DIV_2,

    /* Function Settings */
    .short_circuit_detection_enable            = PDM_SHORT_CIRCUIT_ENABLED,
    .over_voltage_lower_limit_detection_enable = PDM_OVERVOLTAGE_LOWER_LIMIT_ENABLED,
    .over_voltage_upper_limit_detection_enable = PDM_OVERVOLTAGE_UPPER_LIMIT_ENABLED,
    .buffer_overwrite_detection_enable         = PDM_BUFFER_OVERWRITE_DETECTION_ENABLED,

    /* Filter Settings */
    .moving_average_mode         = PDM_MOVING_AVERAGE_MODE_1_ORDER,
    .low_pass_filter_shift       = PDM_LPF_RIGHT_SHIFT_0,
    .compensation_filter_shift   = PDM_COMPENSATION_FILTER_RIGHT_SHIFT_0,
    .high_pass_filter_shift      = PDM_HPF_RIGHT_SHIFT_0,
    .sinc_filter_mode            = PDM_SINC_FILTER_MODE_4,
    .sincrng                     = PDM2_CALCULATED_SINCRNG_VALUE,
    .sincdec                     = PDM2_CALCULATED_SINCDEC_VALUE,
    .hpf_coefficient_s0          = 0x3F61,
    .hpf_coefficient_k1          = 0x3EC1,
    .hpf_coefficient_h           = { 0x4000, 0xC000 },
    .compensation_filter_coefficient_h = {
        0x1FE8, 0x0039, 0x003C, 0x1E56, 0x01DC, 0x06E1,
        0x01DC, 0x1E56, 0x003C, 0x0039, 0x1FE8
    },
    .lpf_coefficient_h0          = 0x0400,
    .lpf_coefficient_h1          = {
        0x1FF8, 0x000A, 0x1FF0, 0x0018, 0x1FDC, 0x0034, 0x1FB3, 0x0076,
        0x1F2E, 0x0289, 0x0289, 0x1F2E, 0x0076, 0x1FB3, 0x0034, 0x1FDC,
        0x0018, 0x1FF0, 0x000A, 0x1FF8
    },

    /* Data Threshold */
    .interrupt_threshold = PDM_INTERRUPT_THRESHOLD_16,

    /* Short-Circuit Detection */
    .short_circuit_count_h = 0x1FFF,
    .short_circuit_count_l = 0x1FFF,

    /* Overvoltage Detection */
    .overvoltage_detection_lower_limit = 0x85EE0,
    .overvoltage_detection_upper_limit = 0x7A120,
};

const pdm_cfg_t g_pdm0_cfg =
{
    .unit          = 0,
    .channel       = 2,
    .pcm_width     = PDM_PCM_WIDTH_20_BITS_0_18,
    .pcm_edge      = PDM_INPUT_DATA_EDGE_RISE,
    .p_transfer_rx = &g_transfer1,
    .p_callback    = pdm_callback,
    .p_context     = NULL,
    .p_extend      = &g_pdm0_cfg_extend,
    .sdet_irq      = FSP_INVALID_VECTOR,
    .sdet_ipl      = (12),
    .dat_irq       = FSP_INVALID_VECTOR,
    .dat_ipl       = (BSP_IRQ_DISABLED),
    .err_irq       = (IRQn_Type)PDM_ERR_IRQ_NUM,
    .err_ipl       = (PDM_ERR_IRQ_PRIORITY),
};

const pdm_instance_t g_pdm0 =
{
    .p_ctrl = &g_pdm0_ctrl,
    .p_cfg  = &g_pdm0_cfg,
    .p_api  = &g_pdm_on_pdm,
};

/* ISR Wrappers for Zephyr */
static void zephyr_dmac1_isr(const void *param)
{
    ARG_UNUSED(param);
    dmac_int_isr();
}

static void zephyr_pdm_err_isr(const void *param)
{
    ARG_UNUSED(param);
    pdm_err_isr();
}

/* Initialization Routine */
int pdm_audio_init(void)
{
    printk("Configuring PDM Pins (P502: MIC_DAT, P812: MIC_CLK)...\n");
    R_BSP_PinAccessEnable();
    R_BSP_PinCfg(BSP_IO_PORT_05_PIN_02, (uint32_t)IOPORT_CFG_DRIVE_MID | (uint32_t)IOPORT_CFG_PERIPHERAL_PIN | (uint32_t)IOPORT_PERIPHERAL_PDM);
    R_BSP_PinCfg(BSP_IO_PORT_08_PIN_12, (uint32_t)IOPORT_CFG_DRIVE_MID | (uint32_t)IOPORT_CFG_PERIPHERAL_PIN | (uint32_t)IOPORT_PERIPHERAL_PDM);
    R_BSP_PinAccessDisable();

    printk("Connecting DMAC1 (IRQ %d) and PDM_ERR2 (IRQ %d) to ICU & NVIC...\n", DMAC1_IRQ_NUM, PDM_ERR_IRQ_NUM);
    /* Connect DMAC1 interrupt */
    R_ICU->IELSR[DMAC1_IRQ_NUM] = (uint32_t)ELC_EVENT_DMAC1_INT;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_DMAC1_INT);
    IRQ_CONNECT(DMAC1_IRQ_NUM, DMAC1_IRQ_PRIORITY, zephyr_dmac1_isr, NULL, 0);
    irq_enable(DMAC1_IRQ_NUM);

    /* Connect PDM ERR2 interrupt */
    R_ICU->IELSR[PDM_ERR_IRQ_NUM] = (uint32_t)ELC_EVENT_PDM_ERR2;
    BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_PDM_ERR2);
    IRQ_CONNECT(PDM_ERR_IRQ_NUM, PDM_ERR_IRQ_PRIORITY, zephyr_pdm_err_isr, NULL, 0);
    irq_enable(PDM_ERR_IRQ_NUM);

    printk("Opening PDM driver (g_pdm0)...\n");
    fsp_err_t pdm_err = R_PDM_Open(&g_pdm0_ctrl, &g_pdm0_cfg);
    g_pdm_open_err = (int32_t)pdm_err;
    if (FSP_SUCCESS != pdm_err)
    {
        printk("ERROR: Failed to open PDM driver (0x%04X)\n", pdm_err);
        return -1;
    }

    printk("PDM driver opened. Waiting for sinc filter settling (60ms)...\n");
    k_msleep(60);

    printk("Starting PDM capture with DMAC (1600 samples / 50ms frames)...\n");
    pdm_err = R_PDM_Start(&g_pdm0_ctrl, g_pcm32_buffer, sizeof(g_pcm32_buffer), FRAME_SAMPLES);
    g_pdm_start_err = (int32_t)pdm_err;
    if (FSP_SUCCESS != pdm_err)
    {
        printk("ERROR: Failed to start PDM capture (0x%04X)\n", pdm_err);
        return -2;
    }

    printk("PDM capture started successfully!\n");
    return 0;
}
