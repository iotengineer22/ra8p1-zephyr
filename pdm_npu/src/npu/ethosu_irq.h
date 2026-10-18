/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ETHOSU_IRQ_H_
#define ETHOSU_IRQ_H_

#include <stdint.h>
#include "bsp_api.h"
#include "rm_ethosu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NPU_IRQ_NUM (20)

extern rm_ethosu_instance_ctrl_t g_rm_ethosu0_ctrl;
extern const rm_ethosu_cfg_t g_rm_ethosu0_cfg;
extern const rm_ethosu_instance_t g_rm_ethosu0;

void ethosu_irq_connect(void);

#ifdef __cplusplus
}
#endif

#endif /* ETHOSU_IRQ_H_ */
