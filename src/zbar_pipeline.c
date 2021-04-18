#include "zbar_pipeline.h"

#include "pipeline.h"
#include "main.h"
#include "io_pipeline.h"
#include <zbar.h>
#include <assert.h>

static MPPipeline *pipeline;

static volatile int frames_processed = 0;
static volatile int frames_received = 0;

static zbar_image_scanner_t *scanner;

static void setup(MPPipeline *pipeline, const void *data)
{
	scanner = zbar_image_scanner_create();
	zbar_image_scanner_set_config(scanner, 0, ZBAR_CFG_ENABLE, 1);
}

void
mp_zbar_pipeline_start()
{
	pipeline = mp_pipeline_new();

	mp_pipeline_invoke(pipeline, setup, NULL, 0);
}

void
mp_zbar_pipeline_stop()
{
	mp_pipeline_free(pipeline);
}

static bool is_3d_code(zbar_symbol_type_t type)
{
	switch (type) {
		case ZBAR_EAN2:
		case ZBAR_EAN5:
		case ZBAR_EAN8:
		case ZBAR_UPCE:
		case ZBAR_ISBN10:
		case ZBAR_UPCA:
		case ZBAR_EAN13:
		case ZBAR_ISBN13:
		case ZBAR_I25:
		case ZBAR_DATABAR:
		case ZBAR_DATABAR_EXP:
		case ZBAR_CODABAR:
		case ZBAR_CODE39:
		case ZBAR_CODE93:
		case ZBAR_CODE128:
			return false;
		case ZBAR_COMPOSITE:
		case ZBAR_PDF417:
		case ZBAR_QRCODE:
		case ZBAR_SQCODE:
			return true;
		default:
			return false;
	}
}

static MPZBarCode
process_symbol(const zbar_symbol_t *symbol)
{
	MPZBarCode code;

	unsigned loc_size = zbar_symbol_get_loc_size(symbol);
	assert(loc_size > 0);

	zbar_symbol_type_t type = zbar_symbol_get_type(symbol);

	if (is_3d_code(type) && loc_size == 4) {
		for (unsigned i = 0; i < loc_size; ++i) {
			code.bounds_x[i] = zbar_symbol_get_loc_x(symbol, i);
			code.bounds_y[i] = zbar_symbol_get_loc_y(symbol, i);
		}
	} else {
		int min_x = zbar_symbol_get_loc_x(symbol, 0);
		int min_y = zbar_symbol_get_loc_y(symbol, 0);
		int max_x = min_x, max_y = min_y;
		for (unsigned i = 1; i < loc_size; ++i) {
			int x = zbar_symbol_get_loc_x(symbol, i);
			int y = zbar_symbol_get_loc_y(symbol, i);
			min_x = MIN(min_x, x);
			min_y = MIN(min_y, y);
			max_x = MAX(max_x, x);
			max_y = MAX(max_y, y);
		}

		code.bounds_x[0] = min_x;
		code.bounds_y[0] = min_y;
		code.bounds_x[1] = max_x;
		code.bounds_y[1] = min_y;
		code.bounds_x[2] = max_x;
		code.bounds_y[2] = max_y;
		code.bounds_x[3] = min_x;
		code.bounds_y[3] = max_y;
	}

	const char *data = zbar_symbol_get_data(symbol);
	unsigned int data_size = zbar_symbol_get_data_length(symbol);
	code.type = zbar_get_symbol_name(type);
	code.data = strndup(data, data_size+1);
	code.data[data_size] = 0;

	return code;
}

static void
process_surface(MPPipeline *pipeline, cairo_surface_t **_surface)
{
	cairo_surface_t *surface = *_surface;

	int width = cairo_image_surface_get_width(surface);
	int height = cairo_image_surface_get_height(surface);
	const uint32_t *surface_data = (const uint32_t *)cairo_image_surface_get_data(surface);

	// Create a grayscale image for scanning from the current preview
	uint8_t *data = malloc(width * height * sizeof(uint8_t));
	for (size_t i = 0; i < width * height; ++i) {
		data[i] = (surface_data[i] >> 16) & 0xff;
	}

	// Create image for zbar
	zbar_image_t *zbar_image = zbar_image_create();
	zbar_image_set_format(zbar_image, zbar_fourcc('Y', '8', '0', '0'));
	zbar_image_set_size(zbar_image, width, height);
	zbar_image_set_data(zbar_image, data, width * height * sizeof(uint8_t), zbar_image_free_data);

	int res = zbar_scan_image(scanner, zbar_image);
	assert(res >= 0);

	if (res > 0) {
		MPZBarScanResult *result = malloc(sizeof(MPZBarScanResult));
		result->size = res;

		const zbar_symbol_t *symbol = zbar_image_first_symbol(zbar_image);
		for (int i = 0; i < MIN(res, 8); ++i) {
			assert(symbol != NULL);
			result->codes[i] = process_symbol(symbol);
			symbol = zbar_symbol_next(symbol);
		}

		mp_main_set_zbar_result(result);
	} else {
		mp_main_set_zbar_result(NULL);
	}

	zbar_image_destroy(zbar_image);
	cairo_surface_destroy(surface);

	++frames_processed;
}

void
mp_zbar_pipeline_process_image(cairo_surface_t *surface)
{
	// If we haven't processed the previous frame yet, drop this one
	if (frames_received != frames_processed) {
		cairo_surface_destroy(surface);
		return;
	}

	++frames_received;

	mp_pipeline_invoke(pipeline, (MPPipelineCallback)process_surface, &surface,
			   sizeof(cairo_surface_t *));
}
