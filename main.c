#include "main.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <linux/media.h>
#include <linux/v4l2-subdev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <assert.h>
#include <limits.h>
#include <linux/kdev_t.h>
#include <sys/sysmacros.h>
#include <asm/errno.h>
#include <wordexp.h>
#include <gtk/gtk.h>
#include <locale.h>
#include "camera_config.h"
#include "quickpreview.h"
#include "io_pipeline.h"

enum user_control {
	USER_CONTROL_ISO,
	USER_CONTROL_SHUTTER
};

static bool camera_is_initialized = false;
static const struct mp_camera_config *camera = NULL;
static MPCameraMode mode;

static int preview_width = -1;
static int preview_height = -1;

static bool gain_is_manual = false;
static int gain;
static int gain_max;

static bool exposure_is_manual = false;
static int exposure;

static bool has_auto_focus_continuous;
static bool has_auto_focus_start;

static cairo_surface_t *surface = NULL;
static cairo_surface_t *status_surface = NULL;
static char last_path[260] = "";

static int burst_length = 3;

static enum user_control current_control;

// Widgets
GtkWidget *preview;
GtkWidget *error_box;
GtkWidget *error_message;
GtkWidget *main_stack;
GtkWidget *thumb_last;
GtkWidget *control_box;
GtkWidget *control_name;
GtkAdjustment *control_slider;
GtkWidget *control_auto;

int
remap(int value, int input_min, int input_max, int output_min, int output_max)
{
	const long long factor = 1000000000;
	long long output_spread = output_max - output_min;
	long long input_spread = input_max - input_min;

	long long zero_value = value - input_min;
	zero_value *= factor;
	long long percentage = zero_value / input_spread;

	long long zero_output = percentage * output_spread / factor;

	long long result = output_min + zero_output;
	return (int)result;
}

static void
update_io_pipeline()
{
	struct mp_io_pipeline_state io_state = {
		.camera = camera,
		.burst_length = burst_length,
		.preview_width = preview_width,
		.preview_height = preview_height,
		.gain_is_manual = gain_is_manual,
		.gain = gain,
		.exposure_is_manual = exposure_is_manual,
		.exposure = exposure,
	};
	mp_io_pipeline_update_state(&io_state);
}

static bool
update_state(const struct mp_main_state *state)
{
	if (!camera_is_initialized) {
		camera_is_initialized = true;
	}

	if (camera == state->camera) {
		mode = state->mode;

		if (!gain_is_manual) {
			gain = state->gain;
		}
		gain_max = state->gain_max;

		if (!exposure_is_manual) {
			exposure = state->exposure;
		}

		has_auto_focus_continuous = state->has_auto_focus_continuous;
		has_auto_focus_start = state->has_auto_focus_start;
	}

	return false;
}

void mp_main_update_state(const struct mp_main_state *state)
{
	struct mp_main_state *state_copy = malloc(sizeof(struct mp_main_state));
	*state_copy = *state;

	g_main_context_invoke_full(
		g_main_context_default(),
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc)update_state,
		state_copy,
		free);
}

static bool
set_preview(cairo_surface_t *image)
{
	if (surface) {
		cairo_surface_destroy(surface);
	}
	surface = image;
	gtk_widget_queue_draw(preview);
	return false;
}

void mp_main_set_preview(cairo_surface_t *image)
{
	g_main_context_invoke_full(
		g_main_context_default(),
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc)set_preview,
		image,
		NULL);
}

static bool
capture_completed(const char *fname)
{
	return false;
}

void mp_main_capture_completed(const char *fname)
{
	gchar *name = g_strdup(fname);

	g_main_context_invoke_full(
		g_main_context_default(),
		G_PRIORITY_DEFAULT_IDLE,
		(GSourceFunc)capture_completed,
		name,
		g_free);
}

// static void
// start_capturing(int fd)
// {
// 	for (int i = 0; i < n_buffers; ++i) {
// 		struct v4l2_buffer buf = {
// 			.type = current.type,
// 			.memory = V4L2_MEMORY_MMAP,
// 			.index = i,
// 		};

// 		if(current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
// 			buf.m.planes = buf_planes;
// 			buf.length = 1;
// 		}

// 		if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
// 			errno_exit("VIDIOC_QBUF");
// 		}
// 	}

// 	if (xioctl(fd, VIDIOC_STREAMON, &current.type) == -1) {
// 		errno_exit("VIDIOC_STREAMON");
// 	}

// 	ready = 1;
// }

// static void
// stop_capturing(int fd)
// {
// 	int i;
// 	ready = 0;
// 	printf("Stopping capture\n");

// 	if (xioctl(fd, VIDIOC_STREAMOFF, &current.type) == -1) {
// 		errno_exit("VIDIOC_STREAMOFF");
// 	}

// 	for (i = 0; i < n_buffers; ++i) {
// 		munmap(buffers[i].start, buffers[i].length);
// 	}

// }

// static void
// init_mmap(int fd)
// {
// 	struct v4l2_requestbuffers req = {
// 		.count = 4,
// 		.type = current.type,
// 		.memory = V4L2_MEMORY_MMAP,
// 	};

// 	if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
// 		if (errno == EINVAL) {
// 			fprintf(stderr, "%s does not support memory mapping",
// 				current.dev_name);
// 			exit(EXIT_FAILURE);
// 		} else {
// 			errno_exit("VIDIOC_REQBUFS");
// 		}
// 	}

// 	if (req.count < 2) {
// 		fprintf(stderr, "Insufficient buffer memory on %s\n",
// 			current.dev_name);
// 		exit(EXIT_FAILURE);
// 	}

// 	buffers = calloc(req.count, sizeof(buffers[0]));

// 	if (!buffers) {
// 		fprintf(stderr, "Out of memory\\n");
// 		exit(EXIT_FAILURE);
// 	}

// 	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
// 		struct v4l2_buffer buf = {
// 			.type = current.type,
// 			.memory = V4L2_MEMORY_MMAP,
// 			.index = n_buffers,
// 		};

// 		if (current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
// 			buf.m.planes = buf_planes;
// 			buf.length = 1;
// 		}

// 		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
// 			errno_exit("VIDIOC_QUERYBUF");
// 		}

// 		if (current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
// 			buffers[n_buffers].length = buf.m.planes[0].length;
// 			buffers[n_buffers].start = mmap(NULL /* start anywhere */,
// 					buf.m.planes[0].length,
// 					PROT_READ | PROT_WRITE /* required */,
// 					MAP_SHARED /* recommended */,
// 					fd, buf.m.planes[0].m.mem_offset);
// 		} else {
// 			buffers[n_buffers].length = buf.length;
// 			buffers[n_buffers].start = mmap(NULL /* start anywhere */,
// 					buf.length,
// 					PROT_READ | PROT_WRITE /* required */,
// 					MAP_SHARED /* recommended */,
// 					fd, buf.m.offset);
// 		}

// 		if (MAP_FAILED == buffers[n_buffers].start) {
// 			errno_exit("mmap");
// 		}
// 	}
// }


static void
draw_controls()
{
	cairo_t *cr;
	char iso[6];
	int temp;
	char shutterangle[6];

	if (exposure_is_manual) {
		temp = (int)((float)exposure / (float)camera->capture_mode.height * 360);
		sprintf(shutterangle, "%d\u00b0", temp);
	} else {
		sprintf(shutterangle, "auto");
	}

	if (gain_is_manual) {
		temp = remap(gain - 1, 0, gain_max, camera->iso_min, camera->iso_max);
		sprintf(iso, "%d", temp);
	} else {
		sprintf(iso, "auto");
	}

	if (status_surface)
		cairo_surface_destroy(status_surface);

	// Make a service to show status of controls, 32px high
	if (gtk_widget_get_window(preview) == NULL) {
		return;
	}
	status_surface = gdk_window_create_similar_surface(gtk_widget_get_window(preview),
		CAIRO_CONTENT_COLOR_ALPHA,
		preview_width, 32);

	cr = cairo_create(status_surface);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.0);
	cairo_paint(cr);

	// Draw the outlines for the headings
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 9);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);

	cairo_move_to(cr, 16, 16);
	cairo_text_path(cr, "ISO");
	cairo_stroke(cr);

	cairo_move_to(cr, 60, 16);
	cairo_text_path(cr, "Shutter");
	cairo_stroke(cr);

	// Draw the fill for the headings
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, 16, 16);
	cairo_show_text(cr, "ISO");
	cairo_move_to(cr, 60, 16);
	cairo_show_text(cr, "Shutter");

	// Draw the outlines for the values
	cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 11);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);

	cairo_move_to(cr, 16, 26);
	cairo_text_path(cr, iso);
	cairo_stroke(cr);

	cairo_move_to(cr, 60, 26);
	cairo_text_path(cr, shutterangle);
	cairo_stroke(cr);

	// Draw the fill for the values
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, 16, 26);
	cairo_show_text(cr, iso);
	cairo_move_to(cr, 60, 26);
	cairo_show_text(cr, shutterangle);

	cairo_destroy(cr);
	
	gtk_widget_queue_draw_area(preview, 0, 0, preview_width, 32);
}

// static void
// init_sensor(char *fn, int width, int height, int mbus, int rate)
// {
// 	int fd;
// 	struct v4l2_subdev_frame_interval interval = {};
// 	struct v4l2_subdev_format fmt = {};
// 	fd = open(fn, O_RDWR);

// 	g_print("Setting sensor rate to %d\n", rate);
// 	interval.pad = 0;
// 	interval.interval.numerator = 1;
// 	interval.interval.denominator = rate;

// 	if (xioctl(fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &interval) == -1) {
// 		errno_exit("VIDIOC_SUBDEV_S_FRAME_INTERVAL");
// 	}

// 	if (interval.interval.numerator != 1 || interval.interval.denominator != rate)
// 		g_printerr("Driver chose %d/%d instead\n",
// 			interval.interval.numerator, interval.interval.denominator);

// 	g_print("Setting sensor to %dx%d fmt %d\n",
// 		width, height, mbus);
// 	fmt.pad = 0;
// 	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
// 	fmt.format.code = mbus;
// 	fmt.format.width = width;
// 	fmt.format.height = height;
// 	fmt.format.field = V4L2_FIELD_ANY;

// 	if (xioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) == -1) {
// 		errno_exit("VIDIOC_SUBDEV_S_FMT");
// 	}
// 	if (fmt.format.width != width || fmt.format.height != height || fmt.format.code != mbus)
// 		g_printerr("Driver chose %dx%d fmt %d instead\n",
// 			fmt.format.width, fmt.format.height,
// 			fmt.format.code);
	

// 	// Trigger continuous auto focus if the sensor supports it
// 	if (v4l2_has_control(fd, V4L2_CID_FOCUS_AUTO)) {
// 		current.has_af_c = 1;
// 		v4l2_ctrl_set(fd, V4L2_CID_FOCUS_AUTO, 1);
// 	}
// 	if (v4l2_has_control(fd, V4L2_CID_AUTO_FOCUS_START)) {
// 		current.has_af_s = 1;
// 	}

// 	if (v4l2_has_control(fd, V4L2_CID_GAIN)) {
// 		current.gain_ctrl = V4L2_CID_GAIN;
// 		current.gain_max = v4l2_ctrl_get_max(fd, V4L2_CID_GAIN);
// 	}

// 	if (v4l2_has_control(fd, V4L2_CID_ANALOGUE_GAIN)) {
// 		current.gain_ctrl = V4L2_CID_ANALOGUE_GAIN;
// 		current.gain_max = v4l2_ctrl_get_max(fd, V4L2_CID_ANALOGUE_GAIN);
// 	}

// 	auto_exposure = 1;
// 	auto_gain = 1;
// 	draw_controls();

// 	close(current.fd);
// 	current.fd = fd;
// }

// static void
// init_media_entity(char *fn, int width, int height, int mbus)
// {
// 	int fd;
// 	struct v4l2_subdev_format fmt = {};

// 	fd = open(fn, O_RDWR);

// 	// Apply mode to v4l2 subdev
// 	g_print("Setting node to %dx%d fmt %d\n",
// 		width, height, mbus);
// 	fmt.pad = 0;
// 	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
// 	fmt.format.code = mbus;
// 	fmt.format.width = width;
// 	fmt.format.height = height;
// 	fmt.format.field = V4L2_FIELD_ANY;

// 	if (xioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) == -1) {
// 		errno_exit("VIDIOC_SUBDEV_S_FMT");
// 	}
// 	if (fmt.format.width != width || fmt.format.height != height || fmt.format.code != mbus)
// 		g_printerr("Driver chose %dx%d fmt %d instead\n",
// 			fmt.format.width, fmt.format.height,
// 			fmt.format.code);
// }

// static int
// init_device(int fd)
// {
// 	struct v4l2_capability cap;
// 	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
// 		if (errno == EINVAL) {
// 			fprintf(stderr, "%s is no V4L2 device\n",
// 				current.dev_name);
// 			exit(EXIT_FAILURE);
// 		} else {
// 			errno_exit("VIDIOC_QUERYCAP");
// 		}
// 	}

// 	// Detect buffer format for the interface node, preferring normal video capture
// 	if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
// 		current.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
// 	} else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
// 		printf("[%s] Using the MPLANE buffer format\n", current.cfg_name);
// 		current.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
// 	} else {
// 		fprintf(stderr, "%s is no video capture device\n",
// 			current.dev_name);
// 		exit(EXIT_FAILURE);
// 	}

// 	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
// 		fprintf(stderr, "%s does not support streaming i/o\n",
// 			current.dev_name);
// 		exit(EXIT_FAILURE);
// 	}

// 	/* Select video input, video standard and tune here. */
// 	struct v4l2_cropcap cropcap = {
// 		.type = current.type,
// 	};

// 	struct v4l2_crop crop = {0};
// 	if (xioctl(fd, VIDIOC_CROPCAP, &cropcap) == 0) {
// 		crop.type = current.type;
// 		crop.c = cropcap.defrect; /* reset to default */

// 		if (xioctl(fd, VIDIOC_S_CROP, &crop) == -1) {
// 			switch (errno) {
// 				case EINVAL:
// 					/* Cropping not supported. */
// 					break;
// 				default:
// 					/* Errors ignored. */
// 					break;
// 			}
// 		}
// 	} else {
// 		/* Errors ignored. */
// 	}

// 	// Request a video format
// 	struct v4l2_format fmt = {
// 		.type = current.type,
// 	};
// 	if (current.width > 0) {
// 		g_print("Setting camera to %dx%d fmt %d\n",
// 			current.width, current.height, current.fmt);

// 		if (current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
// 			fmt.fmt.pix_mp.width = current.width;
// 			fmt.fmt.pix_mp.height = current.height;
// 			fmt.fmt.pix_mp.pixelformat = current.fmt;
// 			fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
// 		} else {
// 			fmt.fmt.pix.width = current.width;
// 			fmt.fmt.pix.height = current.height;
// 			fmt.fmt.pix.pixelformat = current.fmt;
// 			fmt.fmt.pix.field = V4L2_FIELD_ANY;
// 		}

// 		if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
// 			g_printerr("VIDIOC_S_FMT failed");
// 			show_error("Could not set camera mode");
// 			return -1;
// 		}

// 		if (current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
// 			&& (fmt.fmt.pix_mp.width != current.width ||
// 			fmt.fmt.pix_mp.height != current.height ||
// 			fmt.fmt.pix_mp.pixelformat != current.fmt))
// 			g_printerr("Driver returned %dx%d fmt %d\n",
// 				fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
// 				fmt.fmt.pix_mp.pixelformat);
// 		if (current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE
// 			&& (fmt.fmt.pix.width != current.width ||
// 			fmt.fmt.pix.height != current.height ||
// 			fmt.fmt.pix.pixelformat != current.fmt))
// 			g_printerr("Driver returned %dx%d fmt %d\n",
// 				fmt.fmt.pix.width, fmt.fmt.pix.height,
// 				fmt.fmt.pix.pixelformat);


// 		/* Note VIDIOC_S_FMT may change width and height. */
// 	} else {
// 		if (xioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
// 			errno_exit("VIDIOC_G_FMT");
// 		}
// 		if (current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
// 			g_print("Got %dx%d fmt %d from the driver\n",
// 				fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
// 				fmt.fmt.pix_mp.pixelformat);
// 			current.width = fmt.fmt.pix.width;
// 			current.height = fmt.fmt.pix.height;
// 		} else {
// 			g_print("Got %dx%d fmt %d from the driver\n",
// 				fmt.fmt.pix.width, fmt.fmt.pix.height,
// 				fmt.fmt.pix.pixelformat);
// 			current.width = fmt.fmt.pix_mp.width;
// 			current.height = fmt.fmt.pix_mp.height;
// 		}
// 	}
// 	if (current.type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
// 		current.fmt = fmt.fmt.pix_mp.pixelformat;
// 	} else {
// 		current.fmt = fmt.fmt.pix.pixelformat;
// 	}

// 	init_mmap(fd);
// 	return 0;
// }

static gboolean
preview_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	if (!camera_is_initialized) {
		return FALSE;
	}

	if (surface) {
		cairo_save(cr);

		cairo_translate(cr, preview_width / 2, preview_height / 2);

		int width = cairo_image_surface_get_width(surface);
		int height = cairo_image_surface_get_height(surface);
		double scale = MIN(preview_width / (double) width, preview_height / (double) height);
		cairo_scale(cr, scale, scale);

		cairo_translate(cr, -width / 2, -height / 2);

		cairo_set_source_surface(cr, surface, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	}

	cairo_set_source_surface(cr, status_surface, 0, 0);
	cairo_paint(cr);
	return FALSE;
}


static gboolean
preview_configure(GtkWidget *widget, GdkEventConfigure *event)
{
	int new_preview_width = gtk_widget_get_allocated_width(widget);
	int new_preview_height = gtk_widget_get_allocated_height(widget);

	if (preview_width != new_preview_width || preview_height != new_preview_height)
	{
		preview_width = new_preview_width;
		preview_height = new_preview_height;
		update_io_pipeline();
	}

	draw_controls();

	return TRUE;
}

// int
// setup_camera(int cid)
// {
// 	struct media_link_desc link = {0};
	
// 	// Kill existing links for cameras in the same graph
// 	for(int i=0; i<NUM_CAMERAS; i++) {
// 		if(!cameras[i].exists)
// 			continue;
// 		if(i == cid)
// 			continue;
// 		if(strcmp(cameras[i].media_dev_fname, cameras[cid].media_dev_fname) != 0)
// 			continue;

// 		// Disable the interface<->front link
// 		link.flags = 0;
// 		link.source.entity = cameras[i].entity_id;
// 		link.source.index = 0;
// 		link.sink.entity = cameras[i].interface_entity_id;
// 		link.sink.index = 0;

// 		if (xioctl(cameras[cid].media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
// 			g_printerr("Could not disable [%s] camera link\n", cameras[i].cfg_name);
// 			return -1;
// 		}
// 	}

// 	// Enable the interface<->sensor link
// 	link.flags = MEDIA_LNK_FL_ENABLED;
// 	link.source.entity = cameras[cid].entity_id;
// 	link.source.index = 0;
// 	link.sink.entity = cameras[cid].interface_entity_id;
// 	link.sink.index = 0;

// 	current = cameras[cid];

// 	if (xioctl(cameras[cid].media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
// 		g_printerr("[%s] Could not enable direct sensor->if link\n", cameras[cid].cfg_name);
		
// 		for(int i=0;i<NUM_LINKS; i++) {
// 			if (!cameras[cid].media_links[i].valid)
// 				continue;

// 			if (cameras[cid].media_links[i].source_entity_id < 1) {
// 				g_printerr("[%s] media entry [%s] not found\n",
// 						cameras[cid].cfg_name,
// 						cameras[cid].media_links[i].source_name);
// 			}
// 			if (cameras[cid].media_links[i].target_entity_id < 1) {
// 				g_printerr("[%s] media entry [%s] not found\n",
// 						cameras[cid].cfg_name,
// 						cameras[cid].media_links[i].target_name);
// 			}

// 			link.flags = MEDIA_LNK_FL_ENABLED;
// 			link.source.entity = cameras[cid].media_links[i].source_entity_id;
// 			link.source.index = cameras[cid].media_links[i].source_port;
// 			link.sink.entity = cameras[cid].media_links[i].target_entity_id;
// 			link.sink.index = cameras[cid].media_links[i].target_port;
// 			if (xioctl(cameras[cid].media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
// 				g_printerr("[%s] Could not link [%s:%d] -> [%s:%d]\n",
// 						cameras[cid].cfg_name,
// 						cameras[cid].media_links[i].source_name,
// 						cameras[cid].media_links[i].source_port,
// 						cameras[cid].media_links[i].target_name,
// 						cameras[cid].media_links[i].target_port);
				
// 			}
// 			init_media_entity(cameras[cid].media_links[i].source_fname, current.width, current.height, current.mbus);
// 			init_media_entity(cameras[cid].media_links[i].target_fname, current.width, current.height, current.mbus);
// 		}
// 	}

// 	// Find camera node
// 	init_sensor(current.dev_fname, current.width, current.height, current.mbus, current.rate);
// 	return 0;
// }


void
on_open_last_clicked(GtkWidget *widget, gpointer user_data)
{
	char uri[270];
	GError *error = NULL;

	if(strlen(last_path) == 0) {
		return;
	}
	sprintf(uri, "file://%s", last_path);
	if(!g_app_info_launch_default_for_uri(uri, NULL, &error)){
		g_printerr("Could not launch image viewer: %s\n", error->message);
	}
}

void
on_open_directory_clicked(GtkWidget *widget, gpointer user_data)
{
	char uri[270];
	GError *error = NULL;
	sprintf(uri, "file://%s/Pictures", getenv("HOME"));
	if(!g_app_info_launch_default_for_uri(uri, NULL, &error)){
		g_printerr("Could not launch image viewer: %s\n", error->message);
	}
}

void
on_shutter_clicked(GtkWidget *widget, gpointer user_data)
{
	mp_io_pipeline_capture();
}

void
on_preview_tap(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	if (event->type != GDK_BUTTON_PRESS)
		return;

	// Handle taps on the controls
	if (event->y < 32) {
		if (gtk_widget_is_visible(control_box)) {
			gtk_widget_hide(control_box);
			return;
		} else {
			gtk_widget_show(control_box);
		}

		if (event->x < 60 ) {
			// ISO
			current_control = USER_CONTROL_ISO;
			gtk_label_set_text(GTK_LABEL(control_name), "ISO");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control_auto), !gain_is_manual);
			gtk_adjustment_set_lower(control_slider, 0.0);
			gtk_adjustment_set_upper(control_slider, (float)gain_max);
			gtk_adjustment_set_value(control_slider, (double)gain);

		} else if (event->x > 60 && event->x < 120) {
			// Shutter angle
			current_control = USER_CONTROL_SHUTTER;
			gtk_label_set_text(GTK_LABEL(control_name), "Shutter");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control_auto), !exposure_is_manual);
			gtk_adjustment_set_lower(control_slider, 1.0);
			gtk_adjustment_set_upper(control_slider, 360.0);
			gtk_adjustment_set_value(control_slider, (double)exposure);
		}

		return;
	}

	// Tapped preview image itself, try focussing
	if (has_auto_focus_start) {
		mp_io_pipeline_focus();
	}
}

void
on_error_close_clicked(GtkWidget *widget, gpointer user_data)
{
	gtk_widget_hide(error_box);
}

void
on_camera_switch_clicked(GtkWidget *widget, gpointer user_data)
{
	size_t next_index = camera->index + 1;
	const struct mp_camera_config *next_camera = mp_get_camera_config(next_index);

	if (!next_camera) {
		next_index = 0;
		next_camera = mp_get_camera_config(next_index);
	}

	camera = next_camera;
	update_io_pipeline();
}

void
on_settings_btn_clicked(GtkWidget *widget, gpointer user_data)
{
	gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "settings");
}

void
on_back_clicked(GtkWidget *widget, gpointer user_data)
{
	gtk_stack_set_visible_child_name(GTK_STACK(main_stack), "main");
}

void
on_control_auto_toggled(GtkToggleButton *widget, gpointer user_data)
{
	bool is_manual = gtk_toggle_button_get_active(widget) ? false : true;
	bool has_changed;

	switch (current_control) {
		case USER_CONTROL_ISO:
			if (gain_is_manual != is_manual) {
				gain_is_manual = is_manual;
				has_changed = true;
			}
			break;
		case USER_CONTROL_SHUTTER:
			if (exposure_is_manual != is_manual) {
				exposure_is_manual = is_manual;
				has_changed = true;
			}
			break;
	}

	if (has_changed) {
		update_io_pipeline();
		draw_controls();
	}
}

void
on_control_slider_changed(GtkAdjustment *widget, gpointer user_data)
{
	double value = gtk_adjustment_get_value(widget);

	bool has_changed = false;
	switch (current_control) {
		case USER_CONTROL_ISO:
			if (value != gain) {
				gain = (int)value;
				has_changed = true;
			}
			break;
		case USER_CONTROL_SHUTTER:
		{
			// So far all sensors use exposure time in number of sensor rows
			int new_exposure = (int)(value / 360.0 * camera->capture_mode.height);
			if (new_exposure != exposure) {
				exposure = new_exposure;
				has_changed = true;
			}
			break;
		}
	}

	if (has_changed) {
		update_io_pipeline();
		draw_controls();
	}
}

int
main(int argc, char *argv[])
{
	if (!mp_load_config())
		return 1;

	setenv("LC_NUMERIC", "C", 1);


	gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
	GtkBuilder *builder = gtk_builder_new_from_resource("/org/postmarketos/Megapixels/camera.glade");

	GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
	GtkWidget *shutter = GTK_WIDGET(gtk_builder_get_object(builder, "shutter"));
	GtkWidget *switch_btn = GTK_WIDGET(gtk_builder_get_object(builder, "switch_camera"));
	GtkWidget *settings_btn = GTK_WIDGET(gtk_builder_get_object(builder, "settings"));
	GtkWidget *settings_back = GTK_WIDGET(gtk_builder_get_object(builder, "settings_back"));
	GtkWidget *error_close = GTK_WIDGET(gtk_builder_get_object(builder, "error_close"));
	GtkWidget *open_last = GTK_WIDGET(gtk_builder_get_object(builder, "open_last"));
	GtkWidget *open_directory = GTK_WIDGET(gtk_builder_get_object(builder, "open_directory"));
	preview = GTK_WIDGET(gtk_builder_get_object(builder, "preview"));
	error_box = GTK_WIDGET(gtk_builder_get_object(builder, "error_box"));
	error_message = GTK_WIDGET(gtk_builder_get_object(builder, "error_message"));
	main_stack = GTK_WIDGET(gtk_builder_get_object(builder, "main_stack"));
	thumb_last = GTK_WIDGET(gtk_builder_get_object(builder, "thumb_last"));
	control_box = GTK_WIDGET(gtk_builder_get_object(builder, "control_box"));
	control_name = GTK_WIDGET(gtk_builder_get_object(builder, "control_name"));
	control_slider = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "control_adj"));
	control_auto = GTK_WIDGET(gtk_builder_get_object(builder, "control_auto"));
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(shutter, "clicked", G_CALLBACK(on_shutter_clicked), NULL);
	g_signal_connect(error_close, "clicked", G_CALLBACK(on_error_close_clicked), NULL);
	g_signal_connect(switch_btn, "clicked", G_CALLBACK(on_camera_switch_clicked), NULL);
	g_signal_connect(settings_btn, "clicked", G_CALLBACK(on_settings_btn_clicked), NULL);
	g_signal_connect(settings_back, "clicked", G_CALLBACK(on_back_clicked), NULL);
	g_signal_connect(open_last, "clicked", G_CALLBACK(on_open_last_clicked), NULL);
	g_signal_connect(open_directory, "clicked", G_CALLBACK(on_open_directory_clicked), NULL);
	g_signal_connect(preview, "draw", G_CALLBACK(preview_draw), NULL);
	g_signal_connect(preview, "configure-event", G_CALLBACK(preview_configure), NULL);
	gtk_widget_set_events(preview, gtk_widget_get_events(preview) |
			GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
	g_signal_connect(preview, "button-press-event", G_CALLBACK(on_preview_tap), NULL);
	g_signal_connect(control_auto, "toggled", G_CALLBACK(on_control_auto_toggled), NULL);
	g_signal_connect(control_slider, "value-changed", G_CALLBACK(on_control_slider_changed), NULL);

	GtkCssProvider *provider = gtk_css_provider_new();
	if (access("camera.css", F_OK) != -1) {
		gtk_css_provider_load_from_path(provider, "camera.css", NULL);
	} else {
		gtk_css_provider_load_from_resource(provider, "/org/postmarketos/Megapixels/camera.css");
	}
	GtkStyleContext *context = gtk_widget_get_style_context(error_box);
	gtk_style_context_add_provider(context,
		GTK_STYLE_PROVIDER(provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER);
	context = gtk_widget_get_style_context(control_box);
	gtk_style_context_add_provider(context,
		GTK_STYLE_PROVIDER(provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER);

	mp_io_pipeline_start();

	camera = mp_get_camera_config(0);
	update_io_pipeline();

	gtk_widget_show(window);
	gtk_main();

	mp_io_pipeline_stop();

	return 0;
}
