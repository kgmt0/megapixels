/*
 * Fast but bad debayer method that scales and rotates by skipping source
 * pixels and doesn't interpolate any values at all
 */

#include "quickpreview.h"
#include <assert.h>
#include <stdio.h>

static inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
	return (r << 16) | (g << 8) | b;
}

static inline uint32_t convert_yuv_to_srgb(uint8_t y, uint8_t u, uint8_t v)
{
	uint32_t r = 1.164f * y + 1.596f * (v - 128);
	uint32_t g = 1.164f * y - 0.813f * (v - 128) - 0.391f * (u - 128);
	uint32_t b = 1.164f * y + 2.018f * (u - 128);
	return pack_rgb(r, g, b);
}

static inline uint32_t apply_colormatrix(uint32_t color, const float *colormatrix)
{
	if (!colormatrix) {
		return color;
	}

	uint32_t r = (color >> 16) * colormatrix[0] + ((color >> 8) & 0xFF) * colormatrix[1] + (color & 0xFF) * colormatrix[2];
	uint32_t g = (color >> 16) * colormatrix[3] + ((color >> 8) & 0xFF) * colormatrix[4] + (color & 0xFF) * colormatrix[5];
	uint32_t b = (color >> 16) * colormatrix[6] + ((color >> 8) & 0xFF) * colormatrix[7] + (color & 0xFF) * colormatrix[8];

	// Clip colors
	if (r > 0xFF)
		r = 0xFF;
	if (g > 0xFF)
		g = 0xFF;
	if (b > 0xFF)
		b = 0xFF;
	return pack_rgb(r, g, b);
}

static inline uint32_t coord_map(uint32_t x, uint32_t y, uint32_t width, uint32_t height, int rotation, bool mirrored)
{
	uint32_t x_r, y_r;
	if (rotation == 0) {
		x_r = x;
		y_r = y;
	} else if (rotation == 90) {
		x_r = y;
		y_r = height - x - 1;
	} else if (rotation == 270) {
		x_r = width - y - 1;
		y_r = x;
	} else {
		x_r = width - x - 1;
		y_r = height - y - 1;
	}

	if (mirrored) {
		x_r = width - x_r - 1;
	}

	uint32_t index = y_r * width + x_r;
#ifdef DEBUG
	assert(index < width * height);
#endif
	return index;
}

static void quick_preview_rggb8(
	uint32_t *dst,
	const uint32_t dst_width,
	const uint32_t dst_height,
	const uint8_t *src,
	const uint32_t src_width,
	const uint32_t src_height,
	const MPPixelFormat format,
	const uint32_t rotation,
	const bool mirrored,
	const float *colormatrix,
	const uint8_t blacklevel,
	const uint32_t skip)
{
	uint32_t src_y = 0, dst_y = 0;
	while (src_y < src_height) {
		uint32_t src_x = 0, dst_x = 0;
		while (src_x < src_width) {
			uint32_t src_i = src_y * src_width + src_x;

			uint8_t b0 = src[src_i] - blacklevel;
			uint8_t b1 = src[src_i + 1] - blacklevel;
			uint8_t b2 = src[src_i + src_width + 1] - blacklevel;

			uint32_t color;
			switch (format) {
				case MP_PIXEL_FMT_BGGR8:
					color = pack_rgb(b2, b1, b0);
					break;
				case MP_PIXEL_FMT_GBRG8:
					color = pack_rgb(b2, b0, b1);
					break;
				case MP_PIXEL_FMT_GRBG8:
					color = pack_rgb(b1, b0, b2);
					break;
				case MP_PIXEL_FMT_RGGB8:
					color = pack_rgb(b0, b1, b2);
					break;
				default:
					assert(false);
			}

			color = apply_colormatrix(color, colormatrix);

			dst[coord_map(dst_x, dst_y, dst_width, dst_height, rotation, mirrored)] = color;

			src_x += 2 + 2 * skip;
			++dst_x;
		}

		src_y += 2 + 2 * skip;
		++dst_y;
	}
}

static void quick_preview_rggb10(
	uint32_t *dst,
	const uint32_t dst_width,
	const uint32_t dst_height,
	const uint8_t *src,
	const uint32_t src_width,
	const uint32_t src_height,
	const MPPixelFormat format,
	const uint32_t rotation,
	const bool mirrored,
	const float *colormatrix,
	const uint8_t blacklevel,
	const uint32_t skip)
{
	assert(src_width % 2 == 0);

	uint32_t width_bytes = mp_pixel_format_width_to_bytes(format, src_width);

	uint32_t src_y = 0, dst_y = 0;
	while (src_y < src_height) {
		uint32_t src_x = 0, dst_x = 0;
		while (src_x < width_bytes) {
			uint32_t src_i = src_y * width_bytes + src_x;

			uint8_t b0 = src[src_i] - blacklevel;
			uint8_t b1 = src[src_i + 1] - blacklevel;
			uint8_t b2 = src[src_i + width_bytes + 1] - blacklevel;

			uint32_t color;
			switch (format) {
				case MP_PIXEL_FMT_BGGR10P:
					color = pack_rgb(b2, b1, b0);
					break;
				case MP_PIXEL_FMT_GBRG10P:
					color = pack_rgb(b2, b0, b1);
					break;
				case MP_PIXEL_FMT_GRBG10P:
					color = pack_rgb(b1, b0, b2);
					break;
				case MP_PIXEL_FMT_RGGB10P:
					color = pack_rgb(b0, b1, b2);
					break;
				default:
					assert(false);
			}

			color = apply_colormatrix(color, colormatrix);

			dst[coord_map(dst_x, dst_y, dst_width, dst_height, rotation, mirrored)] = color;

			uint32_t advance = 1 + skip;
			if (src_x % 5 == 0) {
				src_x += 2 * (advance % 2) + 5 * (advance / 2);
			} else {
				src_x += 3 * (advance % 2) + 5 * (advance / 2);
			}
			++dst_x;
		}

		src_y += 2 + 2 * skip;
		++dst_y;
	}
}

static void quick_preview_yuv(
	uint32_t *dst,
	const uint32_t dst_width,
	const uint32_t dst_height,
	const uint8_t *src,
	const uint32_t src_width,
	const uint32_t src_height,
	const MPPixelFormat format,
	const uint32_t rotation,
	const bool mirrored,
	const float *colormatrix,
	const uint32_t skip)
{
	assert(src_width % 2 == 0);

	uint32_t width_bytes = src_width * 2;

	uint32_t unrot_dst_width = dst_width;
	if (rotation != 0 && rotation != 180) {
		unrot_dst_width = dst_height;
	}

	uint32_t src_y = 0, dst_y = 0;
	while (src_y < src_height) {
		uint32_t src_x = 0, dst_x = 0;
		while (src_x < width_bytes) {
			uint32_t src_i = src_y * width_bytes + src_x;

			uint8_t b0 = src[src_i];
			uint8_t b1 = src[src_i + 1];
			uint8_t b2 = src[src_i + 2];
			uint8_t b3 = src[src_i + 3];

			uint32_t color1, color2;
			switch (format) {
				case MP_PIXEL_FMT_UYVY:
					color1 = convert_yuv_to_srgb(b1, b0, b2);
					color2 = convert_yuv_to_srgb(b3, b0, b2);
					break;
				case MP_PIXEL_FMT_YUYV:
					color1 = convert_yuv_to_srgb(b0, b1, b3);
					color2 = convert_yuv_to_srgb(b2, b1, b3);
					break;
				default:
					assert(false);
			}

			color1 = apply_colormatrix(color1, colormatrix);
			color2 = apply_colormatrix(color2, colormatrix);

			uint32_t dst_i1 = coord_map(dst_x, dst_y, dst_width, dst_height, rotation, mirrored);
			dst[dst_i1] = color1;
			++dst_x;

			// The last pixel needs to be skipped if we have an odd un-rotated width
			if (dst_x < unrot_dst_width) {
				uint32_t dst_i2 = coord_map(dst_x, dst_y, dst_width, dst_height, rotation, mirrored);
				dst[dst_i2] = color2;
				++dst_x;
			}

			src_x += 4 + 4 * skip;
		}

		src_y += 1 + skip;
		++dst_y;
	}
}

void quick_preview(
	uint32_t *dst,
	const uint32_t dst_width,
	const uint32_t dst_height,
	const uint8_t *src,
	const uint32_t src_width,
	const uint32_t src_height,
	const MPPixelFormat format,
	const uint32_t rotation,
	const bool mirrored,
	const float *colormatrix,
	const uint8_t blacklevel,
	const uint32_t skip)
{
	switch (format) {
		case MP_PIXEL_FMT_BGGR8:
		case MP_PIXEL_FMT_GBRG8:
		case MP_PIXEL_FMT_GRBG8:
		case MP_PIXEL_FMT_RGGB8:
			quick_preview_rggb8(
				dst,
				dst_width,
				dst_height,
				src,
				src_width,
				src_height,
				format,
				rotation,
				mirrored,
				colormatrix,
				blacklevel,
				skip);
			break;
		case MP_PIXEL_FMT_BGGR10P:
		case MP_PIXEL_FMT_GBRG10P:
		case MP_PIXEL_FMT_GRBG10P:
		case MP_PIXEL_FMT_RGGB10P:
			quick_preview_rggb10(
				dst,
				dst_width,
				dst_height,
				src,
				src_width,
				src_height,
				format,
				rotation,
				mirrored,
				colormatrix,
				blacklevel,
				skip);
			break;
		case MP_PIXEL_FMT_UYVY:
		case MP_PIXEL_FMT_YUYV:
			quick_preview_yuv(
				dst,
				dst_width,
				dst_height,
				src,
				src_width,
				src_height,
				format,
				rotation,
				mirrored,
				colormatrix,
				skip);
			break;
		default:
			assert(false);
	}
}

static uint32_t div_ceil(uint32_t x, uint32_t y)
{
	return x/y + !!(x % y);
}

void quick_preview_size(
	uint32_t *dst_width,
	uint32_t *dst_height,
	uint32_t *skip,
	const uint32_t preview_width,
	const uint32_t preview_height,
	const uint32_t src_width,
	const uint32_t src_height,
	const MPPixelFormat format,
	const int rotation)
{
	uint32_t colors_x = mp_pixel_format_width_to_colors(format, src_width);
	uint32_t colors_y = mp_pixel_format_height_to_colors(format, src_height);

	if (rotation != 0 && rotation != 180) {
		uint32_t tmp = colors_x;
		colors_x = colors_y;
		colors_y = tmp;
	}

	uint32_t scale_x = colors_x / preview_width;
	uint32_t scale_y = colors_y / preview_height;

	if (scale_x > 0)
		--scale_x;
	if (scale_y > 0)
		--scale_y;
	*skip = scale_x > scale_y ? scale_x : scale_y;

	*dst_width = div_ceil(colors_x, (1 + *skip));
	if (*dst_width <= 0)
		*dst_width = 1;

	*dst_height = div_ceil(colors_y, (1 + *skip));
	if (*dst_height <= 0)
		*dst_height = 1;
}
