/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <stdlib.h>
#include "bsp_api.h"
#include "r_drw_base.h"
#include "r_drw_cfg.h"

#define DRW_PRV_IRQCTL_DLISTIRQ_ENABLE             (1U << 1)
#define DRW_PRV_IRQCTL_ENUMIRQ_CLEAR               (1U << 2)
#define DRW_PRV_IRQCTL_DLISTIRQ_CLEAR              (1U << 3)
#define DRW_PRV_IRQCTL_BUSIRQ_CLEAR                (1U << 5)
#define DRW_PRV_IRQCTL_ALLIRQ_DISABLE_AND_CLEAR    (DRW_PRV_IRQCTL_BUSIRQ_CLEAR | DRW_PRV_IRQCTL_DLISTIRQ_CLEAR | \
                                                    DRW_PRV_IRQCTL_ENUMIRQ_CLEAR)
#define DRW_PRV_IRQCTL_ALLIRQ_CLEAR_AND_DLISTIRQ_ENABLE                                           \
    (DRW_PRV_IRQCTL_BUSIRQ_CLEAR | DRW_PRV_IRQCTL_DLISTIRQ_CLEAR | DRW_PRV_IRQCTL_ENUMIRQ_CLEAR | \
     DRW_PRV_IRQCTL_DLISTIRQ_ENABLE)
#define DRW_PRV_STATUS_DLISTIRQ_TRIGGERED          (1U << 5)
#define DRW_PRV_STATUS_DLISTACTIVE                 (1U << 3)

static struct k_sem g_drw_sem;
static volatile uint32_t g_dlist_done = 0;
static bool g_drw_irq_initialized = false;
static d1_device_flex *g_drw_handle = NULL;

void drw_int_isr(const void *param)
{
    ARG_UNUSED(param);
    uint32_t int_status = R_DRW->STATUS;

    /* Clear all interrupts and keep display list interrupt enabled */
    R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_CLEAR_AND_DLISTIRQ_ENABLE;

    if (int_status & DRW_PRV_STATUS_DLISTIRQ_TRIGGERED)
    {
#if DRW_CFG_USE_DLIST_INDIRECT
        if (g_drw_handle && g_drw_handle->dlist_indirect_enable &&
            (NULL != (uint32_t *) *(g_drw_handle->pp_dlist_indirect_start)))
        {
            R_DRW->DLISTSTART = *(g_drw_handle->pp_dlist_indirect_start);
            g_drw_handle->pp_dlist_indirect_start++;
            R_BSP_IrqStatusClear((IRQn_Type)DRW_IRQ_NUM);
            return;
        }
#endif
        g_dlist_done++;
        k_sem_give(&g_drw_sem);
    }

    /* Clear ICU IR flag to prevent infinite interrupt storm */
    R_BSP_IrqStatusClear((IRQn_Type)DRW_IRQ_NUM);
}

d1_int_t d1_initirq_intern(d1_device_flex *handle)
{
    g_drw_handle = handle;

    if (!g_drw_irq_initialized)
    {
        k_sem_init(&g_drw_sem, 0, 1);

        /* Connect and enable DRW interrupt */
        R_ICU->IELSR[DRW_IRQ_NUM] = (uint32_t)ELC_EVENT_DRW_INT;
        BSP_ASSIGN_EVENT_TO_CURRENT_CORE((uint32_t)ELC_EVENT_DRW_INT);
        IRQ_CONNECT(DRW_IRQ_NUM, DRW_IRQ_PRIORITY, drw_int_isr, NULL, 0);
        irq_enable(DRW_IRQ_NUM);

        g_drw_irq_initialized = true;
    }

    /* Clear interrupts and enable DLIST IRQ */
    R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_CLEAR_AND_DLISTIRQ_ENABLE;

    return 1;
}

d1_int_t d1_shutdownirq_intern(d1_device_flex *handle)
{
    ARG_UNUSED(handle);
    irq_disable(DRW_IRQ_NUM);
    R_DRW->IRQCTL = DRW_PRV_IRQCTL_ALLIRQ_DISABLE_AND_CLEAR;
    return 1;
}

d1_int_t d1_queryirq(d1_device *handle, d1_int_t irqmask, d1_int_t timeout)
{
    ARG_UNUSED(handle);
    ARG_UNUSED(irqmask);

    /* Fast hardware check: if DLIST is already not active, return immediately */
    if ((R_DRW->STATUS & DRW_PRV_STATUS_DLISTACTIVE) == 0)
    {
        return 1;
    }

    /* Wait for ISR completion semaphore */
    k_timeout_t to = (timeout <= 0) ? K_FOREVER : K_MSEC(timeout);
    if (k_sem_take(&g_drw_sem, to) == 0)
    {
        return 1;
    }

    /* In case of timeout, check hardware one last time */
    if ((R_DRW->STATUS & DRW_PRV_STATUS_DLISTACTIVE) == 0)
    {
        return 1;
    }

    return 0;
}
