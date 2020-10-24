#include "pipeline.h"

#include <gtk/gtk.h>
#include <glib-unix.h>
#include <assert.h>

struct _MPPipeline {
    GMainContext *main_context;
    GMainLoop *main_loop;
    pthread_t thread;
};

static void *thread_main_loop(void *arg)
{
    MPPipeline *pipeline = arg;

    g_main_loop_run(pipeline->main_loop);
    return NULL;
}

MPPipeline *mp_pipeline_new()
{
    MPPipeline *pipeline = malloc(sizeof(MPPipeline));
    pipeline->main_context = g_main_context_new();
    pipeline->main_loop = g_main_loop_new(pipeline->main_context, false);
    int res = pthread_create(
        &pipeline->thread, NULL, thread_main_loop, pipeline);
    assert(res == 0);

    return pipeline;
}

struct invoke_args {
    MPPipeline *pipeline;
    void (*callback)(MPPipeline *, void *);
};

static bool invoke_impl(struct invoke_args *args)
{
    args->callback(args->pipeline, args + 1);
    return false;
}

void mp_pipeline_invoke(MPPipeline *pipeline, MPPipelineCallback callback, void *data, size_t size)
{
    if (pthread_self() != pipeline->thread) {
        struct invoke_args *args = malloc(sizeof(struct invoke_args) + size);
        args->pipeline = pipeline;
        args->callback = callback;

        if (size > 0) {
            memcpy(args + 1, data, size);
        }

        g_main_context_invoke_full(
            pipeline->main_context,
            G_PRIORITY_DEFAULT,
            (GSourceFunc)invoke_impl,
            args,
            free);
    } else {
        callback(pipeline, data);
    }
}

void mp_pipeline_free(MPPipeline *pipeline)
{
    g_main_loop_quit(pipeline->main_loop);

    // Force the main thread loop to wake up, otherwise we might not exit
    g_main_context_wakeup(pipeline->main_context);

    void *r;
    pthread_join(pipeline->thread, &r);
    free(pipeline);
}

struct _MPPipelineCapture {
    MPPipeline *pipeline;
    MPCamera *camera;

    void (*callback)(MPImage, void *);
    void *user_data;
    GSource *video_source;
};

static bool on_capture(int fd, GIOCondition condition, MPPipelineCapture *capture)
{
    mp_camera_capture_image(capture->camera, capture->callback, capture->user_data);
    return true;
}

static void capture_start_impl(MPPipeline *pipeline, MPPipelineCapture **_capture)
{
    MPPipelineCapture *capture = *_capture;

    mp_camera_start_capture(capture->camera);

    // Start watching for new captures
    int video_fd = mp_camera_get_video_fd(capture->camera);
    capture->video_source = g_unix_fd_source_new(video_fd, G_IO_IN);
    g_source_set_callback(
        capture->video_source,
        (GSourceFunc)on_capture,
        capture,
        NULL);
    g_source_attach(capture->video_source, capture->pipeline->main_context);
}

MPPipelineCapture *mp_pipeline_capture_start(MPPipeline *pipeline, MPCamera *camera, void (*callback)(MPImage, void *), void *user_data)
{
    MPPipelineCapture *capture = malloc(sizeof(MPPipelineCapture));
    capture->pipeline = pipeline;
    capture->camera = camera;
    capture->callback = callback;
    capture->user_data = user_data;
    capture->video_source = NULL;

    mp_pipeline_invoke(pipeline, (MPPipelineCallback)capture_start_impl, &capture, sizeof(MPPipelineCapture *));

    return capture;
}

static void capture_end_impl(MPPipeline *pipeline, MPPipelineCapture **_capture)
{
    MPPipelineCapture *capture = *_capture;

    mp_camera_stop_capture(capture->camera);
    g_source_destroy(capture->video_source);

    free(capture);
}

void mp_pipeline_capture_end(MPPipelineCapture *capture)
{
    mp_pipeline_invoke(capture->pipeline, (MPPipelineCallback)capture_end_impl, &capture, sizeof(MPPipelineCapture *));
}
