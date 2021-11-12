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
} MPDisplayFlash;

struct _MPFlash {
	FlashType type;

	union {
		MPLEDFlash led;
		MPDisplayFlash display;
	};
};

MPFlash *
mp_led_flash_from_path(const char *path)
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

static GtkWidget *flash_window = NULL;
static GDBusProxy *dbus_brightness_proxy = NULL;
static int dbus_old_brightness = 0;

static void
dbus_brightness_init(GObject *src, GAsyncResult *res, gpointer *user_data)
{
	GError *err = NULL;
	dbus_brightness_proxy = g_dbus_proxy_new_finish(res, &err);
	if (!dbus_brightness_proxy || err) {
		printf("Failed to connect to dbus brightness service %s\n",
		       err->message);
		g_object_unref(err);
		return;
	}
}

void
mp_flash_gtk_init(GDBusConnection *conn)
{
	g_dbus_proxy_new(conn, G_DBUS_PROXY_FLAGS_NONE, NULL,
			 "org.gnome.SettingsDaemon.Power",
			 "/org/gnome/SettingsDaemon/Power",
			 "org.gnome.SettingsDaemon.Power.Screen", NULL,
			 (GAsyncReadyCallback)dbus_brightness_init, NULL);

	// Create a full screen full white window as a flash
	GtkWidget *window = gtk_window_new();
	// gtk_window_set_accept_focus(GTK_WINDOW(flash->display.window), FALSE);
	gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
	gtk_window_fullscreen(GTK_WINDOW(window));

	GtkStyleContext *context;
	context = gtk_widget_get_style_context(window);
	gtk_style_context_add_class(context, "flash");

	flash_window = window;
}

void
mp_flash_gtk_clean()
{
	gtk_window_destroy(GTK_WINDOW(flash_window));
	g_object_unref(dbus_brightness_proxy);
}

MPFlash *
mp_create_display_flash()
{
	MPFlash *flash = malloc(sizeof(MPFlash));
	flash->type = FLASH_TYPE_DISPLAY;

	return flash;
}

void
mp_flash_free(MPFlash *flash)
{
	switch (flash->type) {
	case FLASH_TYPE_LED:
		close(flash->led.fd);
		break;
	case FLASH_TYPE_DISPLAY:
		break;
	}

	free(flash);
}

static void
set_display_brightness(int brightness)
{
	g_dbus_proxy_call(
		dbus_brightness_proxy, "org.freedesktop.DBus.Properties.Set",
		g_variant_new("(ssv)", "org.gnome.SettingsDaemon.Power.Screen",
			      "Brightness", g_variant_new("i", brightness)),
		G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL, NULL);
}

static void
brightness_received(GDBusProxy *proxy, GAsyncResult *res, gpointer user_data)
{
	GError *error = NULL;
	GVariant *result = g_dbus_proxy_call_finish(proxy, res, &error);

	if (!result) {
		printf("Failed to get display brightness: %s\n", error->message);
		g_object_unref(error);
		return;
	}

	GVariant *values = g_variant_get_child_value(result, 0);
	if (g_variant_n_children(values) == 0) {
		return;
	}

	GVariant *brightness = g_variant_get_child_value(values, 0);
	dbus_old_brightness = g_variant_get_int32(brightness);

	g_variant_unref(result);
}

static bool
show_display_flash(MPFlash *flash)
{
	if (!flash_window)
		return false;

	gtk_widget_show(flash_window);

	// First get brightness and then set brightness to 100%
	if (!dbus_brightness_proxy)
		return false;

	g_dbus_proxy_call(
		dbus_brightness_proxy, "org.freedesktop.DBus.Properties.Get",
		g_variant_new("(ss)", "org.gnome.SettingsDaemon.Power.Screen",
			      "Brightness"),
		G_DBUS_CALL_FLAGS_NONE, -1, NULL,
		(GAsyncReadyCallback)brightness_received, NULL);

	set_display_brightness(100);

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
		g_main_context_invoke(NULL, (GSourceFunc)show_display_flash, flash);
		break;
	}
}

static bool
hide_display_flash(MPFlash *flash)
{
	if (!flash_window)
		return false;

	gtk_widget_hide(flash_window);
	set_display_brightness(dbus_old_brightness);

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
		g_main_context_invoke(NULL, (GSourceFunc)hide_display_flash, flash);
		break;
	}
}
