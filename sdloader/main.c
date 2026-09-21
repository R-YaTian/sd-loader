#include <libs/fatfs/ff.h>
#include <memory_map.h>
#include <gfx.h>
#include <soc/timer.h>
#include <storage/emmc.h>
#include <storage/mmc_def.h>
#include <storage/sd.h>
#include <storage/sdmmc.h>
#include <string.h>
#include <tui.h>
#include <utils/btn.h>
#include <utils/types.h>
#include <utils/util.h>
#include <soc/t210.h>
#include <soc/hw_init.h>
#include <mem/mc.h>
#include <mem/heap.h>
#include <display/di.h>
#include "logo.bmp.h"
#include <power/bq24193.h>
#include <storage/sdmmc_driver.h>
#include "modchip.h"
#include <soc/i2c.h>
#include <power/max77620.h>
#include "loader.h"
#include "files.h"
#include <soc/bpmp.h>

#if defined(ENABLE_TOOLBOX)
#include "modchip_toolbox.h"
#endif

typedef struct{
	void *addr;
	u32 size;
} payload_ctx_t;

typedef enum{
	SD_LOADER_OK = 0,
	SD_LOADER_INV_PAYLOAD_SZ,    // found payload, but too large
	SD_LOADER_NO_SD,             // no SD card found
	SD_LOADER_ERR_PAYLOAD,       // found payload, but read error
	SD_LOADER_FORCE_MENU,        // forced menu by button combo
	SD_LOADER_ERROR              // other error
} SD_LOADER_STATUS;

extern void pivot_stack(u32 stack_top);
extern void excp_reset(void);

static bool display_init_done = false;
static sd_loader_cfg_t sdloader_cfg;
static payload_ctx_t payload_ctx = {0};

static void deinit()
{
	unmount_drive();
	sd_end();
	emmc_end();
	hw_deinit(false);
}

static SD_LOADER_STATUS read_payload(FIL *f)
{
	FSIZE_t sz = f_size(f);
	FRESULT res;
	SD_LOADER_STATUS sd_res = SD_LOADER_OK;

	if (sz > PAYLOAD_SIZE_MAX)
	{
		return SD_LOADER_INV_PAYLOAD_SZ;
	}

	void *buf = (void*)PAYLOAD_BUF_ADDR;

	u32 br;
	res = f_read(f, (void*)buf, sz, &br);

	if (res != FR_OK || br != sz)
	{
		return SD_LOADER_ERR_PAYLOAD;
	}

	payload_ctx.addr = (void *)PAYLOAD_BUF_ADDR;
	payload_ctx.size = sz;

	return sd_res;
}

static void display_logo()
{
	u32 x_pos = (gfx_ctxt.height - logo_width) / 2;
	gfx_render_bmp_2bit_rot(logo_arr, logo_width, logo_height, x_pos, 25);
}

static void init_display()
{
	if (!display_init_done)
	{
		display_init();
		u8 *fb = (u8*)display_init_window_a_pitch_small_palette(logo_lut, sizeof(logo_lut) / 4);
		gfx_init_ctxt(fb, 180, 320, 192);
		gfx_con_init();
		gfx_con_set_origin_rot(0, 0);
		gfx_con_setpos_rot(0, 0);
		display_init_done = true;
		display_backlight_pwm_init();
		display_backlight_brightness(128, 1000);
		display_logo();
	}
}

static char msg_err[] = "Error xxxxing payload from SD!";

static void handle_sdloader_status(SD_LOADER_STATUS res)
{
	const char msg[] = "Payload too large!";
	const char msg_no_sd[] = "No SD card!";
	memcpy(msg_err + 6, "read", 4);

	u8 col = COL_ORANGE;

	const char *msg_to_display = NULL;
	switch(res)
	{
	case SD_LOADER_INV_PAYLOAD_SZ:
		msg_to_display = msg;
		break;
	case SD_LOADER_NO_SD:
		msg_to_display = msg_no_sd;
		break;
	case SD_LOADER_ERR_PAYLOAD:
		msg_to_display = msg_err;
		break;
	case SD_LOADER_FORCE_MENU:
		break;
	default:
		return;
	}

	init_display();
	tui_print_status(col, msg_to_display);
}

__attribute__((noreturn)) static void launch_payload()
{
	if (!is_t210())
	{
		*(u32*)0x7000EF08 = 0x34313254;   // "T214"
		*(u32*)0x7000EF0C = 0x3043444D;   // "MDC0"
		*(u32*)0x7000EF10 = 0x00000000;   // Null terminate
	}

	deinit();
	/* payloads (may) expect to be loaded at 0x40010000, relocate before jumping to payload */
	reloc_and_start_payload(payload_ctx.addr, payload_ctx.size);
	while(1)
	{
		bpmp_halt();
	}
}

static void handle_file_error(FRESULT res)
{
	const char msg[] = "No payload found!";
	const char *msg_to_display = NULL;
	memcpy(msg_err + 6, "open", 4);

	if (res == FR_NO_FILE)
	{
		msg_to_display = msg;
	}
	else if (res != FR_OK)
	{
		msg_to_display = msg_err;
	}
	else
	{
		return;
	}
	init_display();
	tui_print_status(COL_ORANGE, msg_to_display);
}

static SD_LOADER_STATUS load_payload()
{
	FIL f;
	FRESULT res;
	u8 drive = 0;

	// The alternative payload path
	const char *pathAlt = "bootloader/update.bin";

	// First, try the default payload.bin
	const char *path = "payload.bin";
	res = open_file_on(path, &f, drive);

	// if payload.bin not found, try the alternative path
	if (res != FR_OK)
	{
		res = open_file_on(pathAlt, &f, drive);
		if (res == FR_OK)
		{
			path = pathAlt;
		}
	}

	if (res != FR_OK)
	{
		handle_file_error(res);
		return SD_LOADER_ERROR;
	}

	SD_LOADER_STATUS sd_res = read_payload(&f);
	if (sd_res != SD_LOADER_OK)
	{
		handle_sdloader_status(sd_res);
	}
	f_close(&f);
	return sd_res;
}

static void power_off_cb(void *data)
{
	deinit();
	power_set_state(POWER_OFF);
}

static void ofw_cb(void *data)
{
#if defined(TARGET_HWFLY)
	modchip_hwfly_send_fwcmd_noack(FW_DEEP_SLEEP);
#endif
	deinit();
	power_set_state(REBOOT_BYPASS_FUSES);
}

static void try_launch_payload()
{
	if (!sdmmc_get_sd_inserted())
	{
		handle_sdloader_status(SD_LOADER_NO_SD);
		return;
	}

	SD_LOADER_STATUS res = SD_LOADER_ERROR;

	// TODO: probably should disable backlight if payload is big enough to overwrite frame buffer
	res = load_payload();

	if (res == SD_LOADER_OK)
	{
		launch_payload();
	}
}

static void retry_cb(void *data)
{
	try_launch_payload();
}

#if defined(ENABLE_TOOLBOX)
static void clear_screen_except_logo_and_status()
{
	gfx_clear_rect_rot(COL_BLACK, 0, TUI_MENUS_POS_Y, gfx_ctxt.height, gfx_ctxt.width - 80);
}

static void start_toolbox()
{
	gfx_con_setpos_rot(0, 0);
	clear_screen_except_logo_and_status();
	toolbox(0, TUI_MENUS_POS_Y, &sdloader_cfg);
}

static void toolbox_cb()
{
	start_toolbox();
}
#else
typedef struct {
	sd_loader_cfg_t *cfg;
	sd_loader_cfg_t *temp_cfg;
} save_settings_data_t;

static char def_act_str[22] = "Boot action: ";
static char ofw_btn_str[22] = "OFW   combo: ";

static void save_settings_cb(void *data)
{
	emmc_initialize(false);

	save_settings_data_t *save_settings_data = (save_settings_data_t*)data;

	tui_print_status(COL_TEAL, "Saving settings");

	u32 start = get_tmr_ms();

	bool res = modchip_set_cfg(save_settings_data->temp_cfg);

	if (get_tmr_ms() - start < 1000)
	{
		msleep(1000 - (get_tmr_ms() - start));
	}

	if (res)
	{
		tui_print_status(COL_TEAL, "Saved!");
		*(save_settings_data->cfg) = *(save_settings_data->temp_cfg);
	}
	else
	{
		tui_print_status(COL_ORANGE, "Failed to save!");
	}

	emmc_end();
}

static void default_action_update(sd_loader_cfg_t *vol_cfg)
{
	static const char* default_action_names[] = {
		[MODCHIP_DEFAULT_ACTION_OFW]     = "OFW    ",
		[MODCHIP_DEFAULT_ACTION_PAYLOAD] = "Payload",
		[MODCHIP_DEFAULT_ACTION_MENU]    = "Menu   ",
	};

	memset(def_act_str + 13, 0, sizeof(def_act_str) - 13);
	memcpy(def_act_str + 13, default_action_names[vol_cfg->default_action], 7);
}

static void default_action_cb(void *data)
{
	sd_loader_cfg_t *cfg = (sd_loader_cfg_t*)data;

	switch(cfg->default_action)
	{
	case MODCHIP_DEFAULT_ACTION_PAYLOAD:
		cfg->default_action = MODCHIP_DEFAULT_ACTION_OFW;
		break;
	case MODCHIP_DEFAULT_ACTION_OFW:
		cfg->default_action = MODCHIP_DEFAULT_ACTION_MENU;
		break;
	case MODCHIP_DEFAULT_ACTION_MENU:
		cfg->default_action = MODCHIP_DEFAULT_ACTION_PAYLOAD;
		break;
	}

	default_action_update(cfg);
}

static void ofw_btn_update(sd_loader_cfg_t *vol_cfg)
{
	const char *ofw_btn_status = vol_cfg->disable_ofw_btn_combo ? "Disabled" : "Enabled ";
	memset(ofw_btn_str + 13, 0, sizeof(ofw_btn_str) - 13);
	memcpy(ofw_btn_str + 13, ofw_btn_status, 8);
}

static void ofw_btn_cb(void *data)
{
	sd_loader_cfg_t *cfg = (sd_loader_cfg_t*)data;

	cfg->disable_ofw_btn_combo = !cfg->disable_ofw_btn_combo;

	ofw_btn_update(cfg);
}

static void ipl_settings_cb(void *data)
{
	sd_loader_cfg_t *cfg = (sd_loader_cfg_t*)data;
	sd_loader_cfg_t temp_cfg = *cfg;

	save_settings_data_t save_settings_data = {
		.temp_cfg = &temp_cfg,
		.cfg = cfg,
	};

	tui_entry_t menu_entries[] = {
		[0] = TUI_ENTRY_ACTION_NO_BLANK(def_act_str, default_action_cb, &temp_cfg, false, &menu_entries[1]),
		[1] = TUI_ENTRY_ACTION_NO_BLANK(ofw_btn_str, ofw_btn_cb, &temp_cfg, false, &menu_entries[2]),
		[2] = TUI_ENTRY_TEXT("", &menu_entries[3]),
		[3] = TUI_ENTRY_ACTION_NO_BLANK("Save", save_settings_cb, &save_settings_data, false, &menu_entries[4]),
		[4] = TUI_ENTRY_TEXT("", &menu_entries[5]),
		[5] = TUI_ENTRY_BACK(NULL),
	};

	tui_entry_menu_t settings_menu = {
		.entries    = menu_entries,
		.title      = {
			.text = NULL,
		},
		.pos_x      = (gfx_ctxt.height - 21 * 8) / 2,
		.pos_y      = TUI_MENUS_POS_Y,
		.pad        = 21,
		.height     = ARRAY_SIZE(menu_entries) + 2,
		.width      = 21,
		.colors     = &TUI_COLOR_SCHEME_DEFAULT,
		.timeout_ms = 0,
		.show_title = false,
	};

	default_action_update(cfg);
	ofw_btn_update(cfg);

	tui_clear_status();
	tui_menu_start_rot(&settings_menu);
	tui_clear_status();
}
#endif

static void do_menu()
{
	tui_entry_menu_t menu = {
		.colors = &TUI_COLOR_SCHEME_DEFAULT,
		.height = 6,
		.pad = 12,
		.width = 12,
		.pos_x = (gfx_ctxt.height - 12 * 8) / 2 - 1,
		.pos_y = TUI_MENUS_POS_Y,
		.title = {
			.text = NULL,
		},
		.timeout_ms = 60 * 1000, // return and power off if no action for 1 minute
	};

	tui_entry_t menu_entries[] = {
		[0] = TUI_ENTRY_ACTION_NO_BLANK("Load payload", retry_cb,     NULL, false, &menu_entries[1]),
		[1] = TUI_ENTRY_ACTION_NO_BLANK("Power    Off", power_off_cb, NULL, false, &menu_entries[2]),
		[2] = TUI_ENTRY_ACTION_NO_BLANK("Reboot   OFW", ofw_cb,       NULL, false, &menu_entries[3]),
#if !defined(ENABLE_TOOLBOX)
		[3] = TUI_ENTRY_ACTION_NO_BLANK("IPL Settings", ipl_settings_cb, &sdloader_cfg, false, NULL),
#else
		[3] = TUI_ENTRY_ACTION_NO_BLANK("Open toolbox", toolbox_cb,   NULL, false, NULL),
#endif
	};

	menu.entries = menu_entries;

	tui_menu_start_rot(&menu);
}

static void low_battery_shutdown()
{
	u8 intr = i2c_recv_byte(I2C_5, MAX77620_I2C_ADDR, MAX77620_REG_IRQTOP);

	if(intr & MAX77620_IRQ_TOP_GLBL)
	{
		/* battery too low */
		power_set_state(POWER_OFF);
	}
}

static void get_cfg()
{
	emmc_initialize(false);
	modchip_get_cfg_or_default(&sdloader_cfg);
	emmc_end();
}

void main()
{
	modchip_confirm_execution();
	low_battery_shutdown();

	bpmp_clk_rate_set(is_t210() ? BPMP_CLK_LOWER_BOOST : BPMP_CLK_DEFAULT_BOOST);

	get_cfg();

	u8 btn = btn_read_vol();

	if (btn & BTN_VOL_DOWN && btn & BTN_VOL_UP && !sdloader_cfg.disable_ofw_btn_combo)
	{
#if defined(TARGET_HWFLY)
		modchip_hwfly_send_fwcmd_noack(FW_DEEP_SLEEP);
#endif
		power_set_state(REBOOT_BYPASS_FUSES);
	}
	else if (btn & BTN_VOL_UP && !(btn & BTN_VOL_DOWN))
	{
		handle_sdloader_status(SD_LOADER_FORCE_MENU);
	}
	else
	{
#if defined(TARGET_HWFLY)
		modchip_hwfly_send_fwcmd_noack(FW_DEEP_SLEEP);
#endif
#if !defined(ENABLE_TOOLBOX)
		if (sdloader_cfg.default_action == MODCHIP_DEFAULT_ACTION_PAYLOAD)
		{
			try_launch_payload();
		}
		else if (sdloader_cfg.default_action == MODCHIP_DEFAULT_ACTION_OFW)
		{
			power_set_state(REBOOT_BYPASS_FUSES);
		}
		else
#endif
		{
			init_display();
		}
	}

	bq24193_enable_charger();

	do_menu();

	gfx_clear_color(COL_BLACK);

	deinit();
}
