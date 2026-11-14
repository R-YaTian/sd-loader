**Modchip SDLoader payload**

**Features:**
- Works even with defective RAM
- Supports Booting from a FAT32/EXFAT partition on SD card
- Can launch payloads up to size of 171KB
- Integrated toolbox to update modchip firmware, update sdloader, rollback firmware
- Persistent configuration of the boot action (boot to menu by default, or boot payload by default), the button combination to boot Stock (enable or disable)

**How to use:**
1) Put sdloader.enc on root of SD card
2) Boot sdloader.bin (e.g. by loading it via hekate) while holding VOL+ button
3) Using Vol+/-, In the menu go to More -> Toolbox -> Update IPL, hit POWER to confirm
4) Done.

Hold VOL+ while booting to open the sdloader menu, hold VOL+ and VOL- while booting to boot Stock.
The button combination to boot stock can be disabled in the menu.
The payload is loaded from `payload.bin` or `bootloader/update.bin` in the root directory of the FAT32/EXFAT partition on SD card.
