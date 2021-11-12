#pragma once

#include "camera_config.h"

typedef struct _GdkSurface GdkSurface;

struct mp_process_pipeline_state {
        const struct mp_camera_config *camera;
        MPCameraMode mode;

        int burst_length;

        int preview_width;
        int preview_height;

        int device_rotation;

        bool gain_is_manual;
        int gain;
        int gain_max;

        bool exposure_is_manual;
        int exposure;

        bool has_auto_focus_continuous;
        bool has_auto_focus_start;

        bool save_dng;
};

void mp_process_pipeline_start();
void mp_process_pipeline_stop();
void mp_process_pipeline_sync();

void mp_process_pipeline_init_gl(GdkSurface *window);

void mp_process_pipeline_process_image(MPBuffer buffer);
void mp_process_pipeline_capture();
void mp_process_pipeline_update_state(const struct mp_process_pipeline_state *state);

typedef struct _MPProcessPipelineBuffer MPProcessPipelineBuffer;

void mp_process_pipeline_buffer_ref(MPProcessPipelineBuffer *buf);
void mp_process_pipeline_buffer_unref(MPProcessPipelineBuffer *buf);
uint32_t mp_process_pipeline_buffer_get_texture_id(MPProcessPipelineBuffer *buf);
