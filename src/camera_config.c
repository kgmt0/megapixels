#include "camera_config.h"

#include "config.h"
#include "ini.h"
#include "matrix.h"
#include <assert.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct mp_camera_config cameras[MP_MAX_CAMERAS];
static size_t num_cameras = 0;

static char *exif_make;
static char *exif_model;

static bool
find_config(char *conffile)
{
        char buf[512];
        FILE *fp;

        if (access("/proc/device-tree/compatible", F_OK) != -1) {
                // Reads to compatible string of the current device tree, looks like:
                // pine64,pinephone-1.2\0allwinner,sun50i-a64\0
                fp = fopen("/proc/device-tree/compatible", "r");
                fgets(buf, 512, fp);
                fclose(fp);

                // Check config/%dt.ini in the current working directory
                sprintf(conffile, "config/%s.ini", buf);
                if (access(conffile, F_OK) != -1) {
                        printf("Found config file at %s\n", conffile);
                        return true;
                }

                // Check for a config file in XDG_CONFIG_HOME
                sprintf(conffile,
                        "%s/megapixels/config/%s.ini",
                        g_get_user_config_dir(),
                        buf);
                if (access(conffile, F_OK) != -1) {
                        printf("Found config file at %s\n", conffile);
                        return true;
                }

                // Check user overridden /etc/megapixels/config/$dt.ini
                sprintf(conffile, "%s/megapixels/config/%s.ini", SYSCONFDIR, buf);
                if (access(conffile, F_OK) != -1) {
                        printf("Found config file at %s\n", conffile);
                        return true;
                }
                // Check packaged /usr/share/megapixels/config/$dt.ini
                sprintf(conffile, "%s/megapixels/config/%s.ini", DATADIR, buf);
                if (access(conffile, F_OK) != -1) {
                        printf("Found config file at %s\n", conffile);
                        return true;
                }
                printf("%s not found\n", conffile);
        } else {
                printf("Could not read device name from device tree\n");
        }

        // If all else fails, fall back to /etc/megapixels.ini
        sprintf(conffile, "/etc/megapixels.ini");
        if (access(conffile, F_OK) != -1) {
                printf("Found config file at %s\n", conffile);
                return true;
        }

        return false;
}

static int
strtoint(const char *nptr, char **endptr, int base)
{
        long x = strtol(nptr, endptr, base);
        assert(x <= INT_MAX);
        return (int)x;
}

static bool
config_handle_camera_mode(const char *prefix,
                          MPMode *mode,
                          const char *name,
                          const char *value)
{
        int prefix_length = strlen(prefix);
        if (strncmp(prefix, name, prefix_length) != 0)
                return false;

        name += prefix_length;

        if (strcmp(name, "width") == 0) {
                mode->width = strtoint(value, NULL, 10);
        } else if (strcmp(name, "height") == 0) {
                mode->height = strtoint(value, NULL, 10);
        } else if (strcmp(name, "rate") == 0) {
                mode->frame_interval.numerator = 1;
                mode->frame_interval.denominator = strtoint(value, NULL, 10);
        } else if (strcmp(name, "fmt") == 0) {
                mode->pixel_format = mp_pixel_format_from_str(value);
                if (mode->pixel_format == MP_PIXEL_FMT_UNSUPPORTED) {
                        g_printerr("Unsupported pixelformat %s\n", value);
                        exit(1);
                }
        } else {
                return false;
        }
        return true;
}

static int
config_ini_handler(void *user,
                   const char *section,
                   const char *name,
                   const char *value)
{
        if (strcmp(section, "device") == 0) {
                if (strcmp(name, "make") == 0) {
                        exif_make = strdup(value);
                } else if (strcmp(name, "model") == 0) {
                        exif_model = strdup(value);
                } else {
                        g_printerr("Unknown key '%s' in [device]\n", name);
                        exit(1);
                }
        } else {
                if (num_cameras == MP_MAX_CAMERAS) {
                        g_printerr("More cameras defined than NUM_CAMERAS\n");
                        exit(1);
                }

                size_t index = 0;
                for (; index < num_cameras; ++index) {
                        if (strcmp(cameras[index].cfg_name, section) == 0) {
                                break;
                        }
                }

                if (index == num_cameras) {
                        printf("Adding camera %s from config\n", section);
                        ++num_cameras;

                        cameras[index].index = index;
                        strcpy(cameras[index].cfg_name, section);
                }

                struct mp_camera_config *cc = &cameras[index];

                if (config_handle_camera_mode(
                            "capture-", &cc->capture_mode, name, value)) {
                } else if (config_handle_camera_mode(
                                   "preview-", &cc->preview_mode, name, value)) {
                } else if (strcmp(name, "rotate") == 0) {
                        cc->rotate = strtoint(value, NULL, 10);
                } else if (strcmp(name, "mirrored") == 0) {
                        cc->mirrored = strcmp(value, "true") == 0;
                } else if (strcmp(name, "driver") == 0) {
                        strcpy(cc->dev_name, value);
                } else if (strcmp(name, "media-driver") == 0) {
                        strcpy(cc->media_dev_name, value);
                } else if (strcmp(name, "media-links") == 0) {
                        char **linkdefs = g_strsplit(value, ",", 0);

                        for (int i = 0; i < MP_MAX_LINKS && linkdefs[i] != NULL;
                             ++i) {
                                char **linkdef = g_strsplit(linkdefs[i], "->", 2);
                                char **porta = g_strsplit(linkdef[0], ":", 2);
                                char **portb = g_strsplit(linkdef[1], ":", 2);

                                strcpy(cc->media_links[i].source_name, porta[0]);
                                strcpy(cc->media_links[i].target_name, portb[0]);
                                cc->media_links[i].source_port =
                                        strtoint(porta[1], NULL, 10);
                                cc->media_links[i].target_port =
                                        strtoint(portb[1], NULL, 10);

                                g_strfreev(portb);
                                g_strfreev(porta);
                                g_strfreev(linkdef);
                                ++cc->num_media_links;
                        }
                        g_strfreev(linkdefs);
                } else if (strcmp(name, "media-formats") == 0) {
                        struct mp_camera_config *cc = &cameras[index];
                        char **formatdefs = g_strsplit(value, ",", 0);

                        for (int i = 0; i < MP_MAX_FORMATS && formatdefs[i] != NULL;
                             ++i) {
                                char **entry = g_strsplit(formatdefs[i], ":", 5);
                                char *name = entry[0];
                                int pad = strtoint(entry[1], NULL, 10);
                                char *format = entry[2];
                                char *width = entry[3];
                                char *height = entry[4];

                                const size_t name_size =
                                        sizeof(cc->media_formats[i].name);
                                strncpy(cc->media_formats[i].name,
                                        name,
                                        name_size );

                                cc->media_formats[i].pad = pad;

                                cc->media_formats[i].mode.pixel_format =
                                        mp_pixel_format_from_str(format);
                                cc->media_formats[i].mode.width =
                                        strtoint(width, NULL, 10);
                                cc->media_formats[i].mode.height =
                                        strtoint(height, NULL, 10);

                                cc->num_media_formats++;

                                g_strfreev(entry);
                        }
                } else if (strcmp(name, "media-crops") == 0) {
                        char **formatdefs = g_strsplit(value, ",", 0);

                        for (int i = 0; i < MP_MAX_CROPS && formatdefs[i] != NULL;
                             ++i) {
                                char **entry = g_strsplit(formatdefs[i], ":", 6);
                                char *name = entry[0];
                                int pad = strtoint(entry[1], NULL, 10);
                                int top = strtoint(entry[2], NULL, 10);
                                int left = strtoint(entry[3], NULL, 10);
                                int width = strtoint(entry[4], NULL, 10);
                                int height = strtoint(entry[5], NULL, 10);

                                const size_t name_size =
                                        sizeof(cc->media_crops[i].name);
                                strncpy(cc->media_crops[i].name,
                                        name,
                                        name_size );

                                cc->media_crops[i].pad = pad;
                                cc->media_crops[i].top = top;
                                cc->media_crops[i].left = left;
                                cc->media_crops[i].width = width;
                                cc->media_crops[i].height = height;

                                cc->num_media_crops++;

                                g_strfreev(entry);
                        }
                } else if (strcmp(name, "colormatrix") == 0) {
                        sscanf(value,
                               "%f,%f,%f,%f,%f,%f,%f,%f,%f",
                               cc->colormatrix + 0,
                               cc->colormatrix + 1,
                               cc->colormatrix + 2,
                               cc->colormatrix + 3,
                               cc->colormatrix + 4,
                               cc->colormatrix + 5,
                               cc->colormatrix + 6,
                               cc->colormatrix + 7,
                               cc->colormatrix + 8);
                } else if (strcmp(name, "forwardmatrix") == 0) {
                        sscanf(value,
                               "%f,%f,%f,%f,%f,%f,%f,%f,%f",
                               cc->forwardmatrix + 0,
                               cc->forwardmatrix + 1,
                               cc->forwardmatrix + 2,
                               cc->forwardmatrix + 3,
                               cc->forwardmatrix + 4,
                               cc->forwardmatrix + 5,
                               cc->forwardmatrix + 6,
                               cc->forwardmatrix + 7,
                               cc->forwardmatrix + 8);
                } else if (strcmp(name, "whitelevel") == 0) {
                        cc->whitelevel = strtoint(value, NULL, 10);
                } else if (strcmp(name, "blacklevel") == 0) {
                        cc->blacklevel = strtoint(value, NULL, 10);
                } else if (strcmp(name, "focallength") == 0) {
                        cc->focallength = strtof(value, NULL);
                } else if (strcmp(name, "cropfactor") == 0) {
                        cc->cropfactor = strtof(value, NULL);
                } else if (strcmp(name, "fnumber") == 0) {
                        cc->fnumber = strtod(value, NULL);
                } else if (strcmp(name, "iso-min") == 0) {
                        cc->iso_min = strtod(value, NULL);
                } else if (strcmp(name, "iso-max") == 0) {
                        cc->iso_max = strtod(value, NULL);
                } else if (strcmp(name, "flash-path") == 0) {
                        strcpy(cc->flash_path, value);
                        cc->has_flash = true;
                } else if (strcmp(name, "flash-display") == 0) {
                        cc->flash_display = strcmp(value, "true") == 0;

                        if (cc->flash_display) {
                                cc->has_flash = true;
                        }
                } else {
                        g_printerr("Unknown key '%s' in [%s]\n", name, section);
                        exit(1);
                }
        }
        return 1;
}

void
calculate_matrices()
{
        for (size_t i = 0; i < MP_MAX_CAMERAS; ++i) {
                if (cameras[i].colormatrix != NULL &&
                    cameras[i].forwardmatrix != NULL) {
                        multiply_matrices(cameras[i].colormatrix,
                                          cameras[i].forwardmatrix,
                                          cameras[i].previewmatrix);
                }
        }
}

bool
mp_load_config()
{
        char file[512];
        if (!find_config(file)) {
                g_printerr("Could not find any config file\n");
                return false;
        }

        int result = ini_parse(file, config_ini_handler, NULL);
        if (result == -1) {
                g_printerr("Config file not found\n");
                return false;
        }
        if (result == -2) {
                g_printerr("Could not allocate memory to parse config file\n");
                return false;
        }
        if (result != 0) {
                g_printerr("Could not parse config file\n");
                return false;
        }

        calculate_matrices();

        return true;
}

const char *
mp_get_device_make()
{
        return exif_make;
}

const char *
mp_get_device_model()
{
        return exif_model;
}

const struct mp_camera_config *
mp_get_camera_config(size_t index)
{
        if (index >= num_cameras)
                return NULL;

        return &cameras[index];
}
