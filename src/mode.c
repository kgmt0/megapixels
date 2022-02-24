#include "mode.h"

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const char *pixel_format_names[MP_PIXEL_FMT_MAX] = {
        "unsupported", "BGGR8",   "GBRG8",   "GRBG8", "RGGB8", "BGGR10P",
        "GBRG10P",     "GRBG10P", "RGGB10P", "UYVY",  "YUYV",
};

const char *
mp_pixel_format_to_str(uint32_t pixel_format)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, "INVALID");
        return pixel_format_names[pixel_format];
}

MPPixelFormat
mp_pixel_format_from_str(const char *name)
{
        for (MPPixelFormat i = 0; i < MP_PIXEL_FMT_MAX; ++i) {
                if (strcasecmp(pixel_format_names[i], name) == 0) {
                        return i;
                }
        }
        g_return_val_if_reached(MP_PIXEL_FMT_UNSUPPORTED);
}

static const uint32_t pixel_format_v4l_pixel_formats[MP_PIXEL_FMT_MAX] = {
        0,
        V4L2_PIX_FMT_SBGGR8,
        V4L2_PIX_FMT_SGBRG8,
        V4L2_PIX_FMT_SGRBG8,
        V4L2_PIX_FMT_SRGGB8,
        V4L2_PIX_FMT_SBGGR10P,
        V4L2_PIX_FMT_SGBRG10P,
        V4L2_PIX_FMT_SGRBG10P,
        V4L2_PIX_FMT_SRGGB10P,
        V4L2_PIX_FMT_UYVY,
        V4L2_PIX_FMT_YUYV,
};

uint32_t
mp_pixel_format_to_v4l_pixel_format(MPPixelFormat pixel_format)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        return pixel_format_v4l_pixel_formats[pixel_format];
}

MPPixelFormat
mp_pixel_format_from_v4l_pixel_format(uint32_t v4l_pixel_format)
{
        for (MPPixelFormat i = 0; i < MP_PIXEL_FMT_MAX; ++i) {
                if (pixel_format_v4l_pixel_formats[i] == v4l_pixel_format) {
                        return i;
                }
        }
        return MP_PIXEL_FMT_UNSUPPORTED;
}

static const uint32_t pixel_format_v4l_bus_codes[MP_PIXEL_FMT_MAX] = {
        0,
        MEDIA_BUS_FMT_SBGGR8_1X8,
        MEDIA_BUS_FMT_SGBRG8_1X8,
        MEDIA_BUS_FMT_SGRBG8_1X8,
        MEDIA_BUS_FMT_SRGGB8_1X8,
        MEDIA_BUS_FMT_SBGGR10_1X10,
        MEDIA_BUS_FMT_SGBRG10_1X10,
        MEDIA_BUS_FMT_SGRBG10_1X10,
        MEDIA_BUS_FMT_SRGGB10_1X10,
        MEDIA_BUS_FMT_UYVY8_2X8,
        MEDIA_BUS_FMT_YUYV8_2X8,
};

uint32_t
mp_pixel_format_to_v4l_bus_code(MPPixelFormat pixel_format)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        return pixel_format_v4l_bus_codes[pixel_format];
}

MPPixelFormat
mp_pixel_format_from_v4l_bus_code(uint32_t v4l_bus_code)
{
        for (MPPixelFormat i = 0; i < MP_PIXEL_FMT_MAX; ++i) {
                if (pixel_format_v4l_bus_codes[i] == v4l_bus_code) {
                        return i;
                }
        }
        return MP_PIXEL_FMT_UNSUPPORTED;
}

uint32_t
mp_pixel_format_bits_per_pixel(MPPixelFormat pixel_format)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8:
                return 8;
        case MP_PIXEL_FMT_BGGR10P:
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P:
                return 10;
        case MP_PIXEL_FMT_UYVY:
        case MP_PIXEL_FMT_YUYV:
                return 16;
        default:
                return 0;
        }
}

uint32_t
mp_pixel_format_pixel_depth(MPPixelFormat pixel_format)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8:
        case MP_PIXEL_FMT_UYVY:
        case MP_PIXEL_FMT_YUYV:
                return 8;
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P:
        case MP_PIXEL_FMT_BGGR10P:
                return 10;
        default:
                return 0;
        }
}

const char *
mp_pixel_format_cfa(MPPixelFormat pixel_format)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_BGGR10P:
                return "BGGR";
                break;
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GBRG10P:
                return "GBRG";
                break;
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_GRBG10P:
                return "GRBG";
                break;
        case MP_PIXEL_FMT_RGGB8:
        case MP_PIXEL_FMT_RGGB10P:
                return "RGGB";
                break;
        case MP_PIXEL_FMT_UYVY:
                return "UYUV";
                break;
        case MP_PIXEL_FMT_YUYV:
                return "YUYV";
                break;
        default:
                return "unsupported";
        }
}

const char *
mp_pixel_format_cfa_pattern(MPPixelFormat pixel_format)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_BGGR10P:
                return "\002\001\001\000";
                break;
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GBRG10P:
                return "\001\002\000\001";
                break;
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_GRBG10P:
                return "\001\000\002\001";
                break;
        case MP_PIXEL_FMT_RGGB8:
        case MP_PIXEL_FMT_RGGB10P:
                return "\000\001\001\002";
                break;
        default:
                return NULL;
        }
}

uint32_t
mp_pixel_format_width_to_bytes(MPPixelFormat pixel_format, uint32_t width)
{
        uint32_t bits_per_pixel = mp_pixel_format_bits_per_pixel(pixel_format);
        uint64_t bits_per_width = width * (uint64_t)bits_per_pixel;

        uint64_t remainder = bits_per_width % 8;
        if (remainder == 0)
                return bits_per_width / 8;

        return (bits_per_width + 8 - remainder) / 8;
}

uint32_t
mp_pixel_format_width_to_padding(MPPixelFormat pixel_format, uint32_t width)
{
        uint64_t bytes_per_width =
                mp_pixel_format_width_to_bytes(pixel_format, width);

        uint64_t remainder = bytes_per_width % 8;
        if (remainder == 0)
                return remainder;

        return 8 - remainder;
}

uint32_t
mp_pixel_format_width_to_colors(MPPixelFormat pixel_format, uint32_t width)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8:
                return width / 2;
        case MP_PIXEL_FMT_BGGR10P:
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P:
                return width / 2 * 5;
        case MP_PIXEL_FMT_UYVY:
        case MP_PIXEL_FMT_YUYV:
                return width;
        default:
                return 0;
        }
}

uint32_t
mp_pixel_format_height_to_colors(MPPixelFormat pixel_format, uint32_t height)
{
        g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
        switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8:
        case MP_PIXEL_FMT_BGGR10P:
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P:
                return height / 2;
        case MP_PIXEL_FMT_UYVY:
        case MP_PIXEL_FMT_YUYV:
                return height;
        default:
                return 0;
        }
}

bool
mp_mode_is_equivalent(const MPMode *m1, const MPMode *m2)
{
        return m1->pixel_format == m2->pixel_format &&
               m1->frame_interval.numerator == m2->frame_interval.numerator &&
               m1->frame_interval.denominator == m2->frame_interval.denominator &&
               m1->width == m2->width && m1->height == m2->height;
}
