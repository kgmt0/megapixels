#pragma once

#include "camera_config.h"

typedef struct _MPZBarImage MPZBarImage;

typedef struct {
	int bounds_x[4];
	int bounds_y[4];
	char *data;
	const char *type;
} MPZBarCode;

typedef struct {
	MPZBarCode codes[8];
	uint8_t size;
} MPZBarScanResult;

void mp_zbar_pipeline_start();
void mp_zbar_pipeline_stop();

void mp_zbar_pipeline_process_image(MPZBarImage *image);

MPZBarImage *mp_zbar_image_new(uint8_t *data,
			       MPPixelFormat pixel_format,
			       int width,
			       int height,
			       int rotation,
			       bool mirrored);
MPZBarImage *mp_zbar_image_ref(MPZBarImage *image);
void mp_zbar_image_unref(MPZBarImage *image);
