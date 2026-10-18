/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PDM_AUDIO_H_
#define PDM_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_SAMPLES   (1600) // 50ms frame at 32kHz
#define WAVEFORM_POINTS (1024)

/* Audio statistics & waveform buffer */
extern volatile uint32_t g_pdm_event_count;
extern volatile uint32_t g_rms_val;
extern volatile int32_t  g_peak_to_peak;
extern volatile bool     g_pdm_data_ready;
extern int16_t           g_waveform_history[WAVEFORM_POINTS];
extern volatile int32_t  g_pcm32_debug_samples[4];
extern volatile int32_t  g_pdm_open_err;
extern volatile int32_t  g_pdm_start_err;

/* Initialize PDM pins, DMAC, ICU, and start audio capture */
int pdm_audio_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PDM_AUDIO_H_ */
