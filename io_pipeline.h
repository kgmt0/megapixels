#pragma once

#include "camera_config.h"

struct mp_io_pipeline_state {
	const struct mp_camera_config *camera;

	int burst_length;

	int preview_width;
	int preview_height;

	bool gain_is_manual;
	int gain;

	bool exposure_is_manual;
	int exposure;
};

void mp_io_pipeline_start();
void mp_io_pipeline_stop();

void mp_io_pipeline_focus();
void mp_io_pipeline_capture();

void mp_io_pipeline_update_state(const struct mp_io_pipeline_state *state);
