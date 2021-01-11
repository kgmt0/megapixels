#include "io_pipeline.h"

#include "device.h"
#include "camera.h"
#include "pipeline.h"
#include "process_pipeline.h"
#include <string.h>
#include <glib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>

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

	MPDevice *device;

	unsigned int interface_pad_id;

	int video_fd;
};

static struct camera_info cameras[MP_MAX_CAMERAS];

static struct device_info devices[MP_MAX_CAMERAS];
static size_t num_devices = 0;

static const struct mp_camera_config *camera = NULL;
static MPCameraMode mode;

static bool just_switched_mode = false;
static int blank_frame_count = 0;

static int burst_length;
static int captures_remaining = 0;

static int preview_width;
static int preview_height;

struct control_state {
	bool gain_is_manual;
	int gain;

	bool exposure_is_manual;
	int exposure;
};

static struct control_state desired_controls = {};
static struct control_state current_controls = {};

static bool want_focus = false;

static MPPipeline *pipeline;
static GSource *capture_source;

static void
setup_camera(MPDeviceList **device_list, const struct mp_camera_config *config)
{
	// Find device info
	size_t device_index = 0;
	for (; device_index < num_devices; ++device_index) {
		if (strcmp(config->media_dev_name,
			   devices[device_index].media_dev_name) == 0) {
			break;
		}
	}

	if (device_index == num_devices) {
		device_index = num_devices;

		// Initialize new device
		struct device_info *info = &devices[device_index];
		info->media_dev_name = config->media_dev_name;
		info->device = mp_device_list_find_remove(device_list,
							  info->media_dev_name);
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
			g_printerr("Could not open %s: %s\n", dev_name, strerror(errno));
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
		mp_device_setup_link(dev_info->device, info->pad_id,
				     dev_info->interface_pad_id, false);

		const struct media_v2_interface *interface =
			mp_device_find_entity_interface(dev_info->device,
							entity->id);

		if (!mp_find_device_path(interface->devnode, info->dev_fname, 260)) {
			g_printerr("Could not find camera device path\n");
			exit(EXIT_FAILURE);
		}

		info->fd = open(info->dev_fname, O_RDWR);
		if (info->fd == -1) {
			g_printerr("Could not open %s: %s\n", info->dev_fname, strerror(errno));
			exit(EXIT_FAILURE);
		}

		info->camera = mp_camera_new(dev_info->video_fd, info->fd);

		// Trigger continuous auto focus if the sensor supports it
		if (mp_camera_query_control(info->camera, V4L2_CID_FOCUS_AUTO,
					    NULL)) {
			info->has_auto_focus_continuous = true;
			mp_camera_control_set_bool(info->camera, V4L2_CID_FOCUS_AUTO,
						   true);
		}
		if (mp_camera_query_control(info->camera, V4L2_CID_AUTO_FOCUS_START,
					    NULL)) {
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
		.gain_is_manual = current_controls.gain_is_manual,
		.gain = current_controls.gain,
		.gain_max = info->gain_max,
		.exposure_is_manual = current_controls.exposure_is_manual,
		.exposure = current_controls.exposure,
		.has_auto_focus_continuous = info->has_auto_focus_continuous,
		.has_auto_focus_start = info->has_auto_focus_start,
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

	captures_remaining = burst_length;

	// Disable the autogain/exposure while taking the burst
	mp_camera_control_set_int32(info->camera, V4L2_CID_AUTOGAIN, 0);
	mp_camera_control_set_int32(info->camera, V4L2_CID_EXPOSURE_AUTO,
				    V4L2_EXPOSURE_MANUAL);

	// Change camera mode for capturing
	mp_camera_stop_capture(info->camera);

	mode = camera->capture_mode;
	mp_camera_set_mode(info->camera, &mode);
	just_switched_mode = true;

	mp_camera_start_capture(info->camera);

	update_process_pipeline();

	mp_process_pipeline_capture();
}

void
mp_io_pipeline_capture()
{
	mp_pipeline_invoke(pipeline, capture, NULL, 0);
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
		if (info->has_auto_focus_continuous) {
			mp_camera_control_set_bool(info->camera, V4L2_CID_FOCUS_AUTO,
						   1);
		} else if (info->has_auto_focus_start) {
			mp_camera_control_set_bool(info->camera,
						   V4L2_CID_AUTO_FOCUS_START, 1);
		}

		want_focus = false;
	}

	if (current_controls.gain_is_manual != desired_controls.gain_is_manual) {
		mp_camera_control_set_bool(info->camera, V4L2_CID_AUTOGAIN,
					   !desired_controls.gain_is_manual);
	}

	if (desired_controls.gain_is_manual &&
	    current_controls.gain != desired_controls.gain) {
		mp_camera_control_set_int32(info->camera, info->gain_ctrl,
					    desired_controls.gain);
	}

	if (current_controls.exposure_is_manual !=
	    desired_controls.exposure_is_manual) {
		mp_camera_control_set_int32(info->camera, V4L2_CID_EXPOSURE_AUTO,
					    desired_controls.exposure_is_manual ?
						    V4L2_EXPOSURE_MANUAL :
						    V4L2_EXPOSURE_AUTO);
	}

	if (desired_controls.exposure_is_manual &&
	    current_controls.exposure != desired_controls.exposure) {
		mp_camera_control_set_int32(info->camera, V4L2_CID_EXPOSURE,
					    desired_controls.exposure);
	}

	current_controls = desired_controls;
}

static void
on_frame(MPImage image, void *data)
{
	// Only update controls right after a frame was captured
	update_controls();

	// When the mode is switched while capturing we get a couple blank frames,
	// presumably from buffers made ready during the switch. Ignore these.
	if (just_switched_mode) {
		if (blank_frame_count < 20) {
			// Only check a 50x50 area
			size_t test_size =
				MIN(50, image.width) * MIN(50, image.height);

			bool image_is_blank = true;
			for (size_t i = 0; i < test_size; ++i) {
				if (image.data[i] != 0) {
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

	// Copy from the camera buffer
	size_t size =
		mp_pixel_format_width_to_bytes(image.pixel_format, image.width) *
		image.height;
	uint8_t *buffer = malloc(size);
	memcpy(buffer, image.data, size);

	image.data = buffer;

	// Send the image off for processing
	mp_process_pipeline_process_image(image);

	if (captures_remaining > 0) {
		--captures_remaining;

		if (captures_remaining == 0) {
			struct camera_info *info = &cameras[camera->index];

			// Restore the auto exposure and gain if needed
			if (!current_controls.exposure_is_manual) {
				mp_camera_control_set_int32(info->camera,
							    V4L2_CID_EXPOSURE_AUTO,
							    V4L2_EXPOSURE_AUTO);
			}

			if (!current_controls.gain_is_manual) {
				mp_camera_control_set_bool(info->camera,
							   V4L2_CID_AUTOGAIN, true);
			}

			// Go back to preview mode
			mp_camera_stop_capture(info->camera);

			mode = camera->preview_mode;
			mp_camera_set_mode(info->camera, &mode);
			just_switched_mode = true;

			mp_camera_start_capture(info->camera);

			update_process_pipeline();
		}
	}
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

			mp_camera_stop_capture(info->camera);
			mp_device_setup_link(dev_info->device, info->pad_id,
					     dev_info->interface_pad_id, false);
		}

		if (capture_source) {
			g_source_destroy(capture_source);
			capture_source = NULL;
		}

		camera = state->camera;

		if (camera) {
			struct camera_info *info = &cameras[camera->index];
			struct device_info *dev_info = &devices[info->device_index];

			mp_device_setup_link(dev_info->device, info->pad_id,
					     dev_info->interface_pad_id, true);

			mode = camera->preview_mode;
			mp_camera_set_mode(info->camera, &mode);

			mp_camera_start_capture(info->camera);
			capture_source = mp_pipeline_add_capture_source(
				pipeline, info->camera, on_frame, NULL);

			current_controls.gain_is_manual =
				mp_camera_control_get_int32(
					info->camera, V4L2_CID_EXPOSURE_AUTO) ==
				V4L2_EXPOSURE_MANUAL;
			current_controls.gain = mp_camera_control_get_int32(
				info->camera, info->gain_ctrl);

			current_controls.exposure_is_manual =
				mp_camera_control_get_bool(info->camera,
							   V4L2_CID_AUTOGAIN) == 0;
			current_controls.exposure = mp_camera_control_get_int32(
				info->camera, V4L2_CID_EXPOSURE);
		}
	}

	has_changed = has_changed || burst_length != state->burst_length ||
		      preview_width != state->preview_width ||
		      preview_height != state->preview_height;

	burst_length = state->burst_length;
	preview_width = state->preview_width;
	preview_height = state->preview_height;

	if (camera) {
		struct control_state previous_desired = desired_controls;

		desired_controls.gain_is_manual = state->gain_is_manual;
		desired_controls.gain = state->gain;
		desired_controls.exposure_is_manual = state->exposure_is_manual;
		desired_controls.exposure = state->exposure;

		has_changed =
			has_changed || memcmp(&previous_desired, &desired_controls,
					      sizeof(struct control_state)) != 0;
	}

	assert(has_changed);

	update_process_pipeline();
}

void
mp_io_pipeline_update_state(const struct mp_io_pipeline_state *state)
{
	mp_pipeline_invoke(pipeline, (MPPipelineCallback)update_state, state,
			   sizeof(struct mp_io_pipeline_state));
}
