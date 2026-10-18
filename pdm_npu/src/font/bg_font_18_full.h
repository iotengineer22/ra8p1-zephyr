/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/
/**********************************************************************************************************************
 * File Name    : bg_font_18_full.h
 * Description  : This file defines the exported MIPI LCD text print functions.
 **********************************************************************************************************************/

/* GIMP RGB C-Source image dump (bg_font_18_full.h) */

#ifndef BG_FONT_18_FULL_H_
#define BG_FONT_18_FULL_H_

#include "bsp_api.h"
#include "dave_driver.h"

FSP_CPP_HEADER
void print_bg_font_18(d2_device *handle, d2_point _xs, d2_point _ys, float scaling, char *_str);

void lcd_draw_char_rgb565(uint16_t *fb, int fb_width, int fb_height,
                          int x, int y, char c, uint16_t color, bool draw_bg, uint16_t bg_color);

int lcd_draw_string_rgb565(uint16_t *fb, int fb_width, int fb_height,
                           int x, int y, const char *str, uint16_t color, bool draw_bg, uint16_t bg_color);

int lcd_get_string_width(const char *str);

void lcd_fill_rect_rgb565(uint16_t *fb, int fb_width, int fb_height,
                          int x, int y, int w, int h, uint16_t color);

void lcd_draw_rect_rgb565(uint16_t *fb, int fb_width, int fb_height,
                          int x, int y, int w, int h, uint16_t color);
FSP_CPP_FOOTER

#endif // BG_FONT_18_FULL_H_

