#include "io_pipeline.h"

#include "camera.h"
#include "device.h"
#include "flash.h"
#include "pipeline.h"
#include "process_pipeline.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

struct media_link_info {
        unsigned int source_entity_id;
        unsigned int target_entity_id;
        char source_fname[260];
        char target_fname[260];
};

struct camera_info {
        size_t device_index;

        unsigned int pad_id;

        char dev_fname[260];
        int fd;

        MPCamera *camera;

        MPFlash *flash;

        int gain_ctrl;
        int gain_max;

        bool has_auto_focus_continuous;
        bool has_auto_focus_start;

        // unsigned int entity_id;
        // enum v4l2_buf_type type;

        // char media_dev_fname[260];
        // char video_dev_fname[260];
        // int media_fd;

        // struct mp_media_link media_links[MP_MAX_LINKS];
        // int num_media_links;

        // int gain_ctrl;
};

struct device_info {
        const char *media_dev_name; // owned by camera config
        const char *dev_name; // owned by camera config

        MPDevice *device;

        unsigned int interface_pad_id;

        int video_fd;
};

static struct camera_info cameras[MP_MAX_CAMERAS];

static struct device_info devices[MP_MAX_CAMERAS];
static size_t num_devices = 0;

static const struct mp_camera_config *camera = NULL;
static MPMode mode;

static bool just_switched_mode = false;
static int blank_frame_count = 0;

static int burst_length;
static int captures_remaining = 0;

static int preview_width;
static int preview_height;

static int device_rotation;

struct control_state {
        bool gain_is_manual;
        int gain;

        bool exposure_is_manual;
        int exposure;
};

static struct control_state desired_controls = {};
static struct control_state current_controls = {};

static bool flash_enabled = false;

static bool want_focus = false;

static MPPipeline *pipeline;
static GSource *capture_source;

static void
mp_setup_media_link_pad_formats(struct device_info *dev_info,
                                const struct mp_media_link_config media_links[],
                                int num_media_links,
                                MPMode *mode)
{
        const struct media_v2_entity *entities[2];
        int ports[2];
        for (int i = 0; i < num_media_links; i++) {
                entities[0] = mp_device_find_entity(
                        dev_info->device, (const char *)media_links[i].source_name);
                entities[1] = mp_device_find_entity(
                        dev_info->device, (const char *)media_links[i].target_name);
                ports[0] = media_links[i].source_port;
                ports[1] = media_links[i].target_port;

                for (int j = 0; j < 2; j++)
                        if (!mp_entity_pad_set_format(
                                    dev_info->device, entities[j], ports[j], mode)) {
                                g_printerr("Failed to set %s:%d format\n",
                                           entities[j]->name,
                                           ports[j]);
                                exit(EXIT_FAILURE);
                        }
        }
}

static void
setup_camera(MPDeviceList **device_list, const struct mp_camera_config *config)
{
        // Find device info
        size_t device_index = 0;
        for (; device_index < num_devices; ++device_index) {
                if ((strcmp(config->media_dev_name,
                            devices[device_index].media_dev_name) == 0) &&
                    (strcmp(config->dev_name, devices[device_index].dev_name) ==
                     0)) {
                        break;
                }
        }

        if (device_index == num_devices) {
                device_index = num_devices;

                // Initialize new device
                struct device_info *info = &devices[device_index];
                info->media_dev_name = config->media_dev_name;
                info->dev_name = config->dev_name;
                info->device = mp_device_list_find_remove(
                        device_list, info->media_dev_name, info->dev_name);
                if (!info->device) {
                        g_printerr("Could not find /dev/media* node matching '%s'\n",
                                   info->media_dev_name);
                        exit(EXIT_FAILURE);
                }

                const struct media_v2_entity *entity =
                        mp_device_find_entity_type(info->device, MEDIA_ENT_F_IO_V4L);
                if (!entity) {
                        g_printerr("Could not find device video entity\n");
                        exit(EXIT_FAILURE);
                }

                const struct media_v2_pad *pad =
                        mp_device_get_pad_from_entity(info->device, entity->id);
                info->interface_pad_id = pad->id;

                const struct media_v2_interface *interface =
                        mp_device_find_entity_interface(info->device, entity->id);
                char dev_name[260];
                if (!mp_find_device_path(interface->devnode, dev_name, 260)) {
                        g_printerr("Could not find video path\n");
                        exit(EXIT_FAILURE);
                }

                info->video_fd = open(dev_name, O_RDWR);
                if (info->video_fd == -1) {
                        g_printerr("Could not open %s: %s\n",
                                   dev_name,
                                   strerror(errno));
                        exit(EXIT_FAILURE);
                }

                ++num_devices;
        }

        {
                struct camera_info *info = &cameras[config->index];
                struct device_info *dev_info = &devices[device_index];

                info->device_index = device_index;

                const struct media_v2_entity *entity =
                        mp_device_find_entity(dev_info->device, config->dev_name);
                if (!entity) {
                        g_printerr("Could not find camera entity matching '%s'\n",
                                   config->dev_name);
                        exit(EXIT_FAILURE);
                }

                const struct media_v2_pad *pad =
                        mp_device_get_pad_from_entity(dev_info->device, entity->id);

                info->pad_id = pad->id;

                // Make sure the camera starts out as disabled
                mp_device_setup_link(dev_info->device,
                                     info->pad_id,
                                     dev_info->interface_pad_id,
                                     false);

                const struct media_v2_interface *interface =
                        mp_device_find_entity_interface(dev_info->device,
                                                        entity->id);

                if (!mp_find_device_path(interface->devnode, info->dev_fname, 260)) {
                        g_printerr("Could not find camera device path\n");
                        exit(EXIT_FAILURE);
                }

                info->fd = open(info->dev_fname, O_RDWR);
                if (info->fd == -1) {
                        g_printerr("Could not open %s: %s\n",
                                   info->dev_fname,
                                   strerror(errno));
                        exit(EXIT_FAILURE);
                }

                info->camera = mp_camera_new(dev_info->video_fd, info->fd);

                // Start with the capture format, this works around a bug with
                // the ov5640 driver where it won't allow setting the preview
                // format initially.
                MPMode mode = config->capture_mode;
                if (config->num_media_links)
                        mp_setup_media_link_pad_formats(dev_info,
                                                        config->media_links,
                                                        config->num_media_links,
                                                        &mode);
                mp_camera_set_mode(info->camera, &mode);

                // Trigger continuous auto focus if the sensor supports it
                if (mp_camera_query_control(
                            info->camera, V4L2_CID_FOCUS_AUTO, NULL)) {
                        info->has_auto_focus_continuous = true;
                        mp_camera_control_set_bool_bg(
                                info->camera, V4L2_CID_FOCUS_AUTO, true);
                }
                if (mp_camera_query_control(
                            info->camera, V4L2_CID_AUTO_FOCUS_START, NULL)) {
                        info->has_auto_focus_start = true;
                }

                MPControl control;
                if (mp_camera_query_control(info->camera, V4L2_CID_GAIN, &control)) {
                        info->gain_ctrl = V4L2_CID_GAIN;
                        info->gain_max = control.max;
                } else if (mp_camera_query_control(
                                   info->camera, V4L2_CID_ANALOGUE_GAIN, &control)) {
                        info->gain_ctrl = V4L2_CID_ANALOGUE_GAIN;
                        info->gain_max = control.max;
                }

                // Setup flash
                if (config->flash_path[0]) {
                        info->flash = mp_led_flash_from_path(config->flash_path);
                } else if (config->flash_display) {
                        info->flash = mp_create_display_flash();
                } else {
                        info->flash = NULL;
                }
        }
}

static void
setup(MPPipeline *pipeline, const void *data)
{
        MPDeviceList *device_list = mp_device_list_new();

        for (size_t i = 0; i < MP_MAX_CAMERAS; ++i) {
                const struct mp_camera_config *config = mp_get_camera_config(i);
                if (!config) {
                        break;
                }

                setup_camera(&device_list, config);
        }

        mp_device_list_free(device_list);
}

static void
clean_cameras()
{
        for (size_t i = 0; i < MP_MAX_CAMERAS; ++i) {
                struct camera_info *info = &cameras[i];
                if (info->camera) {
                        mp_camera_free(info->camera);
                        info->camera = NULL;
                }
        }
}

void
mp_io_pipeline_start()
{
        mp_process_pipeline_start();

        pipeline = mp_pipeline_new();

        mp_pipeline_invoke(pipeline, setup, NULL, 0);
}

void
mp_io_pipeline_stop()
{
        if (capture_source) {
                g_source_destroy(capture_source);
        }

        clean_cameras();

        mp_pipeline_free(pipeline);

        mp_process_pipeline_stop();
}

static void
update_process_pipeline()
{
        struct camera_info *info = &cameras[camera->index];

        // Grab the latest control values
        if (!current_controls.gain_is_manual) {
                current_controls.gain =
                        mp_camera_control_get_int32(info->camera, info->gain_ctrl);
        }
        if (!current_controls.exposure_is_manual) {
                current_controls.exposure =
                        mp_camera_control_get_int32(info->camera, V4L2_CID_EXPOSURE);
        }

        struct mp_process_pipeline_state pipeline_state = {
                .camera = camera,
                .mode = mode,
                .burst_length = burst_length,
                .preview_width = preview_width,
                .preview_height = preview_height,
                .device_rotation = device_rotation,
                .gain_is_manual = current_controls.gain_is_manual,
                .gain = current_controls.gain,
                .gain_max = info->gain_max,
                .exposure_is_manual = current_controls.exposure_is_manual,
                .exposure = current_controls.exposure,
                .has_auto_focus_continuous = info->has_auto_focus_continuous,
                .has_auto_focus_start = info->has_auto_focus_start,
                .flash_enabled = flash_enabled,
        };
        mp_process_pipeline_update_state(&pipeline_state);
}

static void
focus(MPPipeline *pipeline, const void *data)
{
        want_focus = true;
}

void
mp_io_pipeline_focus()
{
        mp_pipeline_invoke(pipeline, focus, NULL, 0);
}

static void
capture(MPPipeline *pipeline, const void *data)
{
        struct camera_info *info = &cameras[camera->index];
        struct device_info *dev_info = &devices[info->device_index];
        uint32_t gain;
        float gain_norm;

        // Disable the autogain/exposure while taking the burst
        mp_camera_control_set_int32(info->camera, V4L2_CID_AUTOGAIN, 0);
        mp_camera_control_set_int32(
                info->camera, V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL);

        // Get current gain to calculate a burst length;
        // with low gain there's 2, with the max automatic gain of the ov5640
        // the value seems to be 248 which creates a 5 frame burst
        // for manual gain you can go up to 11 frames
        gain = mp_camera_control_get_int32(info->camera, V4L2_CID_GAIN);
        gain_norm = (float)gain / (float)info->gain_max;
        burst_length = (int)fmax(sqrt(gain_norm) * 10, 1) + 1;
        captures_remaining = burst_length;

        // Change camera mode for capturing
        mp_process_pipeline_sync();
        mp_camera_stop_capture(info->camera);

        mode = camera->capture_mode;
        if (camera->num_media_links)
                mp_setup_media_link_pad_formats(dev_info,
                                                camera->media_links,
                                                camera->num_media_links,
                                                &mode);
        mp_camera_set_mode(info->camera, &mode);
        just_switched_mode = true;

        mp_camera_start_capture(info->camera);

        // Enable flash
        if (info->flash && flash_enabled) {
                mp_flash_enable(info->flash);
        }

        update_process_pipeline();

        mp_process_pipeline_capture();
}

void
mp_io_pipeline_capture()
{
        mp_pipeline_invoke(pipeline, capture, NULL, 0);
}

static void
release_buffer(MPPipeline *pipeline, const uint32_t *buffer_index)
{
        struct camera_info *info = &cameras[camera->index];

        mp_camera_release_buffer(info->camera, *buffer_index);
}

void
mp_io_pipeline_release_buffer(uint32_t buffer_index)
{
        mp_pipeline_invoke(pipeline,
                           (MPPipelineCallback)release_buffer,
                           &buffer_index,
                           sizeof(uint32_t));
}

static pid_t focus_continuous_task = 0;
static pid_t start_focus_task = 0;
static void
start_focus(struct camera_info *info)
{
        // only run 1 manual focus at once
        if (!mp_camera_check_task_complete(info->camera, start_focus_task) ||
            !mp_camera_check_task_complete(info->camera, focus_continuous_task))
                return;

        if (info->has_auto_focus_continuous) {
                focus_continuous_task = mp_camera_control_set_bool_bg(
                        info->camera, V4L2_CID_FOCUS_AUTO, 1);
        } else if (info->has_auto_focus_start) {
                start_focus_task = mp_camera_control_set_bool_bg(
                        info->camera, V4L2_CID_AUTO_FOCUS_START, 1);
        }
}

static void
update_controls()
{
        // Don't update controls while capturing
        if (captures_remaining > 0) {
                return;
        }

        struct camera_info *info = &cameras[camera->index];

        if (want_focus) {
                start_focus(info);
                want_focus = false;
        }

        if (current_controls.gain_is_manual != desired_controls.gain_is_manual) {
                mp_camera_control_set_bool_bg(info->camera,
                                              V4L2_CID_AUTOGAIN,
                                              !desired_controls.gain_is_manual);
        }

        if (desired_controls.gain_is_manual &&
            current_controls.gain != desired_controls.gain) {
                mp_camera_control_set_int32_bg(
                        info->camera, info->gain_ctrl, desired_controls.gain);
        }

        if (current_controls.exposure_is_manual !=
            desired_controls.exposure_is_manual) {
                mp_camera_control_set_int32_bg(info->camera,
                                               V4L2_CID_EXPOSURE_AUTO,
                                               desired_controls.exposure_is_manual ?
                                                       V4L2_EXPOSURE_MANUAL :
                                                       V4L2_EXPOSURE_AUTO);
        }

        if (desired_controls.exposure_is_manual &&
            current_controls.exposure != desired_controls.exposure) {
                mp_camera_control_set_int32_bg(
                        info->camera, V4L2_CID_EXPOSURE, desired_controls.exposure);
        }

        current_controls = desired_controls;
}

static void
on_frame(MPBuffer buffer, void *_data)
{
        // Only update controls right after a frame was captured
        update_controls();

        // When the mode is switched while capturing we get a couple blank frames,
        // presumably from buffers made ready during the switch. Ignore these.
        if (just_switched_mode) {
                if (blank_frame_count < 20) {
                        // Only check a 10x10 area
                        size_t test_size =
                                MIN(10, mode.width) * MIN(10, mode.height);

                        bool image_is_blank = true;
                        for (size_t i = 0; i < test_size; ++i) {
                                if (buffer.data[i] != 0) {
                                        image_is_blank = false;
                                }
                        }

                        if (image_is_blank) {
                                ++blank_frame_count;
                                return;
                        }
                } else {
                        printf("Blank image limit reached, resulting capture may be blank\n");
                }

                just_switched_mode = false;
                blank_frame_count = 0;
        }

        // Send the image off for processing
        mp_process_pipeline_process_image(buffer);

        if (captures_remaining > 0) {
                --captures_remaining;

                if (captures_remaining == 0) {
                        struct camera_info *info = &cameras[camera->index];
                        struct device_info *dev_info = &devices[info->device_index];

                        // Restore the auto exposure and gain if needed
                        if (!current_controls.exposure_is_manual) {
                                mp_camera_control_set_int32_bg(
                                        info->camera,
                                        V4L2_CID_EXPOSURE_AUTO,
                                        V4L2_EXPOSURE_AUTO);
                        }

                        if (!current_controls.gain_is_manual) {
                                mp_camera_control_set_bool_bg(
                                        info->camera, V4L2_CID_AUTOGAIN, true);
                        }

                        // Go back to preview mode
                        mp_process_pipeline_sync();
                        mp_camera_stop_capture(info->camera);

                        mode = camera->preview_mode;
                        if (camera->num_media_links)
                                mp_setup_media_link_pad_formats(
                                        dev_info,
                                        camera->media_links,
                                        camera->num_media_links,
                                        &mode);
                        mp_camera_set_mode(info->camera, &mode);
                        just_switched_mode = true;

                        mp_camera_start_capture(info->camera);

                        // Disable flash
                        if (info->flash && flash_enabled) {
                                mp_flash_disable(info->flash);
                        }

                        update_process_pipeline();
                }
        }
}

static void
mp_setup_media_link(struct device_info *dev_info,
                    const struct mp_media_link_config *cfg,
                    bool enable)
{
        const struct media_v2_entity *source_entity =
                mp_device_find_entity(dev_info->device, cfg->source_name);

        const struct media_v2_entity *target_entity =
                mp_device_find_entity(dev_info->device, cfg->target_name);

        mp_device_setup_entity_link(dev_info->device,
                                    source_entity->id,
                                    target_entity->id,
                                    cfg->source_port,
                                    cfg->target_port,
                                    enable);
}

static void
update_state(MPPipeline *pipeline, const struct mp_io_pipeline_state *state)
{
        // Make sure the state isn't updated more than it needs to be by checking
        // whether this state change actually changes anything.
        bool has_changed = false;

        if (camera != state->camera) {
                has_changed = true;

                if (camera) {
                        struct camera_info *info = &cameras[camera->index];
                        struct device_info *dev_info = &devices[info->device_index];

                        mp_process_pipeline_sync();
                        mp_camera_stop_capture(info->camera);
                        mp_device_setup_link(dev_info->device,
                                             info->pad_id,
                                             dev_info->interface_pad_id,
                                             false);

                        // Disable media links
                        for (int i = 0; i < camera->num_media_links; i++)
                                mp_setup_media_link(
                                        dev_info, &camera->media_links[i], false);
                }

                if (capture_source) {
                        g_source_destroy(capture_source);
                        capture_source = NULL;
                }

                camera = state->camera;

                if (camera) {
                        struct camera_info *info = &cameras[camera->index];
                        struct device_info *dev_info = &devices[info->device_index];

                        mp_device_setup_link(dev_info->device,
                                             info->pad_id,
                                             dev_info->interface_pad_id,
                                             true);

                        // Enable media links
                        for (int i = 0; i < camera->num_media_links; i++)
                                mp_setup_media_link(
                                        dev_info, &camera->media_links[i], true);

                        mode = camera->preview_mode;
                        if (camera->num_media_links)
                                mp_setup_media_link_pad_formats(
                                        dev_info,
                                        camera->media_links,
                                        camera->num_media_links,
                                        &mode);
                        mp_camera_set_mode(info->camera, &mode);

                        mp_camera_start_capture(info->camera);
                        capture_source = mp_pipeline_add_capture_source(
                                pipeline, info->camera, on_frame, NULL);

                        current_controls.gain_is_manual =
                                mp_camera_control_get_bool(info->camera,
                                                           V4L2_CID_AUTOGAIN) == 0;
                        current_controls.gain = mp_camera_control_get_int32(
                                info->camera, info->gain_ctrl);

                        current_controls.exposure_is_manual =
                                mp_camera_control_get_int32(
                                        info->camera, V4L2_CID_EXPOSURE_AUTO) ==
                                V4L2_EXPOSURE_MANUAL;
                        current_controls.exposure = mp_camera_control_get_int32(
                                info->camera, V4L2_CID_EXPOSURE);
                }
        }

        has_changed = has_changed || burst_length != state->burst_length ||
                      preview_width != state->preview_width ||
                      preview_height != state->preview_height ||
                      device_rotation != state->device_rotation;

        burst_length = state->burst_length;
        preview_width = state->preview_width;
        preview_height = state->preview_height;
        device_rotation = state->device_rotation;

        if (camera) {
                struct control_state previous_desired = desired_controls;

                desired_controls.gain_is_manual = state->gain_is_manual;
                desired_controls.gain = state->gain;
                desired_controls.exposure_is_manual = state->exposure_is_manual;
                desired_controls.exposure = state->exposure;

                has_changed = has_changed ||
                              memcmp(&previous_desired,
                                     &desired_controls,
                                     sizeof(struct control_state)) != 0 ||
                              flash_enabled != state->flash_enabled;

                flash_enabled = state->flash_enabled;
        }

        assert(has_changed);

        update_process_pipeline();
}

void
mp_io_pipeline_update_state(const struct mp_io_pipeline_state *state)
{
        mp_pipeline_invoke(pipeline,
                           (MPPipelineCallback)update_state,
                           state,
                           sizeof(struct mp_io_pipeline_state));
}
