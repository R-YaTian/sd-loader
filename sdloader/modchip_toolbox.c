#include "files.h"
#include "modchip.h"
#include <libs/fatfs/ff.h>
#include <soc/timer.h>
#include <storage/emmc.h>
#include <string.h>
#include <tui.h>
#include <gfx.h>
#include <utils/sprintf.h>
#include <utils/types.h>
#include <utils/util.h>

#if defined(TARGET_HWFLY)
#include "hwfly.h"
#endif

static bool command_pending = false;

typedef enum {
	TOOLBOX_INVALID_UPDATE_SIZE,
} toolbox_status;

typedef struct {
	tui_entry_menu_t *menu;
	void *data;
} confirm_menu_data_t;

typedef struct {
	FIL *f;
	const char *path;
} update_file_info;

typedef struct {
	sd_loader_cfg_t *cfg;
	sd_loader_cfg_t *temp_cfg;
} save_settings_data_t;

static void handle_file_error(const char *path, FRESULT res)
{
	char msg[48];
	if (res == FR_NO_FILE)
	{
		s_printf(&msg[0], "No %s found!", path);
	}
	else if(res != FR_OK)
	{
		s_printf(&msg[0], "Error opening %s from SD!", path);
	}
	else
	{
		return;
	}
	tui_print_status(COL_ORANGE, msg);
}

static void handle_toolbox_error(toolbox_status res, void *extra_info)
{
	char msg[48];
	if (res == TOOLBOX_INVALID_UPDATE_SIZE)
	{
		update_file_info *update_info = (update_file_info*)extra_info;
		s_printf(&msg[0], "%s on SD too large!", update_info->path);
	}
	else
	{
		return;
	}
	tui_print_status(COL_ORANGE, msg);
}

void info_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
#if !defined(TARGET_HWFLY)
	char sig_str[25]      = "Signature : [Invalid]";
	char ver_str[25]      = "FW Version: --";
	char fw_hash_str[25]  = "FW Hash   : --";
	char ipl_hash_str[25] = "IPL Hash  : --";
	char fuse_str[25]     = "Fuse Count: --";

	modchip_desc_t desc = {0};
	if (!modchip_read_desc(&desc))
	{
		tui_print_status(COL_ORANGE, "Failed to read FW info!");
		return;
	}

	menu->colors = &TUI_COLOR_SCHEME_SHADOW;
	tui_print_menu(menu);

	if (modchip_is_desc_valid(&desc))
	{
		s_printf(&sig_str[0],  "Signature : 0x%08x", desc.signature);
	}
	s_printf(&ver_str[0],      "FW Version: %d.%d", desc.fw_major > 9 ? 9 : desc.fw_major, desc.fw_minor > 99 ? 99 : desc.fw_minor);
	s_printf(&fw_hash_str[0],  "FW Hash   : 0x%08x", desc.firmware_hash);
	s_printf(&ipl_hash_str[0], "IPL Hash  : 0x%08x", desc.sdloader_hash);
	s_printf(&fuse_str[0],     "Fuse Count: %d", desc.fuse_cnt > 999 ? 999 : desc.fuse_cnt);

	tui_entry_t menu_entries[] = {
		[0] = TUI_ENTRY_TEXT_DISABLED(sig_str,      &menu_entries[1]),
		[1] = TUI_ENTRY_TEXT_DISABLED(ver_str,      &menu_entries[2]),
		[2] = TUI_ENTRY_TEXT_DISABLED(fuse_str,     &menu_entries[3]),
		[3] = TUI_ENTRY_TEXT_DISABLED(fw_hash_str,  &menu_entries[4]),
		[4] = TUI_ENTRY_TEXT_DISABLED(ipl_hash_str, &menu_entries[5]),
		[5] = TUI_ENTRY_TEXT("\n", &menu_entries[6]),
		[6] = TUI_ENTRY_BACK(NULL),
	};
#else
	char board_str[25]    = "Board  Type: --";
	char ver_str[25]      = "FW  Version: --";
	char device_str[25]   = "Device Type: --";
	char used_str[25]     = "Glitch used: --";
	char loader_str[25]   = "SD   Loader: --";

	uint32_t fw_version;
	if (hwfly_fw_version(&fw_version) != 0)
	{
		tui_print_status(COL_ORANGE, "Failed to read FW version!");
		return;
	}

	u8 fmt;
	session_info_t si;
	if (!hwfly_session_info(&fmt, &si) && fmt == SESSION_INFO_FORMAT_VER)
	{
		const char* board_type;
		if (si.fpga_type == SESSION_INFO_HWFLY_ID && si.board_id == BOARD_ID_CORE) 
			board_type = "HWFLY CORE";
		else if (si.fpga_type == SESSION_INFO_HWFLY_ID && si.board_id == BOARD_ID_LITE) 
			board_type = "HWFLY OLED";
		else if (si.board_id == BOARD_ID_CORE) 
			board_type = "SX-CORE";
		else if (si.board_id == BOARD_ID_LITE) 
			board_type = "SX-LITE";
		else 
			board_type = "Unknown";

		const char* device_type;
		if (si.device_type == DEVICE_TYPE_ERISTA)
			device_type = "Erista";
		else if (si.device_type == DEVICE_TYPE_MARIKO)
			device_type = "Mariko";
		else if (si.fpga_type == SESSION_INFO_HWFLY_ID && si.board_id == BOARD_ID_LITE && si.device_type == DEVICE_TYPE_LITE)
			device_type = "OLED";
		else if (si.device_type == DEVICE_TYPE_LITE)
			device_type = "Lite";
		else
			device_type = "Unknown";

		u32 version_print = fw_version < 0x100 ? (fw_version << 8) : fw_version;
		s_printf(&ver_str[0],    "FW  Version: %d.%d.%d", (version_print >> 16) & 0xFF, (version_print >> 8) & 0xFF, version_print & 0xFF);
		s_printf(&board_str[0],  "Board  Type: %s", board_type);
		s_printf(&device_str[0], "Device Type: %s", device_type);
		s_printf(&used_str[0],   "Glitch used: %d", si.glitch_attempt);
		s_printf(&loader_str[0], "SD   Loader: %s", si.payload_flashed ? "Flashed" : "Not flash");
	}
	else
	{
		tui_print_status(COL_ORANGE, "Failed to get session info!");
		return;
	}

	menu->colors = &TUI_COLOR_SCHEME_SHADOW;
	tui_print_menu(menu);

	tui_entry_t menu_entries[] = {
		[0] = TUI_ENTRY_TEXT_DISABLED(ver_str,      &menu_entries[1]),
		[1] = TUI_ENTRY_TEXT_DISABLED(board_str,    &menu_entries[2]),
		[2] = TUI_ENTRY_TEXT_DISABLED(device_str,   &menu_entries[3]),
		[3] = TUI_ENTRY_TEXT_DISABLED(used_str,     &menu_entries[4]),
		[4] = TUI_ENTRY_TEXT_DISABLED(loader_str,   &menu_entries[5]),
		[5] = TUI_ENTRY_TEXT("\n", &menu_entries[6]),
		[6] = TUI_ENTRY_BACK(NULL),
	};
#endif

	tui_entry_menu_t info_menu = {
		.entries    = menu_entries,
		.title      = {
			.text = "FW Info"
		},
		.pos_x      = menu->pos_x + (menu->width * 8 + 8),
		.pos_y      = menu->pos_y,
		.pad        = 22,
		.height     = ARRAY_SIZE(menu_entries) + 2,
		.width      = 23,
		.colors     = &TUI_COLOR_SCHEME_DEFAULT,
		.timeout_ms = 0,
		.show_title = true,
	};

	tui_menu_start_rot(&info_menu);

	menu->colors = &TUI_COLOR_SCHEME_DEFAULT;
}

static void confirm_menu(const char *title, tui_action_modifying_cb_t cb, tui_entry_menu_t *menu, void *data)
{
	menu->colors = &TUI_COLOR_SCHEME_SHADOW;
	tui_print_menu(menu);

	confirm_menu_data_t confirm_data = {
		.menu = menu,
		.data = data,
	};

	tui_entry_t menu_entries[] = {
		[0] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("Confirm", cb, &confirm_data, false, &menu_entries[1]),
		[1] = TUI_ENTRY_TEXT("\n\n\n\n\n", &menu_entries[2]),
		[2] = TUI_ENTRY_BACK(NULL),
	};

	u8 pad = strlen(title);
	pad = pad < 4 ? 4 : pad;

	tui_entry_menu_t confirm_menu = {
		.entries    = menu_entries,
		.title      = {
			.text = title
		},
		.pos_x      = menu->pos_x + (menu->width * 8 + 8),
		.pos_y      = menu->pos_y,
		.pad        = pad,
		.height     = ARRAY_SIZE(menu_entries) + 7,
		.width      = pad,
		.colors     = &TUI_COLOR_SCHEME_DEFAULT,
		.timeout_ms = 0,
		.show_title = true,
	};

	tui_menu_start_rot(&confirm_menu);

	menu->colors = &TUI_COLOR_SCHEME_DEFAULT;
}

static void print_cmd_fail()
{
	tui_print_status(COL_ORANGE, "Failed to send command!");
}

static void disable_all_cmds(tui_entry_menu_t *menu)
{
#if defined(TARGET_HWFLY)
	menu->entries[0].disabled = true; // Disable FW Info
#endif
	menu->entries[1].disabled = true;
	menu->entries[2].disabled = true;
	menu->entries[3].disabled = true;
	menu->entries[4].disabled = true;
}

static void reset(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	tui_entry_menu_t *top_menu = ((confirm_menu_data_t*)data)->menu;

	u32 start = get_tmr_ms();
	tui_print_status(COL_TEAL, "Writing reset command...");

#if defined(TARGET_HWFLY)
	bool res = (hwfly_reset_train_data() == 0);
#else
	bool res = modchip_write_rst_cmd();
#endif

	if (get_tmr_ms() - start < 1000)
	{
		msleep(1000 - (get_tmr_ms() - start));
	}

	if (!res)
	{
		print_cmd_fail();
	}
	else
	{
		tui_print_status(COL_TEAL, "Reset command sent. Reboot Console!");
		command_pending = true;
		disable_all_cmds(top_menu);
		tui_print_menu(top_menu);
		entry->disabled = true;
	}
}

#if defined(TARGET_HWFLY)
static void deepsleep(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	tui_entry_menu_t *top_menu = ((confirm_menu_data_t*)data)->menu;

	u32 start = get_tmr_ms();
	tui_print_status(COL_TEAL, "Sending Deep Sleep command...");

	modchip_hwfly_send_fwcmd_noack(FW_DEEP_SLEEP);

	if (get_tmr_ms() - start < 1000)
	{
		msleep(1000 - (get_tmr_ms() - start));
	}

	tui_print_status(COL_TEAL, "Modchip is now in Deep Sleep!");
	command_pending = true;
	disable_all_cmds(top_menu);
	tui_print_menu(top_menu);
	entry->disabled = true;
}
#else
static void rollback(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	tui_entry_menu_t *top_menu = ((confirm_menu_data_t*)data)->menu;

	u32 start = get_tmr_ms();
	tui_print_status(COL_TEAL, "Writing rollback command...");

	bool res = modchip_write_rollback_cmd();

	if (get_tmr_ms() - start < 1000)
	{
		msleep(1000 - (get_tmr_ms() - start));
	}

	if (!res)
	{
		print_cmd_fail();
	}
	else
	{
		tui_print_status(COL_TEAL, "Rollback command sent. Reboot console!");
		command_pending = true;
		disable_all_cmds(top_menu);
		tui_print_menu(top_menu);
		entry->disabled = true;
	}
}
#endif

static void reset_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	confirm_menu("Reset Train Data", reset, menu, NULL);
}

static void update_fw(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	confirm_menu_data_t *confirm_data = (confirm_menu_data_t*)data;
	tui_entry_menu_t *top_menu = confirm_data->menu;
	update_file_info *update_info = (update_file_info*)confirm_data->data;

	u32 start = get_tmr_ms();
	tui_print_status(COL_TEAL, "Writing FW Update...");

#if defined(TARGET_HWFLY)
	int res = hwfly_update_fw_from_file(update_info->f);
#else
	int res = modchip_write_fw_update_from_file(update_info->f);
#endif

	if (get_tmr_ms() - start < 1000)
	{
		msleep(1000 - (get_tmr_ms() - start));
	}

	if (res != true)
	{
		switch (res)
		{
			case -1:
				tui_print_status(COL_ORANGE, "Failed to read update file!");
				break;
			case -2:
				tui_print_status(COL_ORANGE, "Not enough memory to read update!");
				break;
			case -3:
				tui_print_status(COL_ORANGE, "Failed to get current FW version!");
				break;
			case -4:
				tui_print_status(COL_ORANGE, "Firmware in SD not newer!");
				break;
			case -5:
				tui_print_status(COL_ORANGE, "DFU doesn't respond!");
				break;
			case -6:
				tui_print_status(COL_ORANGE, "Failed to set flash address!");
				break;
			default:
				tui_print_status(COL_ORANGE, "FW update fail. Reboot console!");
				break;
		}
	}
	else
	{
		tui_print_status(COL_TEAL, "FW update command sent. Reboot console!");
	}
	command_pending = true;
	disable_all_cmds(top_menu);
	tui_print_menu(top_menu);
	entry->disabled = true;
}

static void update_ipl(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	confirm_menu_data_t *confirm_data = (confirm_menu_data_t*)data;
	tui_entry_menu_t *top_menu = confirm_data->menu;
	update_file_info *update_info = (update_file_info*)confirm_data->data;

	u32 start = get_tmr_ms();
	tui_print_status(COL_TEAL, "Writing IPL Update...");

	bool res = modchip_write_ipl_update_from_file(update_info->f);

	if (get_tmr_ms() - start < 1000)
	{
		msleep(1000 - (get_tmr_ms() - start));
	}

	if (!res)
	{
		tui_print_status(COL_ORANGE, "IPL update fail. Reboot console!");
	}
	else
	{
		tui_print_status(COL_TEAL, "IPL update command sent. Reboot console!");
	}
	command_pending = true;
	disable_all_cmds(top_menu);
	tui_print_menu(top_menu);
	entry->disabled = true;
}

static void update_cb(const char *path, u32 size_max, tui_action_modifying_cb_t cb, tui_entry_menu_t *menu)
{
	u8 drive = 0;
	FIL f;
	FRESULT res;

	res = open_file_on(path, &f, drive);

	if (res != FR_OK)
	{
		handle_file_error(path, res);
		return;
	}

	u32 size = f_size(&f);

	update_file_info update_info = {
		.f = &f,
		.path = path,
	};

	if (size > size_max)
	{
		handle_toolbox_error(TOOLBOX_INVALID_UPDATE_SIZE, &update_info);
		goto out;
	}

	char title[] = "Apply update from SD";

	confirm_menu(title, cb, menu, &update_info);

out:
	f_close(&f);
}

static void fw_update_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
#if defined(TARGET_HWFLY)
	const char *path = "firmware.bin";
#else
	const char *path = "update.bin";
#endif
	update_cb(path, MODCHIP_FW_MAX_SIZE, update_fw, menu);
}

#if defined(TARGET_HWFLY)
static void deep_sleep_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	confirm_menu("Enter deep sleep?", deepsleep, menu, NULL);
}
#else
static void fw_rollback_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	confirm_menu("FW Rollback", rollback, menu, NULL);
}
#endif

static void ipl_update_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	const char *path = "sdloader.enc";
	update_cb(path, MODCHIP_BL_MAX_SIZE, update_ipl, menu);
}

static void save_settings_cb(void *data)
{
	save_settings_data_t *save_settings_data = (save_settings_data_t*)data;

	tui_print_status(COL_TEAL, "Saving settings...");

	u32 start = get_tmr_ms();

	bool res = modchip_set_cfg(save_settings_data->temp_cfg);

	if (get_tmr_ms() - start < 1000)
	{
		msleep(1000 - (get_tmr_ms() - start));
	}

	if (res)
	{
		tui_print_status(COL_TEAL, "Settings saved!");
		*(save_settings_data->cfg) = *(save_settings_data->temp_cfg);
	}
	else
	{
		tui_print_status(COL_ORANGE, "Failed to save settings!");
	}
}

static void default_action_update(sd_loader_cfg_t *vol_cfg, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	static const char* default_action_names[] = {
		[MODCHIP_DEFAULT_ACTION_OFW]     = "OFW",
		[MODCHIP_DEFAULT_ACTION_PAYLOAD] = "Payload",
		[MODCHIP_DEFAULT_ACTION_MENU]    = "Menu",
	};

	s_printf((char*)entry->title.text, "Boot action  %s", default_action_names[vol_cfg->default_action]);
}

static void default_action_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
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

	default_action_update(cfg, entry, menu);
}

static void ofw_btn_update(sd_loader_cfg_t *vol_cfg, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	s_printf((char*)entry->title.text, "OFW combo    %s", vol_cfg->disable_ofw_btn_combo ? "Disabled" : "Enabled");
}

static void ofw_btn_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	sd_loader_cfg_t *cfg = (sd_loader_cfg_t*)data;

	cfg->disable_ofw_btn_combo = !cfg->disable_ofw_btn_combo;

	ofw_btn_update(cfg, entry, menu);
}

static void ipl_settings_cb(void *data, tui_entry_t *entry, tui_entry_menu_t *menu)
{
	sd_loader_cfg_t *cfg = (sd_loader_cfg_t*)data;
	sd_loader_cfg_t temp_cfg = *cfg;

	menu->colors = &TUI_COLOR_SCHEME_SHADOW;
	tui_print_menu(menu);

	save_settings_data_t save_settings_data = {
		.temp_cfg = &temp_cfg,
		.cfg = cfg,
	};

	char default_action_str[30] = "";
	char ofw_btn_str[30] = "";

	tui_entry_t menu_entries[] = {
		[0] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK(default_action_str, default_action_cb, &temp_cfg, false, &menu_entries[1]),
		[1] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK(ofw_btn_str, ofw_btn_cb, &temp_cfg, false, &menu_entries[2]),
		[2] = TUI_ENTRY_TEXT("", &menu_entries[3]),
		[3] = TUI_ENTRY_ACTION_NO_BLANK("Save", save_settings_cb, &save_settings_data, false, &menu_entries[4]),
		[4] = TUI_ENTRY_TEXT("\n", &menu_entries[5]),
		[5] = TUI_ENTRY_BACK(NULL),
	};

	tui_entry_menu_t settings_menu = {
		.entries    = menu_entries,
		.title      = {
			.text = "IPL Settings"
		},
		.pos_x      = menu->pos_x + (menu->width * 8 + 8),
		.pos_y      = menu->pos_y,
		.pad        = 25,
		.height     = ARRAY_SIZE(menu_entries) + 2,
		.width      = 25,
		.colors     = &TUI_COLOR_SCHEME_DEFAULT,
		.timeout_ms = 0,
		.show_title = true,
	};

	default_action_update(cfg, &menu_entries[0], &settings_menu);
	ofw_btn_update(cfg, &menu_entries[1], &settings_menu);

	tui_menu_start_rot(&settings_menu);

	menu->colors = &TUI_COLOR_SCHEME_DEFAULT;
}

void toolbox(u32 x, u32 y, sd_loader_cfg_t *cfg)
{
	emmc_initialize(false);

	tui_entry_t menu_entries[] = {
		[0] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("FW  Info", info_cb, NULL, false, &menu_entries[1]),
		[1] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("FW  Update", fw_update_cb, NULL, command_pending, &menu_entries[2]),
#if !defined(TARGET_HWFLY)
		[2] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("FW  Rollback", fw_rollback_cb, NULL, command_pending, &menu_entries[3]),
#else
		[2] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("Deep Sleep", deep_sleep_cb, NULL, command_pending, &menu_entries[3]),
#endif
		[3] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("Train  Reset", reset_cb, NULL, command_pending, &menu_entries[4]),
		[4] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("IPL Update", ipl_update_cb, NULL, command_pending, &menu_entries[5]),
		[5] = TUI_ENTRY_ACTION_MODIFYING_NO_BLANK("IPL Settings", ipl_settings_cb, cfg, false, &menu_entries[6]),
		[6] = TUI_ENTRY_BACK(NULL)
	};

	tui_entry_menu_t menu = {
		.entries    = menu_entries,
		.title      = {
			.text = "Toolbox"
		},
		.pos_x      = x,
		.pos_y      = y,
		.pad        = 12,
		.height     = ARRAY_SIZE(menu_entries) + 2,
		.width      = 12,
		.colors     = &TUI_COLOR_SCHEME_DEFAULT,
		.timeout_ms = 0,
		.show_title = true,
	};

	tui_clear_status();
	if (is_t210())
	{
		menu.entries[4].disabled = true;
	}
	tui_menu_start_rot(&menu);

	emmc_end();
}
