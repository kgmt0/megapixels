#include "pipeline.h"

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <assert.h>

struct _MPPipeline {
	GMainContext *main_context;
	GMainLoop *main_loop;
	pthread_t thread;
};

static void *
thread_main_loop(void *arg)
{
	MPPipeline *pipeline = arg;

	g_main_loop_run(pipeline->main_loop);
	return NULL;
}

MPPipeline *
mp_pipeline_new()
{
	MPPipeline *pipeline = malloc(sizeof(MPPipeline));
	pipeline->main_context = g_main_context_new();
	pipeline->main_loop = g_main_loop_new(pipeline->main_context, false);
	int res =
		pthread_create(&pipeline->thread, NULL, thread_main_loop, pipeline);
	assert(res == 0);

	return pipeline;
}

struct invoke_args {
	MPPipeline *pipeline;
	MPPipelineCallback callback;
};

static bool
invoke_impl(struct invoke_args *args)
{
	args->callback(args->pipeline, args + 1);
	return false;
}

void
mp_pipeline_invoke(MPPipeline *pipeline, MPPipelineCallback callback,
		   const void *data, size_t size)
{
	if (pthread_self() != pipeline->thread) {
		struct invoke_args *args = malloc(sizeof(struct invoke_args) + size);
		args->pipeline = pipeline;
		args->callback = callback;

		if (size > 0) {
			memcpy(args + 1, data, size);
		}

		g_main_context_invoke_full(pipeline->main_context,
					   G_PRIORITY_DEFAULT,
					   (GSourceFunc)invoke_impl, args, free);
	} else {
		callback(pipeline, data);
	}
}

void
mp_pipeline_free(MPPipeline *pipeline)
{
	g_main_loop_quit(pipeline->main_loop);

	// Force the main thread loop to wake up, otherwise we might not exit
	g_main_context_wakeup(pipeline->main_context);

	void *r;
	pthread_join(pipeline->thread, &r);
	free(pipeline);
}

struct capture_source_args {
	MPCamera *camera;
	void (*callback)(MPImage, void *);
	void *user_data;
};

static bool
on_capture(int fd, GIOCondition condition, struct capture_source_args *args)
{
	mp_camera_capture_image(args->camera, args->callback, args->user_data);
	return true;
}

// Not thread safe
GSource *
mp_pipeline_add_capture_source(MPPipeline *pipeline, MPCamera *camera,
			       void (*callback)(MPImage, void *), void *user_data)
{
	int video_fd = mp_camera_get_video_fd(camera);
	GSource *video_source = g_unix_fd_source_new(video_fd, G_IO_IN);

	struct capture_source_args *args =
		malloc(sizeof(struct capture_source_args));
	args->camera = camera;
	args->callback = callback;
	args->user_data = user_data;
	g_source_set_callback(video_source, (GSourceFunc)on_capture, args, free);
	g_source_attach(video_source, pipeline->main_context);
	return video_source;
}
