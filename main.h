#pragma once

#include "camera_config.h"
#include "gtk/gtk.h"

struct mp_main_state {
	const struct mp_camera_config *camera;
	MPCameraMode mode;

	bool gain_is_manual;
	int gain;
	int gain_max;

	bool exposure_is_manual;
	int exposure;

	bool has_auto_focus_continuous;
	bool has_auto_focus_start;
};

void mp_main_update_state(const struct mp_main_state *state);

void mp_main_set_preview(cairo_surface_t *image);
void mp_main_capture_completed(const char *fname);

int remap(int value, int input_min, int input_max, int output_min, int output_max);
