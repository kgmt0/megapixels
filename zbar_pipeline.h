#pragma once

#include "camera_config.h"

typedef struct _cairo_surface cairo_surface_t;

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

void mp_zbar_pipeline_process_image(cairo_surface_t *surface);
