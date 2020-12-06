#pragma once

#include "camera.h"
#include "device.h"
#include <glib.h>

typedef struct _MPPipeline MPPipeline;

typedef void (*MPPipelineCallback)(MPPipeline *, const void *);

MPPipeline *mp_pipeline_new();
void mp_pipeline_invoke(MPPipeline *pipeline, MPPipelineCallback callback,
			const void *data, size_t size);
void mp_pipeline_free(MPPipeline *pipeline);

GSource *mp_pipeline_add_capture_source(MPPipeline *pipeline, MPCamera *camera,
					void (*callback)(MPImage, void *),
					void *user_data);
