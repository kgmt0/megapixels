#include "zbar_pipeline.h"

#include "io_pipeline.h"
#include "main.h"
#include "pipeline.h"
#include <assert.h>
#include <zbar.h>

struct _MPZBarImage {
        uint8_t *data;
        MPPixelFormat pixel_format;
        int width;
        int height;
        int rotation;
        bool mirrored;

        _Atomic int ref_count;
};

static MPPipeline *pipeline;

static volatile int frames_processed = 0;
static volatile int frames_received = 0;

static zbar_image_scanner_t *scanner;

static void
setup(MPPipeline *pipeline, const void *data)
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

static bool
is_3d_code(zbar_symbol_type_t type)
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

static inline void
map_coords(int *x, int *y, int width, int height, int rotation, bool mirrored)
{
        int x_r, y_r;
        if (rotation == 0) {
                x_r = *x;
                y_r = *y;
        } else if (rotation == 90) {
                x_r = *y;
                y_r = height - *x - 1;
        } else if (rotation == 270) {
                x_r = width - *y - 1;
                y_r = *x;
        } else {
                x_r = width - *x - 1;
                y_r = height - *y - 1;
        }

        if (mirrored) {
                x_r = width - x_r - 1;
        }

        *x = x_r;
        *y = y_r;
}

static MPZBarCode
process_symbol(const MPZBarImage *image,
               int width,
               int height,
               const zbar_symbol_t *symbol)
{
        if (image->rotation == 90 || image->rotation == 270) {
                int tmp = width;
                width = height;
                height = tmp;
        }

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

        for (uint8_t i = 0; i < 4; ++i) {
                map_coords(&code.bounds_x[i],
                           &code.bounds_y[i],
                           width,
                           height,
                           image->rotation,
                           image->mirrored);
        }

        const char *data = zbar_symbol_get_data(symbol);
        unsigned int data_size = zbar_symbol_get_data_length(symbol);
        code.type = zbar_get_symbol_name(type);
        code.data = strndup(data, data_size + 1);
        code.data[data_size] = 0;

        return code;
}

static void
process_image(MPPipeline *pipeline, MPZBarImage **_image)
{
        MPZBarImage *image = *_image;

        assert(image->pixel_format == MP_PIXEL_FMT_BGGR8 ||
               image->pixel_format == MP_PIXEL_FMT_GBRG8 ||
               image->pixel_format == MP_PIXEL_FMT_GRBG8 ||
               image->pixel_format == MP_PIXEL_FMT_RGGB8 ||
               image->pixel_format == MP_PIXEL_FMT_BGGR10P ||
               image->pixel_format == MP_PIXEL_FMT_GBRG10P ||
               image->pixel_format == MP_PIXEL_FMT_GRBG10P ||
               image->pixel_format == MP_PIXEL_FMT_RGGB10P);

        // Create a grayscale image for scanning from the current preview.
        // Rotate/mirror correctly.
        int width = image->width / 2;
        int height = image->height / 2;

        uint8_t *data = malloc(width * height * sizeof(uint8_t));
        size_t row_length =
                mp_pixel_format_width_to_bytes(image->pixel_format, image->width);
        size_t i = 0;
        size_t offset;
        switch (image->pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8:
                for (int y = 0; y < image->height; y += 2) {
                        for (int x = 0; x < row_length; x += 2) {
                                data[i++] = image->data[x + row_length * y];
                        }
                }
                break;
        case MP_PIXEL_FMT_BGGR10P:
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P:
                // Skip 5th byte of each 4-pixel segment by incrementing an
                // offset every time a 5th byte is reached, making the
                // X coordinate land on the next byte:
                //
                // image->data | | | | X | | | | X | | | | X | | | | X | ...
                // x           0   2   4   6   8  10  12  14  16  18  20 ...
                // offset      0       1       2       3       4       5 ...
                //                     >       --->    ----->  ------->
                // x + offset  0   2     4   6     8  10    12  16    18 ...
                for (int y = 0; y < image->height; y += 2) {
                        offset = 0;
                        for (int x = 0; x < image->width; x += 2) {
                                if (x % 4 == 0)
                                        offset += 1;

                                data[i++] = image->data[x + offset + row_length * y];
                        }
                }
                break;
        default:
                assert(0);
        }

        // Create image for zbar
        zbar_image_t *zbar_image = zbar_image_create();
        zbar_image_set_format(zbar_image, zbar_fourcc('Y', '8', '0', '0'));
        zbar_image_set_size(zbar_image, width, height);
        zbar_image_set_data(zbar_image,
                            data,
                            width * height * sizeof(uint8_t),
                            zbar_image_free_data);

        int res = zbar_scan_image(scanner, zbar_image);
        assert(res >= 0);

        if (res > 0) {
                MPZBarScanResult *result = malloc(sizeof(MPZBarScanResult));
                result->size = res;

                const zbar_symbol_t *symbol = zbar_image_first_symbol(zbar_image);
                for (int i = 0; i < MIN(res, 8); ++i) {
                        assert(symbol != NULL);
                        result->codes[i] =
                                process_symbol(image, width, height, symbol);
                        symbol = zbar_symbol_next(symbol);
                }

                mp_main_set_zbar_result(result);
        } else {
                mp_main_set_zbar_result(NULL);
        }

        zbar_image_destroy(zbar_image);
        mp_zbar_image_unref(image);

        ++frames_processed;
}

void
mp_zbar_pipeline_process_image(MPZBarImage *image)
{
        // If we haven't processed the previous frame yet, drop this one
        if (frames_received != frames_processed) {
                mp_zbar_image_unref(image);
                return;
        }

        ++frames_received;

        mp_pipeline_invoke(pipeline,
                           (MPPipelineCallback)process_image,
                           &image,
                           sizeof(MPZBarImage *));
}

MPZBarImage *
mp_zbar_image_new(uint8_t *data,
                  MPPixelFormat pixel_format,
                  int width,
                  int height,
                  int rotation,
                  bool mirrored)
{
        MPZBarImage *image = malloc(sizeof(MPZBarImage));
        image->data = data;
        image->pixel_format = pixel_format;
        image->width = width;
        image->height = height;
        image->rotation = rotation;
        image->mirrored = mirrored;
        image->ref_count = 1;
        return image;
}

MPZBarImage *
mp_zbar_image_ref(MPZBarImage *image)
{
        ++image->ref_count;
        return image;
}

void
mp_zbar_image_unref(MPZBarImage *image)
{
        if (--image->ref_count == 0) {
                free(image->data);
                free(image);
        }
}
