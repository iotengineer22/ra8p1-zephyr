/*
 * Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ETHOSU_LOG_H
#define ETHOSU_LOG_H

#include <stdio.h>
#include <string.h>
#include <zephyr/sys/printk.h>

// Log severity levels
#define RM_ETHOSU_LOG_DISABLED    0
#define RM_ETHOSU_LOG_ERR         1
#define RM_ETHOSU_LOG_WARN        2
#define RM_ETHOSU_LOG_INFO        3
#define RM_ETHOSU_LOG_DEBUG       4

#define RM_ETHOSU_LOG_SEVERITY    RM_ETHOSU_LOG_ERR

#if RM_ETHOSU_LOG_SEVERITY >= RM_ETHOSU_LOG_ERR
 #define LOG(f, ...)    (void) printk(f, ## __VA_ARGS__)
#else
 #define LOG(f, ...)
#endif

#if RM_ETHOSU_LOG_SEVERITY >= RM_ETHOSU_LOG_ERR
 #define LOG_ERR(f, ...)    (void) printk("Ethos-U Err: " f "\n", ## __VA_ARGS__)
#else
 #define LOG_ERR(f, ...)
#endif

#if RM_ETHOSU_LOG_SEVERITY >= RM_ETHOSU_LOG_WARN
 #define LOG_WARN(f, ...)    (void) printk("Ethos-U Warn: " f "\n", ## __VA_ARGS__)
#else
 #define LOG_WARN(f, ...)
#endif

#if RM_ETHOSU_LOG_SEVERITY >= RM_ETHOSU_LOG_INFO
 #define LOG_INFO(f, ...)    (void) printk("Ethos-U Info: " f "\n", ## __VA_ARGS__)
#else
 #define LOG_INFO(f, ...)
#endif

#if RM_ETHOSU_LOG_SEVERITY >= RM_ETHOSU_LOG_DEBUG
 #define LOG_DEBUG(f, ...)    (void) printk("Ethos-U Debug: " f "\n", ## __VA_ARGS__)
#else
 #define LOG_DEBUG(f, ...)
#endif

#endif // ETHOSU_LOG_H
