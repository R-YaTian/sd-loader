#include "modchip.h"
#include "memory_map.h"
#include <libs/fatfs/ff.h>
#include <soc/timer.h>
#include <storage/emmc.h>
#include <storage/sdmmc.h>
#include <storage/mmc_def.h>
#include <storage/sdmmc_driver.h>
#include <string.h>
#include <libs/fatfs/diskio.h>

#define MODCHIP_MAGIC 0xAA5458BA
#define MODCHIP_RECV  0xAA5458BB

static sd_loader_cfg_t default_cfg = {
	.magic1 = MODCHIP_MAGIC,
	.magic2 = MODCHIP_MAGIC,
	.default_action = MODCHIP_DEFAULT_ACTION_PAYLOAD,
	.disable_ofw_btn_combo = false,
};

static void _init_mmc(){
	u32 start = get_tmr_us();
	sdmmc_init(&emmc_sdmmc, SDMMC_4, SDMMC_POWER_1_8, SDMMC_BUS_WIDTH_1, SDHCI_TIMING_MMC_ID);
	u32 now = get_tmr_us();
	if(now - start < 40000){
		usleep(start + 40000 - now);
	}
}

#if !defined(TARGET_HWFLY)
void modchip_confirm_execution()
{
	// modchip waits for IDLE_CMD with magic argument to confirm the payload is running
	_init_mmc();
	sdmmc_cmd_t cmdbuf;
	sdmmc_init_cmd(&cmdbuf, MMC_GO_IDLE_STATE, MODCHIP_MAGIC, SDMMC_RSP_TYPE_0, 0);
	sdmmc_execute_cmd(&emmc_sdmmc, &cmdbuf, NULL, NULL);
	sdmmc_end(&emmc_sdmmc);
}
#else
static void modchip_hwfly_send(u8 *buf)
{
	// emmc_sdmmc must be initialized at this point with SDMMC_BUS_WIDTH_1 and SDHCI_TIMING_MMC_ID
	// not supported by picofly fw
	sdmmc_cmd_t cmdbuf;
	sdmmc_req_t req;
	sdmmc_init_cmd(&cmdbuf, MMC_GO_IDLE_STATE, MODCHIP_MAGIC, SDMMC_RSP_TYPE_1, 0);

	req.blksize = MODCHIP_SECTOR_SIZE;
	req.num_sectors = 1;
	req.is_write = 1;
	req.is_multi_block = 0;
	req.is_auto_stop_trn = 0;
	req.buf = buf;

	sdmmc_execute_cmd(&emmc_sdmmc, &cmdbuf, &req, NULL);
}

static void modchip_hwfly_recv(u8 *buf)
{
	sdmmc_cmd_t cmdbuf;
	sdmmc_req_t reqbuf;
	sdmmc_init_cmd(&cmdbuf, MMC_GO_IDLE_STATE, MODCHIP_RECV, SDMMC_RSP_TYPE_1, 0);

	reqbuf.buf = buf;
	reqbuf.blksize = MODCHIP_SECTOR_SIZE;
	reqbuf.num_sectors = 1;
	reqbuf.is_write = 0;
	reqbuf.is_multi_block = 0;
	reqbuf.is_auto_stop_trn = 0;

	sdmmc_execute_cmd(&emmc_sdmmc, &cmdbuf, &reqbuf, NULL);
}

void modchip_confirm_execution()
{
	_init_mmc();
	u8 *modchip_buf = (u8*)SDMMC_UPPER_BUFFER;
	memset(modchip_buf, 0, MODCHIP_SECTOR_SIZE);
	modchip_buf[0] = FW_GET_VER;
    modchip_hwfly_send(modchip_buf);
	do
	{
		msleep(10);
		modchip_hwfly_recv(modchip_buf);
	}
	while (modchip_buf[0] != (u8) ~FW_GET_VER);
	sdmmc_end(&emmc_sdmmc);
}

void modchip_hwfly_send_fwcmd_noack(u8 cmd)
{
	_init_mmc();
	u8 *modchip_buf = (u8*)SDMMC_UPPER_BUFFER;
	memset(modchip_buf, 0, MODCHIP_SECTOR_SIZE);
	modchip_buf[0] = cmd;
	modchip_hwfly_send(modchip_buf);
	sdmmc_end(&emmc_sdmmc);
}
#endif

bool modchip_get_cfg(sd_loader_cfg_t *cfg){
	u8 *buf = (u8*)SDMMC_UPPER_BUFFER;
	DRESULT res = disk_read(DEV_BOOT0, buf, MODCHIP_CFG_SECTOR, 1);
	if(res == RES_OK){
		memcpy(cfg, buf + MODCHIP_CFG_OFFSET, sizeof(*cfg));
		return true;
	}

	return false;
}

void modchip_get_cfg_or_default(sd_loader_cfg_t *cfg){
	if(!(modchip_get_cfg(cfg) && modchip_is_cfg_valid(cfg))){
		memcpy(cfg, &default_cfg, sizeof(*cfg));
	}
}

bool modchip_set_cfg(sd_loader_cfg_t *cfg){
	u8 *buf = (u8*)SDMMC_UPPER_BUFFER;
	if(disk_read(DEV_BOOT0, buf, MODCHIP_CFG_SECTOR, 1) != RES_OK){
		return false;
	}
	memcpy(buf + MODCHIP_CFG_OFFSET, cfg, sizeof(*cfg));
	return disk_write(DEV_BOOT0, buf, MODCHIP_CFG_SECTOR, 1) == RES_OK;
}

bool modchip_is_cfg_valid(sd_loader_cfg_t *cfg){
	if(cfg->magic1 != MODCHIP_MAGIC || cfg->magic2 != MODCHIP_MAGIC){
		return false;
	}
	return true;
}

static bool modchip_write_cmd(modchip_cmd_t *cmd)
{
	u8 *buf = (u8 *)SDMMC_UPPER_BUFFER;

	if (disk_read(DEV_BOOT0, buf, MODCHIP_CMD_SECTOR, 1) != RES_OK)
	{
		return false;
	}

	memset(buf, 0, MODCHIP_CMD_SIZE);
	memcpy(buf, cmd, sizeof(modchip_cmd_t));

	return disk_write(DEV_BOOT0, buf, MODCHIP_CMD_SECTOR, 1) == RES_OK;
}

// buf must be multiple of 512
bool modchip_write_fw_update(const u8 *buf, u32 size){
	u32 sec_cnt = (size + MODCHIP_SECTOR_SIZE - 1) / MODCHIP_SECTOR_SIZE;

	// modchip will overwrite our config and fw descriptor when applying update/resetting
	DRESULT res = disk_write(DEV_BOOT0, buf, MODCHIP_FW_START_SECTOR, sec_cnt);

	if (res == RES_OK)
	{
		// fw image written, now issue fw update command. if this fails, not much we can do
		return modchip_write_fw_update_cmd(MODCHIP_FW_START_SECTOR, sec_cnt);
	}
	else
	{
		// fw image write failed, issue reset command so modchip rewrites sdloader atleast, if this fails too, not much we can do
		modchip_write_rst_cmd();
		return false;
	}
}

bool modchip_write_fw_update_from_file(FIL *f)
{
	FRESULT f_res;
	u32 size = f_size(f);
	u32 sectors = (size + MODCHIP_SECTOR_SIZE - 1) / MODCHIP_SECTOR_SIZE;
	u32 aligned_size = sectors * MODCHIP_SECTOR_SIZE;

	u8 *buf = (u8*)SDMMC_UPPER_BUFFER;
	u32 br;

	// we can read entire fw update into memory first
	if (aligned_size <= SDMMC_UP_BUF_SZ)
	{
		memset(buf + (size & ~(MODCHIP_SECTOR_SIZE - 1)), 0xFF, MODCHIP_SECTOR_SIZE);
		f_res = f_read(f, buf, size, &br);
		if(f_res != FR_OK || br != size)
		{
			return false;
		}

		return modchip_write_fw_update(buf, size);
	}

	return false;
}

// buf must be multiple of 512
bool modchip_write_ipl_update(const u8 *buf, u32 size)
{
	u32 sec_cnt = (size + MODCHIP_SECTOR_SIZE - 1) / MODCHIP_SECTOR_SIZE;
	if (disk_write(DEV_BOOT0, buf, MODCHIP_BL_START_SECTOR, sec_cnt) != RES_OK)
	{
		modchip_write_rst_cmd();
		return false;
	}
	return true;
}

bool modchip_write_ipl_update_from_file(FIL *f)
{
	u32 size = f_size(f);
	u32 sectors = (size + MODCHIP_SECTOR_SIZE - 1) / MODCHIP_SECTOR_SIZE;
	u32 aligned_size = sectors * MODCHIP_SECTOR_SIZE;

	if (aligned_size > MODCHIP_BL_MAX_SIZE)
	{
		return false;
	}

	u8 *buf = (u8*)SDMMC_UPPER_BUFFER;
	u32 br;

	memset(buf + (size & ~(MODCHIP_SECTOR_SIZE - 1)), 0xFF, MODCHIP_SECTOR_SIZE);
	FRESULT res = f_read(f, buf, size, &br);

	if (res != FR_OK || br != size)
	{
		return false;
	}

	return modchip_write_ipl_update(buf, size);
}

bool modchip_write_rst_cmd()
{
	modchip_cmd_t cmd = {0};
	cmd.cmd = MODCHIP_CMD_RST;
	return modchip_write_cmd(&cmd);
}

bool modchip_write_fw_update_cmd(u32 sector_start, u32 sector_cnt)
{
	modchip_cmd_t cmd = {0};
	cmd.cmd = MODCHIP_CMD_FW_UPDATE;
	cmd.fw_update_info.fw_sector_cnt   = sector_cnt;
	cmd.fw_update_info.fw_sector_start = sector_start;
	return modchip_write_cmd(&cmd);
}

bool modchip_write_rollback_cmd()
{
	return modchip_write_fw_update_cmd(0xffffffff, 0xffffffff);
}

bool modchip_read_desc(modchip_desc_t *desc)
{
	u8 *buf = (u8 *)SDMMC_UPPER_BUFFER;

	if (disk_read(DEV_BOOT0, buf, MODCHIP_DESC_SECTOR, 1) != RES_OK)
	{
		return false;
	}

	memcpy(desc, buf + MODCHIP_DESC_OFFSET, sizeof(modchip_desc_t));
	return true;
}

bool modchip_is_desc_valid(modchip_desc_t *desc)
{
	return desc->signature == MODCHIP_DESC_SIGNATURE;
}
