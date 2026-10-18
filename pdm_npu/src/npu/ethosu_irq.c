/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/printk.h>
#include "ethosu_irq.h"
#include "ethosu_driver.h"
#include "bsp_elc.h"
#include "bsp_api.h"

struct ethosu_driver g_ethosu0;

rm_ethosu_extended_cfg_t g_rm_ethosu0_ext_cfg = {
    .p_dev = &g_ethosu0,
};

rm_ethosu_instance_ctrl_t g_rm_ethosu0_ctrl = {
    .p_ext_cfg = &g_rm_ethosu0_ext_cfg,
};

const rm_ethosu_cfg_t g_rm_ethosu0_cfg = {
    .p_callback       = NULL,
    .p_context        = NULL,
    .ipl              = 2,
    .irq              = (IRQn_Type)NPU_IRQ_NUM,
    .secure_enable    = 1,
    .privilege_enable = 1,
};

const rm_ethosu_instance_t g_rm_ethosu0 = {
    .p_ctrl = &g_rm_ethosu0_ctrl,
    .p_cfg  = &g_rm_ethosu0_cfg,
    .p_api  = &g_rm_ethosu_on_npu,
};

static volatile uint32_t s_npu_irq_count = 0;

static void ethosu_zephyr_isr_wrapper(const void *arg)
{
    ARG_UNUSED(arg);
    s_npu_irq_count++;

    /* 1. Dispatch to Ethos-U Core Driver ISR (clears NPU hardware IRQ) */
    ethosu_irq_handler(&g_ethosu0);

    /* 2. Clear Renesas RA ICU IRQ status bit AFTER NPU peripheral IRQ is cleared */
    R_BSP_IrqStatusClear((IRQn_Type)NPU_IRQ_NUM);

    __DSB();
    __ISB();
}

void ethosu_irq_connect(void)
{
    /* Connect NPU interrupt to Zephyr NVIC */
    IRQ_CONNECT(NPU_IRQ_NUM, 2, ethosu_zephyr_isr_wrapper, NULL, 0);
    irq_enable(NPU_IRQ_NUM);

    /* Bind Renesas Event Link Controller (ELC) event to the vector */
    R_ICU->IELSR[NPU_IRQ_NUM] = (uint32_t)ELC_EVENT_NPU_IRQ;
}


