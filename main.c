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
#include <tiffio.h>
#include <locale.h>
#include "config.h"
#include "ini.h"
#include "quickdebayer.h"

#define NUM_CAMERAS 5

enum user_control {
	USER_CONTROL_ISO,
	USER_CONTROL_SHUTTER
};

#define TIFFTAG_FORWARDMATRIX1 50964

struct buffer {
	void *start;
	size_t length;
};

struct camerainfo {
	char cfg_name[100];
	int exists;
	char dev_name[260];
	char dev_fname[260];
	unsigned int entity_id;
	int width;
	int height;
	int rate;
	int rotate;
	int fmt;
	int mbus;
	int fd;

	char media_dev_name[260];
	char media_dev_fname[260];
	char video_dev_fname[260];
	int media_fd;
	int video_fd;
	unsigned int interface_entity_id;

	float colormatrix[9];
	float forwardmatrix[9];
	int blacklevel;
	int whitelevel;

	float focallength;
	float cropfactor;
	double fnumber;
	int iso_min;
	int iso_max;

	int gain_ctrl;
	int gain_max;

	int has_af_c;
	int has_af_s;
};

struct camerainfo cameras[NUM_CAMERAS];

static float colormatrix_srgb[] = {
	3.2409, -1.5373, -0.4986,
	-0.9692, 1.8759, 0.0415,
	0.0556, -0.2039, 1.0569
};

struct buffer *buffers;
static unsigned int n_buffers;

struct camerainfo current;

// General info
static char *exif_make;
static char *exif_model;

// State
static int ready = 0;
static int capture = 0;
static int current_cid = -1;
static cairo_surface_t *surface = NULL;
static cairo_surface_t *status_surface = NULL;
static int preview_width = -1;
static int preview_height = -1;
static char last_path[260] = "";
static int auto_exposure = 1;
static int exposure = 1;
static int auto_gain = 1;
static int gain = 1;
static int burst_length = 10;
static char burst_dir[23];
static char processing_script[512];
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

static int
xioctl(int fd, int request, void *arg)
{
	int r;
	do {
		r = ioctl(fd, request, arg);
	} while (r == -1 && errno == EINTR);
	return r;
}

static void
errno_exit(const char *s)
{
	fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static void
show_error(const char *s)
{
	gtk_label_set_text(GTK_LABEL(error_message), s);
	gtk_widget_show(error_box);
}

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
start_capturing(int fd)
{
	enum v4l2_buf_type type;

	for (int i = 0; i < n_buffers; ++i) {
		struct v4l2_buffer buf = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
			.index = i,
		};

		if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			errno_exit("VIDIOC_QBUF");
		}
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) {
		errno_exit("VIDIOC_STREAMON");
	}

	ready = 1;
}

static void
stop_capturing(int fd)
{
	int i;
	ready = 0;
	printf("Stopping capture\n");

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(fd, VIDIOC_STREAMOFF, &type) == -1) {
		errno_exit("VIDIOC_STREAMOFF");
	}

	for (i = 0; i < n_buffers; ++i) {
		munmap(buffers[i].start, buffers[i].length);
	}

}

static void
init_mmap(int fd)
{
	struct v4l2_requestbuffers req = {0};
	req.count = 4;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (xioctl(fd, VIDIOC_REQBUFS, &req) == -1) {
		if (errno == EINVAL) {
			fprintf(stderr, "%s does not support memory mapping",
				current.dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n",
			current.dev_name);
		exit(EXIT_FAILURE);
	}

	buffers = calloc(req.count, sizeof(buffers[0]));

	if (!buffers) {
		fprintf(stderr, "Out of memory\\n");
		exit(EXIT_FAILURE);
	}

	for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
		struct v4l2_buffer buf = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
			.index = n_buffers,
		};

		if (xioctl(fd, VIDIOC_QUERYBUF, &buf) == -1) {
			errno_exit("VIDIOC_QUERYBUF");
		}

		buffers[n_buffers].length = buf.length;
		buffers[n_buffers].start = mmap(NULL /* start anywhere */,
			buf.length,
			PROT_READ | PROT_WRITE /* required */,
			MAP_SHARED /* recommended */,
			fd, buf.m.offset);

		if (MAP_FAILED == buffers[n_buffers].start) {
			errno_exit("mmap");
		}
	}
}

static int
v4l2_ctrl_set(int fd, uint32_t id, int val)
{
	struct v4l2_control ctrl = {0};
	ctrl.id = id;
	ctrl.value = val;

	if (xioctl(fd, VIDIOC_S_CTRL, &ctrl) == -1) {
		g_printerr("Failed to set control %d to %d\n", id, val);
		return -1;
	}
	return 0;
}

static int
v4l2_ctrl_get(int fd, uint32_t id)
{
	struct v4l2_control ctrl = {0};
	ctrl.id = id;

	if (xioctl(fd, VIDIOC_G_CTRL, &ctrl) == -1) {
		g_printerr("Failed to get control %d\n", id);
		return -1;
	}
	return ctrl.value;
}

static int
v4l2_ctrl_get_max(int fd, uint32_t id)
{
	struct v4l2_queryctrl queryctrl;
	int ret;

	memset(&queryctrl, 0, sizeof(queryctrl));

	queryctrl.id = id;
	ret = xioctl(fd, VIDIOC_QUERYCTRL, &queryctrl);
	if (ret)
		return 0;

	if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
		return 0;
	}

	return queryctrl.maximum;
}

static int
v4l2_has_control(int fd, int control_id)
{
	struct v4l2_queryctrl queryctrl;
	int ret;

	memset(&queryctrl, 0, sizeof(queryctrl));

	queryctrl.id = control_id;
	ret = xioctl(fd, VIDIOC_QUERYCTRL, &queryctrl);
	if (ret)
		return 0;

	if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
		return 0;
	}

	return 1;
}

static void
draw_controls()
{
	cairo_t *cr;
	char iso[6];
	int temp;
	char shutterangle[6];

	if (auto_exposure) {
		sprintf(shutterangle, "auto");
	} else {
		temp = (int)((float)exposure / (float)current.height * 360);
		sprintf(shutterangle, "%d\u00b0", temp);
	}

	if (auto_gain) {
		sprintf(iso, "auto");
	} else {
		temp = remap(gain - 1, 0, current.gain_max, current.iso_min, current.iso_max);
		sprintf(iso, "%d", temp);
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
	
}

static void
init_sensor(char *fn, int width, int height, int mbus, int rate)
{
	int fd;
	struct v4l2_subdev_frame_interval interval = {};
	struct v4l2_subdev_format fmt = {};
	fd = open(fn, O_RDWR);

	g_print("Setting sensor rate to %d\n", rate);
	interval.pad = 0;
	interval.interval.numerator = 1;
	interval.interval.denominator = rate;

	if (xioctl(fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &interval) == -1) {
		errno_exit("VIDIOC_SUBDEV_S_FRAME_INTERVAL");
	}

	if (interval.interval.numerator != 1 || interval.interval.denominator != rate)
		g_printerr("Driver chose %d/%d instead\n",
			interval.interval.numerator, interval.interval.denominator);

	g_print("Setting sensor to %dx%d fmt %d\n",
		width, height, mbus);
	fmt.pad = 0;
	fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt.format.code = mbus;
	fmt.format.width = width;
	fmt.format.height = height;
	fmt.format.field = V4L2_FIELD_ANY;

	if (xioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) == -1) {
		errno_exit("VIDIOC_SUBDEV_S_FMT");
	}
	if (fmt.format.width != width || fmt.format.height != height || fmt.format.code != mbus)
		g_printerr("Driver chose %dx%d fmt %d instead\n",
			fmt.format.width, fmt.format.height,
			fmt.format.code);

	// Trigger continuous auto focus if the sensor supports it
	if (v4l2_has_control(fd, V4L2_CID_FOCUS_AUTO)) {
		current.has_af_c = 1;
		v4l2_ctrl_set(fd, V4L2_CID_FOCUS_AUTO, 1);
	}
	if (v4l2_has_control(fd, V4L2_CID_AUTO_FOCUS_START)) {
		current.has_af_s = 1;
	}

	if (v4l2_has_control(fd, V4L2_CID_GAIN)) {
		current.gain_ctrl = V4L2_CID_GAIN;
		current.gain_max = v4l2_ctrl_get_max(fd, V4L2_CID_GAIN);
	}

	if (v4l2_has_control(fd, V4L2_CID_ANALOGUE_GAIN)) {
		current.gain_ctrl = V4L2_CID_ANALOGUE_GAIN;
		current.gain_max = v4l2_ctrl_get_max(fd, V4L2_CID_ANALOGUE_GAIN);
	}

	auto_exposure = 1;
	auto_gain = 1;
	draw_controls();

	close(current.fd);
	current.fd = fd;
}

static int
init_device(int fd)
{
	struct v4l2_capability cap;
	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
		if (errno == EINVAL) {
			fprintf(stderr, "%s is no V4L2 device\n",
				current.dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s is no video capture device\n",
			current.dev_name);
		exit(EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "%s does not support streaming i/o\n",
			current.dev_name);
		exit(EXIT_FAILURE);
	}

	/* Select video input, video standard and tune here. */
	struct v4l2_cropcap cropcap = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};

	struct v4l2_crop crop = {0};
	if (xioctl(fd, VIDIOC_CROPCAP, &cropcap) == 0) {
		crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		crop.c = cropcap.defrect; /* reset to default */

		if (xioctl(fd, VIDIOC_S_CROP, &crop) == -1) {
			switch (errno) {
				case EINVAL:
					/* Cropping not supported. */
					break;
				default:
					/* Errors ignored. */
					break;
			}
		}
	} else {
		/* Errors ignored. */
	}


	struct v4l2_format fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};
	if (current.width > 0) {
		g_print("Setting camera to %dx%d fmt %d\n",
			current.width, current.height, current.fmt);
		fmt.fmt.pix.width = current.width;
		fmt.fmt.pix.height = current.height;
		fmt.fmt.pix.pixelformat = current.fmt;
		fmt.fmt.pix.field = V4L2_FIELD_ANY;

		if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
			g_printerr("VIDIOC_S_FMT failed");
			show_error("Could not set camera mode");
			return -1;
		}
		if (fmt.fmt.pix.width != current.width ||
			fmt.fmt.pix.height != current.height ||
			fmt.fmt.pix.pixelformat != current.fmt)
			g_printerr("Driver returned %dx%d fmt %d\n",
				fmt.fmt.pix.width, fmt.fmt.pix.height,
				fmt.fmt.pix.pixelformat);


		/* Note VIDIOC_S_FMT may change width and height. */
	} else {
		if (xioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
			errno_exit("VIDIOC_G_FMT");
		}
		g_print("Got %dx%d fmt %d from the driver\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height,
			fmt.fmt.pix.pixelformat);
		current.width = fmt.fmt.pix.width;
		current.height = fmt.fmt.pix.height;
	}
	current.fmt = fmt.fmt.pix.pixelformat;

	/* Buggy driver paranoia. */
	unsigned int min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min) {
		fmt.fmt.pix.bytesperline = min;
	}
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min) {
		fmt.fmt.pix.sizeimage = min;
	}

	init_mmap(fd);
	return 0;
}

static void
register_custom_tiff_tags(TIFF *tif)
{
	static const TIFFFieldInfo custom_fields[] = {
		{TIFFTAG_FORWARDMATRIX1, -1, -1, TIFF_SRATIONAL, FIELD_CUSTOM, 1, 1, "ForwardMatrix1"},
	};
	
	// Add missing dng fields
	TIFFMergeFieldInfo(tif, custom_fields, sizeof(custom_fields) / sizeof(custom_fields[0]));
}

static void
process_image(const int *p, int size)
{
	time_t rawtime;
	char datetime[20] = {0};
	struct tm tim;
	uint8_t *pixels;
	char fname[255];
	char fname_target[255];
	char command[1024];
	char timestamp[30];
	char uniquecameramodel[255];
	GdkPixbuf *pixbuf;
	GdkPixbuf *pixbufrot;
	GdkPixbuf *thumb;
	GError *error = NULL;
	double scale;
	cairo_t *cr;
	TIFF *tif;
	int skip = 2;
	long sub_offset = 0;
	uint64 exif_offset = 0;
	static const short cfapatterndim[] = {2, 2};
	static const float neutral[] = {1.0, 1.0, 1.0};
	static uint16_t isospeed[] = {0};

	// Only process preview frames when not capturing
	if (capture == 0) {
		if(current.width > 1280) {
			skip = 3;
		}
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, current.width / (skip*2), current.height / (skip*2));
		pixels = gdk_pixbuf_get_pixels(pixbuf);
		quick_debayer((const uint8_t *)p, pixels, current.fmt,
			       current.width, current.height, skip,
			       current.blacklevel);

		if (current.rotate == 0) {
			pixbufrot = pixbuf;
		} else if (current.rotate == 90) {
			pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
		} else if (current.rotate == 180) {
			pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
		} else if (current.rotate == 270) {
			pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
		}

		// Draw preview image
		scale = (double) preview_width / gdk_pixbuf_get_width(pixbufrot);
		cr = cairo_create(surface);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		cairo_scale(cr, scale, scale);
		gdk_cairo_set_source_pixbuf(cr, pixbufrot, 0, 0);
		cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_NONE);
		cairo_paint(cr);

		// Draw controls over preview
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_set_source_surface(cr, status_surface, 0, 0);
		cairo_paint(cr);

		// Queue gtk3 repaint of the preview area
		gtk_widget_queue_draw_area(preview, 0, 0, preview_width, preview_height);
		cairo_destroy(cr);
		g_object_unref(pixbufrot);
		g_object_unref(pixbuf);
	} else {
		capture--;
		time(&rawtime);
		tim = *(localtime(&rawtime));
		strftime(timestamp, 30, "%Y%m%d%H%M%S", &tim);
		strftime(datetime, 20, "%Y:%m:%d %H:%M:%S", &tim);

		sprintf(fname_target, "%s/Pictures/IMG%s", getenv("HOME"), timestamp);
		sprintf(fname, "%s/%d.dng", burst_dir, burst_length - capture);

		// Get latest exposure and gain now the auto gain/exposure is disabled while capturing
		gain = v4l2_ctrl_get(current.fd, current.gain_ctrl);
		exposure = v4l2_ctrl_get(current.fd, V4L2_CID_EXPOSURE);

		if(!(tif = TIFFOpen(fname, "w"))) {
			printf("Could not open tiff\n");
		}

		// Define TIFF thumbnail
		TIFFSetField(tif, TIFFTAG_SUBFILETYPE, 1);
		TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, current.width >> 4);
		TIFFSetField(tif, TIFFTAG_IMAGELENGTH, current.height >> 4);
		TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
		TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
		TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
		TIFFSetField(tif, TIFFTAG_MAKE, exif_make);
		TIFFSetField(tif, TIFFTAG_MODEL, exif_model);
		TIFFSetField(tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
		TIFFSetField(tif, TIFFTAG_DATETIME, datetime);
		TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 3);
		TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
		TIFFSetField(tif, TIFFTAG_SOFTWARE, "Megapixels");
		TIFFSetField(tif, TIFFTAG_SUBIFD, 1, &sub_offset);
		TIFFSetField(tif, TIFFTAG_DNGVERSION, "\001\001\0\0");
		TIFFSetField(tif, TIFFTAG_DNGBACKWARDVERSION, "\001\0\0\0");
		sprintf(uniquecameramodel, "%s %s", exif_make, exif_model);
		TIFFSetField(tif, TIFFTAG_UNIQUECAMERAMODEL, uniquecameramodel);
		if(current.colormatrix[0]) {
			TIFFSetField(tif, TIFFTAG_COLORMATRIX1, 9, current.colormatrix);
		} else {
			TIFFSetField(tif, TIFFTAG_COLORMATRIX1, 9, colormatrix_srgb);
		}
		if(current.forwardmatrix[0]) {
			TIFFSetField(tif, TIFFTAG_FORWARDMATRIX1, 9, current.forwardmatrix);
		}
		TIFFSetField(tif, TIFFTAG_ASSHOTNEUTRAL, 3, neutral);
		TIFFSetField(tif, TIFFTAG_CALIBRATIONILLUMINANT1, 21);
		// Write black thumbnail, only windows uses this
		{
			unsigned char *buf = (unsigned char *)calloc(1, (int)current.width >> 4);
			for (int row = 0; row < current.height>>4; row++) {
				TIFFWriteScanline(tif, buf, row, 0);
			}
			free(buf);
		}
		TIFFWriteDirectory(tif);

		// Define main photo
		TIFFSetField(tif, TIFFTAG_SUBFILETYPE, 0);
		TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, current.width);
		TIFFSetField(tif, TIFFTAG_IMAGELENGTH, current.height);
		TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, 8);
		TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA);
		TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, 1);
		TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
		TIFFSetField(tif, TIFFTAG_CFAREPEATPATTERNDIM, cfapatterndim);
		TIFFSetField(tif, TIFFTAG_CFAPATTERN, "\002\001\001\000"); // BGGR
		if(current.whitelevel) {
			TIFFSetField(tif, TIFFTAG_WHITELEVEL, 1, &current.whitelevel);
		}
		if(current.blacklevel) {
			TIFFSetField(tif, TIFFTAG_BLACKLEVEL, 1, &current.blacklevel);
		}
		TIFFCheckpointDirectory(tif);
		printf("Writing frame to %s\n", fname);
		
		unsigned char *pLine = (unsigned char*)malloc(current.width);
		for(int row = 0; row < current.height; row++){
			TIFFWriteScanline(tif, ((uint8_t *)p)+(row*current.width), row, 0);
		}
		free(pLine);
		TIFFWriteDirectory(tif);

		// Add an EXIF block to the tiff
		TIFFCreateEXIFDirectory(tif);
		// 1 = manual, 2 = full auto, 3 = aperture priority, 4 = shutter priority
		if (auto_exposure) {
			TIFFSetField(tif, EXIFTAG_EXPOSUREPROGRAM, 2);
		} else {
			TIFFSetField(tif, EXIFTAG_EXPOSUREPROGRAM, 1);
		}

		TIFFSetField(tif, EXIFTAG_EXPOSURETIME, (1.0/current.rate) / ((float)current.height / (float)exposure));
		isospeed[0] = (uint16_t)remap(gain - 1, 0, current.gain_max, current.iso_min, current.iso_max);
		TIFFSetField(tif, EXIFTAG_ISOSPEEDRATINGS, 1, isospeed);
		TIFFSetField(tif, EXIFTAG_FLASH, 0);

		TIFFSetField(tif, EXIFTAG_DATETIMEORIGINAL, datetime);
		TIFFSetField(tif, EXIFTAG_DATETIMEDIGITIZED, datetime);
		if(current.fnumber) {
			TIFFSetField(tif, EXIFTAG_FNUMBER, current.fnumber);
		}
		if(current.focallength) {
			TIFFSetField(tif, EXIFTAG_FOCALLENGTH, current.focallength);
		}
		if(current.focallength && current.cropfactor) {
			TIFFSetField(tif, EXIFTAG_FOCALLENGTHIN35MMFILM, (short)(current.focallength * current.cropfactor));
		}
		TIFFWriteCustomDirectory(tif, &exif_offset);
		TIFFFreeDirectory(tif);

		// Update exif pointer
		TIFFSetDirectory(tif, 0);
		TIFFSetField(tif, TIFFTAG_EXIFIFD, exif_offset);
		TIFFRewriteDirectory(tif);

		TIFFClose(tif);


		if (capture == 0) {
			// Update the thumbnail if this is the last frame
			pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, current.width / (skip*2), current.height / (skip*2));
			pixels = gdk_pixbuf_get_pixels(pixbuf);
			quick_debayer((const uint8_t *)p, pixels, current.fmt,
				      current.width, current.height, skip,
				      current.blacklevel);

			if (current.rotate == 0) {
				pixbufrot = pixbuf;
			} else if (current.rotate == 90) {
				pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
			} else if (current.rotate == 180) {
				pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
			} else if (current.rotate == 270) {
				pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
			}
			thumb = gdk_pixbuf_scale_simple(pixbufrot, 24, 24, GDK_INTERP_BILINEAR);
			gtk_image_set_from_pixbuf(GTK_IMAGE(thumb_last), thumb);
			sprintf(last_path, "%s.jpg", fname_target);
			if (error != NULL) {
				g_printerr("%s\n", error->message);
				g_clear_error(&error);
			}

			g_object_unref(pixbufrot);
			g_object_unref(pixbuf);

			// Start post-processing the captured burst
			g_print("Post process %s to %s.ext\n", burst_dir, fname_target);
			sprintf(command, "%s %s %s &", processing_script, burst_dir, fname_target);
			system(command);

			// Restore the auto exposure and gain if needed
			if (auto_exposure) {
				v4l2_ctrl_set(current.fd, V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_AUTO);
			}
			if (auto_gain) {
				v4l2_ctrl_set(current.fd, V4L2_CID_AUTOGAIN, 1);
			}

		}
	}
}

static gboolean
preview_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	cairo_set_source_surface(cr, surface, 0, 0);
	cairo_paint(cr);
	return FALSE;
}


static gboolean
preview_configure(GtkWidget *widget, GdkEventConfigure *event)
{
	cairo_t *cr;

	if (surface)
		cairo_surface_destroy(surface);

	surface = gdk_window_create_similar_surface(gtk_widget_get_window(widget),
		CAIRO_CONTENT_COLOR,
		gtk_widget_get_allocated_width(widget),
		gtk_widget_get_allocated_height(widget));

	preview_width = gtk_widget_get_allocated_width(widget);
	preview_height = gtk_widget_get_allocated_height(widget);

	cr = cairo_create(surface);
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);

	draw_controls();

	return TRUE;
}

static int
read_frame(int fd)
{
	struct v4l2_buffer buf = {0};

	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	if (xioctl(fd, VIDIOC_DQBUF, &buf) == -1) {
		switch (errno) {
			case EAGAIN:
				return 0;
			case EIO:
				/* Could ignore EIO, see spec. */
				/* fallthrough */
			default:
				errno_exit("VIDIOC_DQBUF");
				break;
		}
	}

	//assert(buf.index < n_buffers);

	process_image(buffers[buf.index].start, buf.bytesused);

	if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
		errno_exit("VIDIOC_QBUF");
	}

	return 1;
}

gboolean
get_frame()
{
	if (ready == 0)
		return TRUE;
	while (1) {
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(current.video_fd, &fds);

		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(current.video_fd + 1, &fds, NULL, NULL, &tv);

		if (r == -1) {
			if (EINTR == errno) {
				continue;
			}
			errno_exit("select");
		} else if (r == 0) {
			fprintf(stderr, "select timeout\\n");
			exit(EXIT_FAILURE);
		}

		if (read_frame(current.video_fd)) {
			break;
		}
		/* EAGAIN - continue select loop. */
	}
	return TRUE;
}

int
strtoint(const char *nptr, char **endptr, int base)
{
	long x = strtol(nptr, endptr, base);
	assert(x <= INT_MAX);
	return (int) x;
}

static int
config_ini_handler(void *user, const char *section, const char *name,
	const char *value)
{
	struct camerainfo *cc;
	int cid;
	int found;
	int first_free;
	if (strcmp(section, "device") == 0) {
		if (strcmp(name, "make") == 0) {
			exif_make = strdup(value);
		} else if (strcmp(name, "model") == 0) {
			exif_model = strdup(value);
		} else {
			g_printerr("Unknown key '%s' in [device]\n", name);
			exit(1);
		}
	} else {
		found = 0;
		first_free = -1;
		for (int i=0; i<NUM_CAMERAS; i++) {
			if(cameras[i].exists == 1 && strcmp(cameras[i].cfg_name, section) == 0) {
				cid = i;
				found = 1;
				break;
			}
			if(first_free == -1 && cameras[i].exists != 1) {
				first_free = i;
			}
		}

		if (first_free == -1 && found == 0) {
			g_printerr("More cameras defined than NUM_CAMERAS\n");
			exit(1);
		}

		if (!found) {
			cid = first_free;
			strcpy(cameras[cid].cfg_name, section);
			cameras[cid].exists = 1;
			printf("Adding camera %s from config\n", section);
		}
		
		cc = &cameras[cid];

		if (strcmp(name, "width") == 0) {
			cc->width = strtoint(value, NULL, 10);
		} else if (strcmp(name, "height") == 0) {
			cc->height = strtoint(value, NULL, 10);
		} else if (strcmp(name, "rate") == 0) {
			cc->rate = strtoint(value, NULL, 10);
		} else if (strcmp(name, "rotate") == 0) {
			cc->rotate = strtoint(value, NULL, 10);
		} else if (strcmp(name, "fmt") == 0) {
			if (strcmp(value, "RGGB8") == 0) {
				cc->fmt = V4L2_PIX_FMT_SRGGB8;
				cc->mbus = MEDIA_BUS_FMT_SRGGB8_1X8;
			} else if (strcmp(value, "BGGR8") == 0) {
				cc->fmt = V4L2_PIX_FMT_SBGGR8;
				cc->mbus = MEDIA_BUS_FMT_SBGGR8_1X8;
			} else if (strcmp(value, "GRBG8") == 0) {
				cc->fmt = V4L2_PIX_FMT_SGRBG8;
				cc->mbus = MEDIA_BUS_FMT_SGRBG8_1X8;
			} else if (strcmp(value, "GBRG8") == 0) {
				cc->fmt = V4L2_PIX_FMT_SGBRG8;
				cc->mbus = MEDIA_BUS_FMT_SGBRG8_1X8;
			} else {
				g_printerr("Unsupported pixelformat %s\n", value);
				exit(1);
			}
		} else if (strcmp(name, "driver") == 0) {
			strcpy(cc->dev_name, value);
		} else if (strcmp(name, "media-driver") == 0) {
			strcpy(cc->media_dev_name, value);
		} else if (strcmp(name, "colormatrix") == 0) {
			sscanf(value, "%f,%f,%f,%f,%f,%f,%f,%f,%f",
					cc->colormatrix+0,
					cc->colormatrix+1,
					cc->colormatrix+2,
					cc->colormatrix+3,
					cc->colormatrix+4,
					cc->colormatrix+5,
					cc->colormatrix+6,
					cc->colormatrix+7,
					cc->colormatrix+8
					);
		} else if (strcmp(name, "forwardmatrix") == 0) {
			sscanf(value, "%f,%f,%f,%f,%f,%f,%f,%f,%f",
					cc->forwardmatrix+0,
					cc->forwardmatrix+1,
					cc->forwardmatrix+2,
					cc->forwardmatrix+3,
					cc->forwardmatrix+4,
					cc->forwardmatrix+5,
					cc->forwardmatrix+6,
					cc->forwardmatrix+7,
					cc->forwardmatrix+8
					);
		} else if (strcmp(name, "whitelevel") == 0) {
			cc->whitelevel = strtoint(value, NULL, 10);
		} else if (strcmp(name, "blacklevel") == 0) {
			cc->blacklevel = strtoint(value, NULL, 10);
		} else if (strcmp(name, "focallength") == 0) {
			cc->focallength = strtof(value, NULL);
		} else if (strcmp(name, "cropfactor") == 0) {
			cc->cropfactor = strtof(value, NULL);
		} else if (strcmp(name, "fnumber") == 0) {
			cc->fnumber = strtod(value, NULL);
		} else if (strcmp(name, "iso-min") == 0) {
			cc->iso_min = strtod(value, NULL);
		} else if (strcmp(name, "iso-max") == 0) {
			cc->iso_max = strtod(value, NULL);
		} else {
			g_printerr("Unknown key '%s' in [%s]\n", name, section);
			exit(1);
		}
	}
	return 1;
}

int
find_dev_node(int maj, int min, char *fnbuf)
{
	DIR *d;
	struct dirent *dir;
	struct stat info;
	d = opendir("/dev");
	while ((dir = readdir(d)) != NULL) {
		sprintf(fnbuf, "/dev/%s", dir->d_name);
		stat(fnbuf, &info);
		if (!S_ISCHR(info.st_mode)) {
			continue;
		}
		if (major(info.st_rdev) == maj && minor(info.st_rdev) == min) {
			return 0;
		}
	}
	return -1;
}

int
setup_camera(int cid)
{
	struct media_link_desc link = {0};
	
	// Kill existing links for cameras in the same graph
	for(int i=0; i<NUM_CAMERAS; i++) {
		if(!cameras[i].exists)
			continue;
		if(i == cid)
			continue;
		if(strcmp(cameras[i].media_dev_fname, cameras[cid].media_dev_fname) != 0)
			continue;

		// Disable the interface<->front link
		link.flags = 0;
		link.source.entity = cameras[i].entity_id;
		link.source.index = 0;
		link.sink.entity = cameras[i].interface_entity_id;
		link.sink.index = 0;

		if (xioctl(cameras[cid].media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
			g_printerr("Could not disable [%s] camera link\n", cameras[i].cfg_name);
			return -1;
		}
	}


	// Enable the interface<->sensor link
	link.flags = MEDIA_LNK_FL_ENABLED;
	link.source.entity = cameras[cid].entity_id;
	link.source.index = 0;
	link.sink.entity = cameras[cid].interface_entity_id;
	link.sink.index = 0;

	if (xioctl(cameras[cid].media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
		g_printerr("[%s] Could not enable camera link\n", cameras[cid].cfg_name);
		return -1;
	}

	current = cameras[cid];

	// Find camera node
	init_sensor(current.dev_fname, current.width, current.height, current.mbus, current.rate);
	return 0;
}

int
find_camera(int cid)
{
	struct media_entity_desc entity = {0};
	DIR *d;
	struct dirent *dir;
	int fd;
	char fnbuf[261];
	struct media_device_info mdi = {0};
	int ret;
	int found_subdev = 0;
	int found_interface = 0;

	// find the /dev/media node for the camera media-driver
	d = opendir("/dev");
	while ((dir = readdir(d)) != NULL) {
		if (strncmp(dir->d_name, "media", 5) == 0) {
			sprintf(fnbuf, "/dev/%s", dir->d_name);
			fd = open(fnbuf, O_RDWR);
			xioctl(fd, MEDIA_IOC_DEVICE_INFO, &mdi);
			if (strcmp(mdi.driver, cameras[cid].media_dev_name) == 0) {
				printf("[%s] media device: %s (%s)\n", cameras[cid].cfg_name, fnbuf, mdi.driver);
				cameras[cid].media_fd = fd;
				goto find_camera_found_media;
			}
			close(fd);
		}
	}
	g_printerr("Could not find /dev/media* node matching '%s'\n", cameras[cid].media_dev_name);
	return 0;

find_camera_found_media:
	// inspect the media node and find the sensor
	while (1) {
		entity.id = entity.id | MEDIA_ENT_ID_FLAG_NEXT;
		ret = xioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &entity);
		if (ret < 0) {
			break;
		}
		if (!found_subdev && strncmp(entity.name, cameras[cid].dev_name, strlen(cameras[cid].dev_name)) == 0) {
			cameras[cid].entity_id = entity.id;
			find_dev_node(entity.dev.major, entity.dev.minor, cameras[cid].dev_fname);
			printf("[%s] subdev: %s (%s)\n", cameras[cid].cfg_name, cameras[cid].dev_fname, entity.name);
			found_subdev = 1;
		}
		if (!found_interface && entity.type == MEDIA_ENT_F_IO_V4L) {
			cameras[cid].interface_entity_id = entity.id;
			find_dev_node(entity.dev.major, entity.dev.minor, cameras[cid].video_dev_fname);
			printf("[%s] video: %s (%s)\n", cameras[cid].cfg_name, cameras[cid].video_dev_fname, entity.name);
			found_interface = 1;
		}
	}

	if (!found_subdev) {
		g_printerr("[%s] Could not find subdev '%s'\n", cameras[cid].cfg_name, cameras[cid].dev_name);
		return 0;
	}
	if (!found_interface) {
		g_printerr("[%s] Could not find interface node\n", cameras[cid].cfg_name);
		return 0;
	}
	
	return 1;
}

int
find_cameras()
{
	int found_one = 0;
	for(int i=0; i<NUM_CAMERAS; i++) {
		if(!cameras[i].exists)
			continue;
		if(find_camera(i)) {
			found_one = 1;
		} else {
			cameras[i].exists = 0;
		}
	}
	return found_one;
}

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
	char template[] = "/tmp/megapixels.XXXXXX";
	char *tempdir;
	tempdir = mkdtemp(template);

	if (tempdir == NULL) {
		g_printerr("Could not make capture directory %s\n", template);
		exit (EXIT_FAILURE);
	}

	strcpy(burst_dir, tempdir);
	
	// Disable the autogain/exposure while taking the burst
	v4l2_ctrl_set(current.fd, V4L2_CID_AUTOGAIN, 0);
	v4l2_ctrl_set(current.fd, V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL);

	capture = burst_length;
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
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control_auto), auto_gain);
			gtk_adjustment_set_lower(control_slider, 0.0);
			gtk_adjustment_set_upper(control_slider, (float)current.gain_max);
			gtk_adjustment_set_value(control_slider, (double)gain);

		} else if (event->x > 60 && event->x < 120) {
			// Shutter angle
			current_control = USER_CONTROL_SHUTTER;
			gtk_label_set_text(GTK_LABEL(control_name), "Shutter");
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control_auto), auto_exposure);
			gtk_adjustment_set_lower(control_slider, 1.0);
			gtk_adjustment_set_upper(control_slider, 360.0);
			gtk_adjustment_set_value(control_slider, (double)exposure);
		}

		return;
	}

	// Tapped preview image itself, try focussing
	if (current.has_af_s) {
		v4l2_ctrl_set(current.fd, V4L2_CID_AUTO_FOCUS_STOP, 1);
		v4l2_ctrl_set(current.fd, V4L2_CID_AUTO_FOCUS_START, 1);
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
	int found_next = 0;
	int old_cid = current_cid;
	int next_cid = -1;
	for(int i=current_cid; i<NUM_CAMERAS; i++) {
		if(i == current_cid)
			continue;
		if(!cameras[i].exists)
			continue;

		found_next = 1;
		next_cid = i;
	}
	if(!found_next) {
		for(int i=0; i<current_cid; i++) {
			if(i == current_cid)
				continue;
			if(!cameras[i].exists)
				continue;

			found_next = 1;
			next_cid = i;
		}	
	}
	if(!found_next) {
		g_printerr("Could not find a candidate camera to switch to\n");
		return;
	}

	printf("Switching from [%s] to [%s]\n", cameras[current_cid].cfg_name, cameras[next_cid].cfg_name);

	stop_capturing(cameras[current_cid].video_fd);
	close(cameras[current_cid].fd);
	setup_camera(next_cid);
	close(cameras[old_cid].video_fd);
	cameras[next_cid].video_fd = open(cameras[next_cid].video_dev_fname, O_RDWR);
	if (cameras[next_cid].video_fd == -1) {
		g_printerr("Error opening video device: %s\n", cameras[next_cid].video_dev_fname);
		return;
	}

	current_cid = next_cid;
	init_device(cameras[current_cid].video_fd);
	start_capturing(cameras[current_cid].video_fd);
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
	int fd = current.fd;
	switch (current_control) {
		case USER_CONTROL_ISO:
			auto_gain = gtk_toggle_button_get_active(widget);
			if (auto_gain) {
				v4l2_ctrl_set(fd, V4L2_CID_AUTOGAIN, 1);
			} else {
				v4l2_ctrl_set(fd, V4L2_CID_AUTOGAIN, 0);
				gain = v4l2_ctrl_get(fd, V4L2_CID_GAIN);
				gtk_adjustment_set_value(control_slider, (double)gain);
			}
			break;
		case USER_CONTROL_SHUTTER:
			auto_exposure = gtk_toggle_button_get_active(widget);
			if (auto_exposure) {
				v4l2_ctrl_set(fd, V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_AUTO);
			} else {
				v4l2_ctrl_set(fd, V4L2_CID_EXPOSURE_AUTO, V4L2_EXPOSURE_MANUAL);
				exposure = v4l2_ctrl_get(fd, V4L2_CID_EXPOSURE);
				gtk_adjustment_set_value(control_slider, (double)exposure);
			}
			break;
	}
	draw_controls();
}

void
on_control_slider_changed(GtkAdjustment *widget, gpointer user_data)
{
	double value = gtk_adjustment_get_value(widget);

	switch (current_control) {
		case USER_CONTROL_ISO:
			gain = (int)value;
			v4l2_ctrl_set(current.fd, current.gain_ctrl, gain);
			break;
		case USER_CONTROL_SHUTTER:
			// So far all sensors use exposure time in number of sensor rows
			exposure = (int)(value / 360.0 * current.height);
			v4l2_ctrl_set(current.fd, V4L2_CID_EXPOSURE, exposure);
			break;
	}
	draw_controls();
}

int
find_config(char *conffile)
{
	char buf[512];
	char *xdg_config_home;
	wordexp_t exp_result;
	FILE *fp;

	// Resolve XDG stuff
	if ((xdg_config_home = getenv("XDG_CONFIG_HOME")) == NULL) {
		xdg_config_home = "~/.config";
	}
	wordexp(xdg_config_home, &exp_result, 0);
	xdg_config_home = strdup(exp_result.we_wordv[0]);
	wordfree(&exp_result);

	if(access("/proc/device-tree/compatible", F_OK) != -1) {
		// Reads to compatible string of the current device tree, looks like:
		// pine64,pinephone-1.2\0allwinner,sun50i-a64\0
		fp = fopen("/proc/device-tree/compatible", "r");
		fgets(buf, 512, fp);
		fclose(fp);

		// Check config/%dt.ini in the current working directory
		sprintf(conffile, "config/%s.ini", buf);
		if(access(conffile, F_OK) != -1) {
			printf("Found config file at %s\n", conffile);
			return 0;
		}

		// Check for a config file in XDG_CONFIG_HOME
		sprintf(conffile, "%s/megapixels/config/%s.ini", xdg_config_home, buf);
		if(access(conffile, F_OK) != -1) {
			printf("Found config file at %s\n", conffile);
			return 0;
		}

		// Check user overridden /etc/megapixels/config/$dt.ini
		sprintf(conffile, "%s/megapixels/config/%s.ini", SYSCONFDIR, buf);
		if(access(conffile, F_OK) != -1) {
			printf("Found config file at %s\n", conffile);
			return 0;
		}
		// Check packaged /usr/share/megapixels/config/$dt.ini
		sprintf(conffile, "%s/megapixels/config/%s.ini", DATADIR, buf);
		if(access(conffile, F_OK) != -1) {
			printf("Found config file at %s\n", conffile);
			return 0;
		}
		printf("%s not found\n", conffile);
	} else {
		printf("Could not read device name from device tree\n");
	}

	// If all else fails, fall back to /etc/megapixels.ini
	sprintf(conffile, "/etc/megapixels.ini");
	if(access(conffile, F_OK) != -1) {
		printf("Found config file at %s\n", conffile);
		return 0;
	}
	return -1;
}

int
find_processor(char *script)
{
	char *xdg_config_home;
	char filename[] = "postprocess.sh";
	wordexp_t exp_result;

	// Resolve XDG stuff
	if ((xdg_config_home = getenv("XDG_CONFIG_HOME")) == NULL) {
		xdg_config_home = "~/.config";
	}
	wordexp(xdg_config_home, &exp_result, 0);
	xdg_config_home = strdup(exp_result.we_wordv[0]);
	wordfree(&exp_result);

	// Check postprocess.h in the current working directory
	sprintf(script, "%s", filename);
	if(access(script, F_OK) != -1) {
		sprintf(script, "./%s", filename);
		printf("Found postprocessor script at %s\n", script);
		return 0;
	}

	// Check for a script in XDG_CONFIG_HOME
	sprintf(script, "%s/megapixels/%s", xdg_config_home, filename);
	if(access(script, F_OK) != -1) {
		printf("Found postprocessor script at %s\n", script);
		return 0;
	}

	// Check user overridden /etc/megapixels/postprocessor.sh
	sprintf(script, "%s/megapixels/%s", SYSCONFDIR, filename);
	if(access(script, F_OK) != -1) {
		printf("Found postprocessor script at %s\n", script);
		return 0;
	}

	// Check packaged /usr/share/megapixels/postprocessor.sh
	sprintf(script, "%s/megapixels/%s", DATADIR, filename);
	if(access(script, F_OK) != -1) {
		printf("Found postprocessor script at %s\n", script);
		return 0;
	}

	return -1;
}

int
main(int argc, char *argv[])
{
	int ret;
	char conffile[512];

	ret = find_config(conffile);
	if (ret) {
		g_printerr("Could not find any config file\n");
		return ret;
	}
	ret = find_processor(processing_script);
	if (ret) {
		g_printerr("Could not find any post-process script\n");
		return ret;
	}

	setenv("LC_NUMERIC", "C", 1);

	TIFFSetTagExtender(register_custom_tiff_tags);

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

	int result = ini_parse(conffile, config_ini_handler, NULL);
	if (result == -1) {
		g_printerr("Config file not found\n");
		return 1;
	} else if (result == -2) {
		g_printerr("Could not allocate memory to parse config file\n");
		return 1;
	} else if (result != 0) {
		g_printerr("Could not parse config file\n");
		return 1;
	}
	if (find_cameras() == 0) {
		g_printerr("Could not find the cameras\n");
		show_error("Could not find the cameras");
		goto failed;
	}

	// Disable the camera switch button if only one camera exists
	int camera_count = 0;
	for (int i=0; i<NUM_CAMERAS; i++) {
		if(cameras[i].exists)
			camera_count++;
	}
	if (camera_count < 2) {
		gtk_widget_set_sensitive(switch_btn, FALSE);
	}

	// Setup first defined camera
	for(int i=0;i<NUM_CAMERAS; i++) {
		if(cameras[i].exists){
			setup_camera(i);
			current_cid = i;

			cameras[i].video_fd = open(cameras[i].video_dev_fname, O_RDWR);
			if (cameras[i].video_fd == -1) {
				g_printerr("Error opening video device: %s\n", cameras[i].video_dev_fname);
				show_error("Error opening the video device");
				goto failed;
			}
			if(init_device(cameras[i].video_fd) < 0){
				goto failed;
			}
			start_capturing(cameras[i].video_fd);

			break;
		}
	}


failed:
	printf("window show\n");
	gtk_widget_show(window);
	g_idle_add((GSourceFunc)get_frame, NULL);
	gtk_main();
	return 0;
}
