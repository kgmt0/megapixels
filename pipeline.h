#pragma once

#include "camera.h"
#include "device.h"

typedef struct _MPPipeline MPPipeline;

typedef void (*MPPipelineCallback)(MPPipeline *, void *);

MPPipeline *mp_pipeline_new();
void mp_pipeline_invoke(MPPipeline *pipeline, MPPipelineCallback callback, void *data, size_t size);
void mp_pipeline_free(MPPipeline *pipeline);

typedef struct _MPPipelineCapture MPPipelineCapture;

MPPipelineCapture *mp_pipeline_capture_start(MPPipeline *pipeline, MPCamera *camera, void (*capture)(MPImage, void *), void *data);
void mp_pipeline_capture_end(MPPipelineCapture *capture);
