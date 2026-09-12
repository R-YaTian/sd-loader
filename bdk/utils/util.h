/*
 * Copyright (c) 2018 naehrwert
 * Copyright (c) 2018-2025 CTCaer
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

#ifndef _UTIL_H_
#define _UTIL_H_

#include <utils/types.h>

typedef enum
{
	REBOOT_RCM,          // PMC reset. Enter RCM mode.
	REBOOT_BYPASS_FUSES, // PMC reset via watchdog. Enter Normal mode. Bypass fuse programming in package1.

	POWER_OFF,           // Power off PMIC. Do not reset regulators.
	POWER_OFF_RESET,     // Power off PMIC. Reset regulators.
	POWER_OFF_REBOOT,    // Power off PMIC. Reset regulators. Power on.
} power_state_t;

typedef struct _reg_cfg_t
{
	u32 idx;
	u32 val;
} reg_cfg_t;

u8   bit_count(u32 val);
u32  bit_count_mask(u8 bits);
char *strcpy_ns(char *dst, char *src);
u64  sqrt64(u64 num);
long strtol(const char *nptr, char **endptr, register int base);
int  atoi(const char *nptr);

void reg_write_array(vu32 *base, const reg_cfg_t *cfg, u32 num_cfg);
u32  crc32_calc(u32 crc, const u8 *buf, u32 len);

int qsort_compare_int(const void *a, const void *b);
int qsort_compare_char(const void *a, const void *b);
int qsort_compare_char_case(const void *a, const void *b);

void panic(u32 val);
void power_set_state(power_state_t state);
void power_set_state_ex(void *param);

// Additional utility functions
void rcm_if_t210_or_off();
bool is_t210();

#endif
