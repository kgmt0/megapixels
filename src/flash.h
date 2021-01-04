typedef struct _MPFlash MPFlash;

MPFlash *mp_led_flash_from_path(const char *path);
MPFlash *mp_create_display_flash();
void mp_flash_free(MPFlash *flash);

void mp_flash_enable(MPFlash *flash);
void mp_flash_disable(MPFlash *flash);
