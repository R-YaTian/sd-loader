/*
 * Copyright (c) 2021-2022 HWFLY
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

#include <string.h>
#include <stdlib.h>

#include <tui.h>
#include <soc/timer.h>
#include <storage/sd.h>
#include <storage/emmc.h>
#include <storage/sdmmc_driver.h>

#include "hwfly.h"
#include "files.h"
#include "modchip.h"
#include "memory_map.h"

#define DFU_XFER_BYTES 64
#define DFU_FW_OFFSET  0x3000
#define DFU_FLASH_SIZE 0x20000

#define TRAIN_DATA_ACK         0xA11600D
#define TRAIN_DATA_RESET_MAGIC 0x14CCB847

enum DFU_ERRORS
{
	ERROR_SUCCESS                = 0x70000000,
	ERROR_INVALID_PACKAGE_LENGTH = 0x40000000,
	ERROR_INVALID_OFFSET,       // 0x40000001
	ERROR_INVALID_LENGTH,       // 0x40000002
	ERROR_ERASE_FAILED,         // 0x40000003
	ERROR_FLASH_FAILED,         // 0x40000004
	ERROR_FAILED_TO_UPDATE_OB,  // 0x40000005

	ERROR_UNIMPLEMENTED          = 0x50000000,

	ERROR_XFER_TIMEOUT           = 0xFFFFFFFF
};

enum DFU_COMMANDS
{
	DFU_PING         = 0xA0F0,
	DFU_SET_OFFSET, // 0xA0F1
	DFU_READ_FLASH, // 0xA0F2
	DFU_READ_OB,    // 0xA0F3
	DFU_SET_OB,     // 0xA0F4
};

typedef struct
{
	uint8_t len;
	uint16_t cmd;
	uint32_t arg;
	uint32_t arg2;
} __attribute__((packed)) dfu_cmd_t;

typedef struct
{
	uint8_t resp;
	uint32_t result;
} __attribute__((packed)) dfu_resp_t;

typedef struct
{
	uint8_t cmd; // FW_COMMAND
	union
	{
		struct
		{
			uint16_t cmd;
			uint32_t arg;
			uint32_t arg2;
		} __attribute__((packed)) dfu;
		uint32_t train_data_magic;
	};
} __attribute__((packed)) sdio_req_t;

typedef struct
{
	uint8_t cmd; // ~FW_COMMAND
	union
	{
		uint32_t dfu_result;
		uint32_t fw_info;
		uint32_t train_data_ack;
		struct
		{
			uint32_t magic : 24;
			uint32_t format : 8;
			session_info_t data;
		} session_info;
	};
} __attribute__((packed)) sdio_resp_t;

uint8_t *MMC_BUFFER = (uint8_t *)SDMMC_UPPER_BUFFER;

// HWFLY xfer
static uint32_t hwfly_xfer_start(uint32_t timeout_s)
{
	uint32_t timeout = get_tmr_ms() + timeout_s * 1000;
	uint8_t b0inv = (uint8_t)~MMC_BUFFER[0];

	modchip_hwfly_send(MMC_BUFFER);
	do
	{
		msleep(10);
		modchip_hwfly_recv(MMC_BUFFER);

		if (get_tmr_ms() > timeout)
		{
			return ERROR_XFER_TIMEOUT;
		}
	} while (MMC_BUFFER[0] != b0inv);

	return 0;
}

static uint32_t hwfly_dfu_xfer_start(uint8_t len, uint32_t timeout_s)
{
	dfu_cmd_t *mcmd = (dfu_cmd_t *)MMC_BUFFER;
	mcmd->len = len;

	dfu_resp_t *mresp = (dfu_resp_t *)MMC_BUFFER;
	uint32_t xfer_res = hwfly_xfer_start(timeout_s);

	if (xfer_res)
	{
		return xfer_res;
	}
	else
	{
		return mresp->result != ERROR_SUCCESS;
	}
}

// HWFLY DFU
static uint32_t hwfly_dfu_ping()
{
	dfu_cmd_t *mcomms = (dfu_cmd_t *)MMC_BUFFER;

	mcomms->cmd = DFU_PING;
	if (!hwfly_dfu_xfer_start(sizeof(mcomms->cmd), 5))
	{
		return 0;
	}

	return 1;
}

static int hwfly_dfu_set_flash_addr(uint32_t offset)
{
	dfu_cmd_t *mcomms = (dfu_cmd_t *)MMC_BUFFER;

	if (hwfly_dfu_ping())
		return 1;

	mcomms->cmd = DFU_SET_OFFSET;
	mcomms->arg = offset;
	if (!hwfly_dfu_xfer_start(sizeof(mcomms->cmd) + sizeof(mcomms->arg), 10))
		return 0;

	return 2;
}

static int hwfly_dfu_read_flash(uint32_t offset, void *buffer)
{
	dfu_cmd_t *mcomms = (dfu_cmd_t *)MMC_BUFFER;
	uint8_t len = sizeof(mcomms->cmd) + sizeof(mcomms->arg) * 2;

	mcomms->len = len;
	mcomms->cmd = DFU_READ_FLASH;
	mcomms->arg = offset;
	mcomms->arg2 = DFU_XFER_BYTES;

	uint32_t timeout = get_tmr_ms() + 10000;
	modchip_hwfly_send(MMC_BUFFER);

	do
	{
		// receive data
		msleep(10);
		modchip_hwfly_recv(MMC_BUFFER);

		if (get_tmr_ms() > timeout)
			return 1;
	} while (MMC_BUFFER[0] != (uint8_t) ~len);

	memcpy(buffer, &MMC_BUFFER[1], DFU_XFER_BYTES);

	return 0;
}

// HWFLY Firmware
static uint32_t hwfly_get_fw_version(uint32_t *version)
{
	memset(MMC_BUFFER, 0, MODCHIP_SECTOR_SIZE);

	sdio_req_t *mreq = (sdio_req_t *)MMC_BUFFER;
	mreq->cmd = FW_GET_VER;

	sdio_resp_t *mresp = (sdio_resp_t *)MMC_BUFFER;
	uint32_t xfer_res = hwfly_xfer_start(2);
	if (!xfer_res)
	{
		*version = mresp->dfu_result;
	}

	return xfer_res;
}

static uint32_t hwfly_session_info_get(uint8_t *format, session_info_t* si)
{
	sdio_req_t *mreq = (sdio_req_t *)MMC_BUFFER;
	mreq->cmd = FW_SESSION_INFO;

	sdio_resp_t *mresp = (sdio_resp_t *)MMC_BUFFER;
	uint32_t xfer_res = hwfly_xfer_start(2);
	if (!xfer_res)
	{
		if (mresp->session_info.magic == SESSION_INFO_MAGIC)
		{
			*format = mresp->session_info.format;
			*si = mresp->session_info.data;
			return 0;
		}
		else
			return 1; // invalid magic
	}
	else
		return xfer_res;
}

static uint32_t hwfly_train_data_reset()
{
	sdio_req_t *mreq = (sdio_req_t *)MMC_BUFFER;
	mreq->cmd = FW_RESET_TRAIN;
	mreq->train_data_magic = TRAIN_DATA_RESET_MAGIC;

	sdio_resp_t *mresp = (sdio_resp_t *)MMC_BUFFER;
	uint32_t xfer_res = hwfly_xfer_start(2);

	return xfer_res ? xfer_res : mresp->train_data_ack != TRAIN_DATA_ACK;
}

// HWFLY features
/*
bool hwfly_update_fw(FIL *fw)
{
	FIL fp;
	uint32_t new_version = 0;

	sdmmc_init(&emmc_sdmmc, SDMMC_4, SDMMC_POWER_1_8, SDMMC_BUS_WIDTH_1, SDHCI_TIMING_MMC_ID);

	FRESULT res = open_file_on(".force_update", &fp, 0);
	bool force_update = res == FR_OK;

	uint32_t current_version;
	if (hwfly_get_fw_version(&current_version) == ERROR_XFER_TIMEOUT)
	{
		if (hwfly_dfu_ping() || !force_update)
		{
			goto out;
		}
	}

	u32 version_print = current_version < 0x100 ? (current_version << 8) : current_version;
	gfx_printf("Current FW Ver: %d.%d.%d\nForce update: %d\n\n", version_print >> 16, (version_print >> 8) & 0xFF, version_print & 0xFF, force_update);

	u32 size = 0;
	uint8_t *firmware = sd_file_read("firmware.bin", &size);

	if (!firmware) {
		// EPRINTF("firmware.bin not found!\n");
		goto out;
	}

	// open update
	if (firmware)
	{
		// read fw version
		new_version = *(uint32_t *)(firmware + 0x150);

		// check if update is newer or forced and <= 128KB
		if ((!force_update && (new_version < current_version)) || size > 0x20000)
		{
			if (size > 0x20000)
			{
				// EPRINTF("Firmware in SD too big!");
			}
			else
			{
				// EPRINTF("Firmware in SD not newer!");
			}

			f_close(&fp);
			goto out;
		}

		version_print = new_version < 0x100 ? (new_version << 8) : new_version;
		if (force_update)
			gfx_printf("Forced update to version %d.%d.%d.\nPress Power to update or VOL to exit..\n", version_print >> 16, (version_print >> 8) & 0xFF, version_print & 0xFF);
		else
			gfx_printf("New version %d.%d.%d found.\nPress Power to update or VOL to exit..\n", version_print >> 16, (version_print >> 8) & 0xFF, version_print & 0xFF);

		msleep(500);

		gfx_printf("\nEntering DFU mode..");
		// set HWFLY in DFU mode if not already
		if (hwfly_dfu_ping())
		{
			// set HWFLY in DFU mode
			modchip_hwfly_send_fwcmd_noack(FW_ENTER_DFU);

			if (hwfly_dfu_ping())
			{
				// EPRINTF("DFU doesn't respond..\n");
				goto out;
			}
		}

		// print info
		gfx_printf("\nFlashing FW...\n");

		// set FW flash address
		if (hwfly_dfu_set_flash_addr(DFU_FW_OFFSET)) {
			// EPRINTF("\nFailed to set flash address!\n");
			goto out;
		}

		uint8_t pct = 0;
		uint32_t prevPct = 200;
		uint32_t offset = 0;
		uint32_t remaining = size;

		// start flashing
		while (remaining)
		{
			// load and start xfer
			memset(&MMC_BUFFER[1], 0, DFU_XFER_BYTES);
			memcpy(&MMC_BUFFER[1], firmware + offset, MIN(remaining, DFU_XFER_BYTES));
			if (hwfly_dfu_xfer_start(DFU_XFER_BYTES, 10)) {
				// EPRINTFARGS("\nFailed to flash (@ %08X)!\n", 0x8000000 + DFU_FW_OFFSET + offset);
				break;
			}

			pct = ((uint32_t)offset * 100) / size;
			if (pct > 100)
				pct = 100;
			if (pct != prevPct)
			{
				// tui_pbar(0, gfx_con.y, pct, 0xFFFF9600, 0xFF551500);
				prevPct = pct;
			}

			offset += MIN(remaining, DFU_XFER_BYTES);
			remaining -= MIN(remaining, DFU_XFER_BYTES);
		}

		if (!remaining)
		{
			// tui_pbar(0, gfx_con.y, 100, 0xFFFF9600, 0xFF551500);
			gfx_printf("\n\nFlashed, now verifying..\n");

			// start verification
			remaining = size;
			offset = 0;
			uint8_t buffer[64];
			while (remaining)
			{
				// load and start xfer
				memset(&MMC_BUFFER[1], 0, DFU_XFER_BYTES);

				if (hwfly_dfu_read_flash(DFU_FW_OFFSET + offset, buffer))
				{
					// EPRINTFARGS("Verification failed at %08X: no response!\n", 0x8000000 + DFU_FW_OFFSET + offset);
					break;
				}

				if (memcmp(buffer, firmware + offset, MIN(remaining, DFU_XFER_BYTES)))
				{
					// EPRINTFARGS("Verification mismatch at %08X: firmware corrupt!\n", 0x8000000 + DFU_FW_OFFSET + offset);
					break;
				}

				pct = ((uint32_t)offset * 100) / size;
				if (pct > 100)
					pct = 100;
				if (pct != prevPct)
				{
					// tui_pbar(0, gfx_con.y, pct, 0xFF96FF00, 0xFF155500);
					prevPct = pct;
				}

				offset += MIN(remaining, DFU_XFER_BYTES);
				remaining -= MIN(remaining, DFU_XFER_BYTES);
			}

			if (!remaining)
			{
				// tui_pbar(0, gfx_con.y, 100, 0xFF96FF00, 0xFF155500);
				gfx_printf("\nVerified successfully\n");
				if (force_update)
					f_unlink(".force_update");
			}
		}

		free(firmware);
	}

out:
	sd_end();
	sdmmc_end(&emmc_sdmmc);
}
*/

uint32_t hwfly_reset_train_data()
{
	_init_mmc();
	uint32_t res = hwfly_train_data_reset();
	sdmmc_end(&emmc_sdmmc);
	return res;
}

uint32_t hwfly_session_info(uint8_t *fmt, session_info_t *si)
{
	_init_mmc();
	uint32_t res = hwfly_session_info_get(fmt, si);
	sdmmc_end(&emmc_sdmmc);
	return res;
}
