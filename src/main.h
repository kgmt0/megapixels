#pragma once

#include "camera_config.h"
#include "gtk/gtk.h"
#include "process_pipeline.h"
#include "zbar_pipeline.h"

struct mp_main_state {
        const struct mp_camera_config *camera;
        MPMode mode;

        int image_width;
        int image_height;

        bool gain_is_manual;
        int gain;
        int gain_max;

        bool exposure_is_manual;
        int exposure;

        bool has_auto_focus_continuous;
        bool has_auto_focus_start;
};

void mp_main_update_state(const struct mp_main_state *state);

void mp_main_set_preview(MPProcessPipelineBuffer *buffer);
void mp_main_capture_completed(GdkTexture *thumb, const char *fname);

void mp_main_set_zbar_result(MPZBarScanResult *result);

int remap(int value, int input_min, int input_max, int output_min, int output_max);
