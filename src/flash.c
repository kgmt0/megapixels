#include "flash.h"

#include "gtk/gtk.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>

typedef enum {
	FLASH_TYPE_LED,
	FLASH_TYPE_DISPLAY,
} FlashType;

typedef struct {
	char path[260];
	int fd;
} MPLEDFlash;

typedef struct {
	GtkWidget *window;
} MPDisplayFlash;

struct _MPFlash {
	FlashType type;

	union {
		MPLEDFlash led;
		MPDisplayFlash display;
	};
};

MPFlash *
mp_led_flash_from_path(const char * path)
{
	MPFlash *flash = malloc(sizeof(MPFlash));
	flash->type = FLASH_TYPE_LED;

	strncpy(flash->led.path, path, 259);

	char mpath[275];
	snprintf(mpath, 275, "%s/flash_strobe", path);
	flash->led.fd = open(mpath, O_WRONLY);
	if (flash->led.fd == -1) {
		g_printerr("Failed to open %s\n", mpath);
		free(flash);
		return NULL;
	}

	return flash;
}

static bool create_display(MPFlash *flash)
{
	// Create a full screen full white window as a flash
	GtkWidget *window = gtk_window_new();
	// gtk_window_set_accept_focus(GTK_WINDOW(flash->display.window), FALSE);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_fullscreen(GTK_WINDOW(window));

	GtkStyleContext *context;
	context = gtk_widget_get_style_context(window);
	gtk_style_context_add_class(context, "flash");

	flash->display.window = window;

	return false;
}

MPFlash *
mp_create_display_flash()
{
	MPFlash *flash = malloc(sizeof(MPFlash));
	flash->type = FLASH_TYPE_DISPLAY;
	flash->display.window = NULL;

	// All GTK functions must be called on the main thread
	g_main_context_invoke(NULL, (GSourceFunc)create_display, flash);

	return flash;
}

static bool flash_free(MPFlash *flash)
{
	gtk_window_destroy(GTK_WINDOW(flash->display.window));
	free(flash);
	return false;
}

void
mp_flash_free(MPFlash *flash)
{
	switch (flash->type) {
		case FLASH_TYPE_LED:
			close(flash->led.fd);
			free(flash);
			break;
		case FLASH_TYPE_DISPLAY:
			g_main_context_invoke(NULL, (GSourceFunc)flash_free, flash);
			break;
	}
}

static bool show_flash_window(MPFlash *flash)
{
	gtk_widget_show(flash->display.window);
	return false;
}

void
mp_flash_enable(MPFlash *flash)
{
	switch (flash->type) {
		case FLASH_TYPE_LED:
			lseek(flash->led.fd, 0, SEEK_SET);
			dprintf(flash->led.fd, "1\n");
			break;
		case FLASH_TYPE_DISPLAY:
			g_main_context_invoke(NULL, (GSourceFunc)show_flash_window, flash);
			break;
	}
}

static bool hide_flash_window(MPFlash *flash)
{
	gtk_widget_hide(flash->display.window);
	return false;
}

void
mp_flash_disable(MPFlash *flash)
{
	switch (flash->type) {
		case FLASH_TYPE_LED:
			// Flash gets reset automatically
			break;
		case FLASH_TYPE_DISPLAY:
			g_main_context_invoke(NULL, (GSourceFunc)hide_flash_window, flash);
			break;
	}
}

