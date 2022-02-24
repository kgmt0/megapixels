#pragma once

#include <linux/v4l2-subdev.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
        MP_PIXEL_FMT_UNSUPPORTED,
        MP_PIXEL_FMT_BGGR8,
        MP_PIXEL_FMT_GBRG8,
        MP_PIXEL_FMT_GRBG8,
        MP_PIXEL_FMT_RGGB8,
        MP_PIXEL_FMT_BGGR10P,
        MP_PIXEL_FMT_GBRG10P,
        MP_PIXEL_FMT_GRBG10P,
        MP_PIXEL_FMT_RGGB10P,
        MP_PIXEL_FMT_UYVY,
        MP_PIXEL_FMT_YUYV,

        MP_PIXEL_FMT_MAX,
} MPPixelFormat;

const char *mp_pixel_format_to_str(MPPixelFormat pixel_format);
MPPixelFormat mp_pixel_format_from_str(const char *str);

MPPixelFormat mp_pixel_format_from_v4l_pixel_format(uint32_t v4l_pixel_format);
MPPixelFormat mp_pixel_format_from_v4l_bus_code(uint32_t v4l_bus_code);
uint32_t mp_pixel_format_to_v4l_pixel_format(MPPixelFormat pixel_format);
uint32_t mp_pixel_format_to_v4l_bus_code(MPPixelFormat pixel_format);

uint32_t mp_pixel_format_bits_per_pixel(MPPixelFormat pixel_format);
uint32_t mp_pixel_format_pixel_depth(MPPixelFormat pixel_format);
const char *mp_pixel_format_cfa(MPPixelFormat pixel_format);
const char *mp_pixel_format_cfa_pattern(MPPixelFormat pixel_format);
uint32_t mp_pixel_format_width_to_bytes(MPPixelFormat pixel_format, uint32_t width);
uint32_t mp_pixel_format_width_to_padding(MPPixelFormat pixel_format,
                                          uint32_t width);
uint32_t mp_pixel_format_width_to_colors(MPPixelFormat pixel_format, uint32_t width);
uint32_t mp_pixel_format_height_to_colors(MPPixelFormat pixel_format,
                                          uint32_t height);

typedef struct {
        MPPixelFormat pixel_format;

        struct v4l2_fract frame_interval;
        uint32_t width;
        uint32_t height;
} MPMode;

bool mp_mode_is_equivalent(const MPMode *m1, const MPMode *m2);

typedef struct _MPModeList MPModeList;

struct _MPModeList {
        MPMode mode;
        MPModeList *next;
};
