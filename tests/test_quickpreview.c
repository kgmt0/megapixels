#include "quickpreview.h"
#include <assert.h>
#include <glib.h>
#include <stdio.h>

static void
test_quick_preview_fuzz()
{
	for (size_t i = 0; i < 10000; ++i) {
		uint32_t width = rand() % 127 + 1;
		uint32_t height = rand() % 127 + 1;
		uint32_t format = rand() % (MP_PIXEL_FMT_MAX - 1) + 1;

		assert(width > 0 && height > 0);

		switch (format) {
			case MP_PIXEL_FMT_BGGR8:
			case MP_PIXEL_FMT_GBRG8:
			case MP_PIXEL_FMT_GRBG8:
			case MP_PIXEL_FMT_RGGB8:
				width += width % 2;
				height += height % 2;
				break;
			case MP_PIXEL_FMT_BGGR10P:
			case MP_PIXEL_FMT_GBRG10P:
			case MP_PIXEL_FMT_GRBG10P:
			case MP_PIXEL_FMT_RGGB10P:
				width += width % 2;
				height += height % 2;
				break;
			case MP_PIXEL_FMT_UYVY:
			case MP_PIXEL_FMT_YUYV:
				width += width % 4;
				break;
			default:
				assert(false);
		}

		int rotation;
		switch (rand() % 3) {
			case 0:
				rotation = 0;
				break;
			case 1:
				rotation = 90;
				break;
			case 2:
				rotation = 180;
				break;
			default:
				rotation = 270;
				break;
		}

		bool mirrored = rand() % 2;

		float matbuf[9];
		float *colormatrix = NULL;
		if (rand() % 2) {
			for (int j = 0; j < 9; ++j) {
				matbuf[j] = (double)rand() / RAND_MAX * 2.0;
			}
			colormatrix = matbuf;
		}

		uint8_t blacklevel = rand() % 3;

		size_t src_size = mp_pixel_format_width_to_bytes(format, width) *  height;
		uint8_t *src = malloc(src_size);
		for (int j = 0; j < src_size; ++j) {
			src[j] = rand();
		}

		uint32_t preview_width = mp_pixel_format_width_to_colors(format, width);
		uint32_t preview_height = mp_pixel_format_height_to_colors(format, height);
		if (preview_width > 32 && preview_height > 32) {
			preview_width /= (1 + rand() % 2);
			preview_height /= (1 + rand() % 2);
		}

		uint32_t dst_width, dst_height, skip;
		quick_preview_size(
			&dst_width,
			&dst_height,
			&skip,
			preview_width,
			preview_height,
			width,
			height,
			format,
			rotation);

		uint32_t *dst = malloc(dst_width * dst_height * sizeof(uint32_t));

		quick_preview(
			dst,
			dst_width,
			dst_height,
			src,
			width,
			height,
			format,
			rotation,
			mirrored,
			colormatrix,
			blacklevel,
			skip);

		free(dst);
		free(src);
	}
}

int
main(int argc, char *argv[])
{
	g_test_init(&argc, &argv, NULL);
	g_test_add_func("/quick_preview/fuzz", test_quick_preview_fuzz);
	return g_test_run();
}
