/*
 * Copyright (c) 2019-2021 CTCaer
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

#ifndef _MEMORY_MAP_H_
#define _MEMORY_MAP_H_

#define IRAM_START  0x40000000

#ifndef IPL_LOAD_ADDR
#define IPL_LOAD_ADDR             0x40003000 //64K max
#endif

#define IPL_SIZE_MAX              0x10000

#define IPL_SMALL_FB_SZ           (SZ_32K + SZ_16K + SZ_8K + SZ_4K)

#define IPL_HEAP_SIZE_MAX         (SZ_1K)
#define IPL_STACK_SIZE_MAX        (SZ_1K * 8)
#define IPL_STACK_TOP             0x40040000

#define IPL_HEAP_START            (IPL_STACK_TOP - IPL_STACK_SIZE_MAX - IPL_HEAP_SIZE_MAX)

#define IPL_SMALL_FB_ADDR         (IPL_HEAP_START - IPL_SMALL_FB_SZ)

// load payload to buffer after ipl, will be relocated to 0x40010000 before jumping to it
#define PAYLOAD_BUF_ADDR          (IPL_LOAD_ADDR + IPL_SIZE_MAX)
#define PAYLOAD_SIZE_MAX          (IPL_HEAP_START - PAYLOAD_BUF_ADDR)

#define PAYLOAD_SIZE_SAFE         (IPL_SMALL_FB_ADDR - PAYLOAD_BUF_ADDR)

#define PAYLOAD_LOAD_ADDR         0x40010000

#define SDMMC_UPPER_BUFFER        PAYLOAD_BUF_ADDR
#define SDMMC_UP_BUF_SZ           PAYLOAD_SIZE_SAFE


#if (PAYLOAD_SIZE_MAX) < 0x20000
#error Payload buffer too small
#endif

#if (PAYLOAD_SIZE_SAFE) < 0x10000
#error Payload buffer too small
#endif

#define DRAM_START                0x80000000

// Framebuffer addresses.
#define IPL_FB_ADDRESS   0xF5A00000
#define IPL_FB_SZ         0x384000 // 720 x 1280 x 4.
#define LOG_FB_ADDRESS   0xF5E00000
#define LOG_FB_SZ         0x334000 // 1280 x 656 x 4.
#define NYX_FB_ADDRESS   0xF6200000

#endif
