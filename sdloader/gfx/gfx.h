/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018-2021 CTCaer
 * Copyright (c) 2018 M4xw
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GFX_H_
#define _GFX_H_

#include <utils/types.h>

#define COL_BLACK 253
#define COL_WHITE 254
#define COL_GREY  255
#define COL_RED   252
#define COL_GREEN 251
#define COL_BLUE  250
#define COL_TEAL  249
#define COL_DARK_GREY 248
#define COL_LIGHT_GREY 247
#define COL_ORANGE 246
#define COL_DARK_GREEN 245
#define COL_DARK_RED 244
#define COL_DARK_DARK_GREY 243

typedef struct _gfx_ctxt_t
{
	u8 *fb;
	u32 width;
	u32 height;
	u32 stride;
} gfx_ctxt_t;

typedef struct _gfx_con_t
{
	gfx_ctxt_t *gfx_ctxt;
	u32 fntsz;
	u32 x;
	u32 y;
	u32 savedx;
	u32 savedy;
	u8 fgcol;
	int fillbg;
	u8 bgcol;
	bool mute;
} gfx_con_t;

// Global gfx console and context.
extern gfx_ctxt_t gfx_ctxt;
extern gfx_con_t gfx_con;

void gfx_init_ctxt(u8 *fb, u32 width, u32 height, u32 stride);
void gfx_clear_color(u32 color);
void gfx_con_init();
void gfx_con_setcol(u32 fgcol, int fillbg, u32 bgcol);
void gfx_con_getpos_rot(u32 *x, u32 *y);
void gfx_con_setpos_rot(u32 x, u32 y);
void gfx_putc(char c);
void gfx_putc_rot(char c);
void gfx_printf(const char *fmt, ...);
void gfx_printf_rot(const char *fmt, ...);
void gfx_print_centered_rot(const char *fmt);

void gfx_con_set_origin_rot(u32 x, u32 y);
void gfx_con_get_origin_rot(u32 *x, u32 *y);

void gfx_clear_rect(u8 color, u32 pos_x, u32 pos_y, u32 width, u32 height);
void gfx_clear_rect_rot(u8 color, u32 pos_x, u32 pos_y, u32 width, u32 height);

void gfx_render_bmp_2bit_rot(const u8 *buf, u32 size_x, u32 size_y, u32 pos_x, u32 pos_y);

#endif
