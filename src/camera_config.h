#pragma once

#include "mode.h"

#include <stdbool.h>
#include <stddef.h>

#define MP_MAX_CAMERAS 5
#define MP_MAX_LINKS 10
#define MP_MAX_FORMATS 10
#define MP_MAX_CROPS 10

struct mp_media_link_config {
        char source_name[100];
        char target_name[100];
        int source_port;
        int target_port;
};

struct mp_media_format_config {
        char name[100];
        int pad;
        MPMode mode;
};

struct mp_media_crop_config {
        char name[100];
        int pad;
        int left;
        int top;
        int width;
        int height;
};

struct mp_camera_config {
        size_t index;

        char cfg_name[100];
        char dev_name[260];
        char media_dev_name[260];

        MPMode capture_mode;
        MPMode preview_mode;
        int rotate;
        bool mirrored;

        struct mp_media_link_config media_links[MP_MAX_LINKS];
        int num_media_links;

        struct mp_media_format_config media_formats[MP_MAX_FORMATS];
        int num_media_formats;

        struct mp_media_crop_config media_crops[MP_MAX_CROPS];
        int num_media_crops;

        float colormatrix[9];
        float forwardmatrix[9];
        float previewmatrix[9];
        int blacklevel;
        int whitelevel;

        float focallength;
        float cropfactor;
        double fnumber;
        int iso_min;
        int iso_max;

        char flash_path[260];
        bool flash_display;
        bool has_flash;
};

bool mp_load_config();

const char *mp_get_device_make();
const char *mp_get_device_model();
const struct mp_camera_config *mp_get_camera_config(size_t index);
