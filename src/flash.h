#include "gio/gio.h"

typedef struct _MPFlash MPFlash;

void mp_flash_gtk_init(GDBusConnection *conn);
void mp_flash_gtk_clean();

MPFlash *mp_led_flash_from_path(const char *path);
MPFlash *mp_create_display_flash();
void mp_flash_free(MPFlash *flash);

void mp_flash_enable(MPFlash *flash);
void mp_flash_disable(MPFlash *flash);
