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
#include <gtk/gtk.h>
#include "ini.h"
#include "bayer.h"

enum io_method {
	IO_METHOD_READ,
	IO_METHOD_MMAP,
	IO_METHOD_USERPTR,
};

struct buffer {
	void *start;
	size_t length;
};

struct buffer *buffers;
static unsigned int n_buffers;

// Rear camera
static char *rear_dev_name;
static unsigned int rear_entity_id;
static char rear_dev[260];
static int rear_width = -1;
static int rear_height = -1;
static int rear_rotate = 0;
static int rear_fmt = V4L2_PIX_FMT_RGB24;
static int rear_mbus = MEDIA_BUS_FMT_RGB888_1X24;

// Front camera
static char *front_dev_name;
static unsigned int front_entity_id;
static char front_dev[260];
static int front_width = -1;
static int front_height = -1;
static int front_rotate = 0;
static int front_fmt = V4L2_PIX_FMT_RGB24;
static int front_mbus = MEDIA_BUS_FMT_RGB888_1X24;

// Camera interface
static char *media_drv_name;
static unsigned int interface_entity_id;
static char dev_name[260];
static int media_fd;
static int video_fd;

// State
static int ready = 0;
static int current_width = -1;
static int current_height = -1;
static int current_fmt = 0;
static int current_rotate = 0;
static int current_fd;
static int capture = 0;
static int current_is_rear = 1;
static cairo_surface_t *surface = NULL;
static int preview_width = -1;
static int preview_height = -1;

// Widgets
GtkWidget *preview;

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
				dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_REQBUFS");
		}
	}

	if (req.count < 2) {
		fprintf(stderr, "Insufficient buffer memory on %s\n",
			dev_name);
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

static void
init_sensor(char *fn, int width, int height, int mbus)
{
	int fd;
	struct v4l2_subdev_format fmt;
	fd = open(fn, O_RDWR);

	g_printerr("Setting sensor to %dx%d fmt %d\n",
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

	g_printerr("Driver returned %dx%d fmt %d\n",
		fmt.format.width, fmt.format.height,
		fmt.format.code);

	// Placeholder, default is also 1
	//v4l2_ctrl_set(fd, V4L2_CID_AUTOGAIN, 1);
	close(current_fd);
	current_fd = fd;
}

static void
init_device(int fd)
{
	struct v4l2_capability cap;
	if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == -1) {
		if (errno == EINVAL) {
			fprintf(stderr, "%s is no V4L2 device\n",
				dev_name);
			exit(EXIT_FAILURE);
		} else {
			errno_exit("VIDIOC_QUERYCAP");
		}
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
		fprintf(stderr, "%s is no video capture device\n",
			dev_name);
		exit(EXIT_FAILURE);
	}

	if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
		fprintf(stderr, "%s does not support streaming i/o\n",
			dev_name);
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
	if (current_width > 0) {
		g_printerr("Setting camera to %dx%d fmt %d\n",
			current_width, current_height, current_fmt);
		fmt.fmt.pix.width = current_width;
		fmt.fmt.pix.height = current_height;
		fmt.fmt.pix.pixelformat = current_fmt;
		fmt.fmt.pix.field = V4L2_FIELD_ANY;

		if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
			errno_exit("VIDIOC_S_FMT");
		}

		g_printerr("Driver returned %dx%d fmt %d\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height,
			fmt.fmt.pix.pixelformat);


		/* Note VIDIOC_S_FMT may change width and height. */
	} else {
		g_printerr("Querying camera format\n");
		/* Preserve original settings as set by v4l2-ctl for example */
		if (xioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
			errno_exit("VIDIOC_G_FMT");
		}
		g_printerr("Driver returned %dx%d fmt %d\n",
			fmt.fmt.pix.width, fmt.fmt.pix.height,
			fmt.fmt.pix.pixelformat);
		current_width = fmt.fmt.pix.width;
		current_height = fmt.fmt.pix.height;
	}
	current_fmt = fmt.fmt.pix.pixelformat;

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
}

static void
process_image(const int *p, int size)
{
	clock_t t;
	time_t rawtime;
	struct tm tim;
	uint8_t *pixels;
	double time_taken;
	char fname[255];
	char timestamp[30];
	GdkPixbuf *pixbuf;
	GdkPixbuf *pixbufrot;
	GError *error = NULL;
	double scale;
	cairo_t *cr;
	t = clock();

	dc1394bayer_method_t method = DC1394_BAYER_METHOD_DOWNSAMPLE;
	dc1394color_filter_t filter = DC1394_COLOR_FILTER_BGGR;

	if (capture) {
		method = DC1394_BAYER_METHOD_SIMPLE;
		// method = DC1394_BAYER_METHOD_VNG is slightly sharper but takes 10 seconds;
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, current_width, current_height);
	} else {
		pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, current_width / 2, current_height / 2);
	}

	pixels = gdk_pixbuf_get_pixels(pixbuf);
	dc1394_bayer_decoding_8bit((const uint8_t *) p, pixels, current_width, current_height, filter, method);
	if (current_rotate == 0) {
		pixbufrot = pixbuf;
	} else if (current_rotate == 90) {
		pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
	} else if (current_rotate == 180) {
		pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_UPSIDEDOWN);
	} else if (current_rotate == 270) {
		pixbufrot = gdk_pixbuf_rotate_simple(pixbuf, GDK_PIXBUF_ROTATE_CLOCKWISE);
	}
	if (capture) {
		time(&rawtime);
		tim = *(localtime(&rawtime));
		strftime(timestamp, 30, "%F %T", &tim);
		sprintf(fname, "%s/Pictures/Photo-%s.jpg", getenv("HOME"), timestamp);
		printf("Saving image\n");
		gdk_pixbuf_save(pixbufrot, fname, "jpeg", &error, "quality", "85", NULL);
		if (error != NULL) {
			g_printerr(error->message);
			g_clear_error(&error);
		}
	} else {
		scale = (double) preview_width / gdk_pixbuf_get_width(pixbufrot);
		cr = cairo_create(surface);
		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_paint(cr);
		cairo_scale(cr, scale, scale);
		gdk_cairo_set_source_pixbuf(cr, pixbufrot, 0, 0);
		cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_NONE);
		cairo_paint(cr);
		gtk_widget_queue_draw_area(preview, 0, 0, preview_width, preview_height);
	}
	capture = 0;
	t = clock() - t;
	time_taken = ((double) t) / CLOCKS_PER_SEC;
	printf("%f fps\n", 1.0 / time_taken);
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
		FD_SET(video_fd, &fds);

		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(video_fd + 1, &fds, NULL, NULL, &tv);

		if (r == -1) {
			if (EINTR == errno) {
				continue;
			}
			errno_exit("select");
		} else if (r == 0) {
			fprintf(stderr, "select timeout\\n");
			exit(EXIT_FAILURE);
		}

		if (read_frame(video_fd)) {
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
	if (strcmp(section, "rear") == 0) {
		if (strcmp(name, "width") == 0) {
			rear_width = strtoint(value, NULL, 10);
		} else if (strcmp(name, "height") == 0) {
			rear_height = strtoint(value, NULL, 10);
		} else if (strcmp(name, "rotate") == 0) {
			rear_rotate = strtoint(value, NULL, 10);
		} else if (strcmp(name, "fmt") == 0) {
			if (strcmp(value, "RGB") == 0) {
				rear_fmt = V4L2_PIX_FMT_RGB24;
			} else if (strcmp(value, "UYVY") == 0) {
				rear_fmt = V4L2_PIX_FMT_UYVY;
			} else if (strcmp(value, "YUYV") == 0) {
				rear_fmt = V4L2_PIX_FMT_YUYV;
			} else if (strcmp(value, "JPEG") == 0) {
				rear_fmt = V4L2_PIX_FMT_JPEG;
			} else if (strcmp(value, "NV12") == 0) {
				rear_fmt = V4L2_PIX_FMT_NV12;
			} else if (strcmp(value, "YUV420") == 0
				|| strcmp(value, "I420") == 0
				|| strcmp(value, "YU12") == 0) {
				rear_fmt = V4L2_PIX_FMT_YUV420;
			} else if (strcmp(value, "YVU420") == 0
				|| strcmp(value, "YV12") == 0) {
				rear_fmt = V4L2_PIX_FMT_YVU420;
			} else if (strcmp(value, "RGGB8") == 0) {
				rear_fmt = V4L2_PIX_FMT_SRGGB8;
			} else if (strcmp(value, "BGGR8") == 0) {
				rear_fmt = V4L2_PIX_FMT_SBGGR8;
				rear_mbus = MEDIA_BUS_FMT_SBGGR8_1X8;
			} else if (strcmp(value, "GRBG8") == 0) {
				rear_fmt = V4L2_PIX_FMT_SGRBG8;
			} else if (strcmp(value, "GBRG8") == 0) {
				rear_fmt = V4L2_PIX_FMT_SGBRG8;
			} else {
				g_printerr("Unsupported pixelformat %s\n", value);
				exit(1);
			}
		} else if (strcmp(name, "driver") == 0) {
			rear_dev_name = strdup(value);
		} else {
			g_printerr("Unknown key '%s' in [rear]\n", name);
			exit(1);
		}
	} else if (strcmp(section, "front") == 0) {
		if (strcmp(name, "width") == 0) {
			front_width = strtoint(value, NULL, 10);
		} else if (strcmp(name, "height") == 0) {
			front_height = strtoint(value, NULL, 10);
		} else if (strcmp(name, "rotate") == 0) {
			front_rotate = strtoint(value, NULL, 10);
		} else if (strcmp(name, "fmt") == 0) {
			if (strcmp(value, "RGB") == 0) {
				front_fmt = V4L2_PIX_FMT_RGB24;
			} else if (strcmp(value, "UYVY") == 0) {
				front_fmt = V4L2_PIX_FMT_UYVY;
			} else if (strcmp(value, "YUYV") == 0) {
				front_fmt = V4L2_PIX_FMT_YUYV;
			} else if (strcmp(value, "JPEG") == 0) {
				front_fmt = V4L2_PIX_FMT_JPEG;
			} else if (strcmp(value, "NV12") == 0) {
				front_fmt = V4L2_PIX_FMT_NV12;
			} else if (strcmp(value, "YUV420") == 0
				|| strcmp(value, "I420") == 0
				|| strcmp(value, "YU12") == 0) {
				front_fmt = V4L2_PIX_FMT_YUV420;
			} else if (strcmp(value, "YVU420") == 0
				|| strcmp(value, "YV12") == 0) {
				front_fmt = V4L2_PIX_FMT_YVU420;
			} else if (strcmp(value, "RGGB8") == 0) {
				front_fmt = V4L2_PIX_FMT_SRGGB8;
			} else if (strcmp(value, "BGGR8") == 0) {
				front_fmt = V4L2_PIX_FMT_SBGGR8;
				front_mbus = MEDIA_BUS_FMT_SBGGR8_1X8;
			} else if (strcmp(value, "GRBG8") == 0) {
				front_fmt = V4L2_PIX_FMT_SGRBG8;
			} else if (strcmp(value, "GBRG8") == 0) {
				front_fmt = V4L2_PIX_FMT_SGBRG8;
			} else {
				g_printerr("Unsupported pixelformat %s\n", value);
				exit(1);
			}
		} else if (strcmp(name, "driver") == 0) {
			front_dev_name = strdup(value);
		} else {
			g_printerr("Unknown key '%s' in [front]\n", name);
			exit(1);
		}
	} else if (strcmp(section, "device") == 0) {
		if (strcmp(name, "csi") == 0) {
			media_drv_name = strdup(value);
		} else {
			g_printerr("Unknown key '%s' in [device]\n", name);
			exit(1);
		}
	} else {
		g_printerr("Unknown section '%s' in config file\n", section);
		exit(1);
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
setup_rear()
{
	struct media_link_desc link = {0};

	// Disable the interface<->front link
	link.flags = 0;
	link.source.entity = front_entity_id;
	link.source.index = 0;
	link.sink.entity = interface_entity_id;
	link.sink.index = 0;

	if (xioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
		g_printerr("Could not disable front camera link\n");
		return -1;
	}

	// Enable the interface<->rear link
	link.flags = MEDIA_LNK_FL_ENABLED;
	link.source.entity = rear_entity_id;
	link.source.index = 0;
	link.sink.entity = interface_entity_id;
	link.sink.index = 0;

	if (xioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
		g_printerr("Could not enable rear camera link\n");
		return -1;
	}

	current_width = rear_width;
	current_height = rear_height;
	current_fmt = rear_fmt;
	current_rotate = rear_rotate;
	// Find camera node
	init_sensor(rear_dev, rear_width, rear_height, rear_mbus);
	return 0;
}

int
setup_front()
{
	struct media_link_desc link = {0};

	// Disable the interface<->rear link
	link.flags = 0;
	link.source.entity = rear_entity_id;
	link.source.index = 0;
	link.sink.entity = interface_entity_id;
	link.sink.index = 0;

	if (xioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
		g_printerr("Could not disable rear camera link\n");
		return -1;
	}

	// Enable the interface<->rear link
	link.flags = MEDIA_LNK_FL_ENABLED;
	link.source.entity = front_entity_id;
	link.source.index = 0;
	link.sink.entity = interface_entity_id;
	link.sink.index = 0;

	if (xioctl(media_fd, MEDIA_IOC_SETUP_LINK, &link) < 0) {
		g_printerr("Could not enable front camera link\n");
		return -1;
	}
	current_width = front_width;
	current_height = front_height;
	current_fmt = front_fmt;
	current_rotate = front_rotate;
	// Find camera node
	init_sensor(front_dev, front_width, front_height, front_mbus);
	return 0;
}

int
find_cameras()
{
	struct media_entity_desc entity = {0};
	int ret;
	int found = 0;

	while (1) {
		entity.id = entity.id | MEDIA_ENT_ID_FLAG_NEXT;
		ret = xioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &entity);
		if (ret < 0) {
			break;
		}
		printf("At node %s, (0x%x)\n", entity.name, entity.type);
		if (strncmp(entity.name, front_dev_name, strlen(front_dev_name)) == 0) {
			front_entity_id = entity.id;
			find_dev_node(entity.dev.major, entity.dev.minor, front_dev);
			printf("Found front cam, is %s at %s\n", entity.name, front_dev);
			found++;
		}
		if (strncmp(entity.name, rear_dev_name, strlen(rear_dev_name)) == 0) {
			rear_entity_id = entity.id;
			find_dev_node(entity.dev.major, entity.dev.minor, rear_dev);
			printf("Found rear cam, is %s at %s\n", entity.name, rear_dev);
			found++;
		}
		if (entity.type == MEDIA_ENT_F_IO_V4L) {
			interface_entity_id = entity.id;
			find_dev_node(entity.dev.major, entity.dev.minor, dev_name);
			printf("Found v4l2 interface node at %s\n", dev_name);
		}
	}
	if (found < 2) {
		return -1;
	}
	return 0;
}


int
find_media_fd()
{
	DIR *d;
	struct dirent *dir;
	int fd;
	char fnbuf[261];
	struct media_device_info mdi = {0};
	d = opendir("/dev");
	while ((dir = readdir(d)) != NULL) {
		if (strncmp(dir->d_name, "media", 5) == 0) {
			sprintf(fnbuf, "/dev/%s", dir->d_name);
			printf("Checking %s\n", fnbuf);
			fd = open(fnbuf, O_RDWR);
			xioctl(fd, MEDIA_IOC_DEVICE_INFO, &mdi);
			printf("Found media device: %s\n", mdi.driver);
			if (strcmp(mdi.driver, media_drv_name) == 0) {
				media_fd = fd;
				return 0;
			}
			close(fd);
		}
	}
	return 1;
}

void
on_shutter_clicked(GtkWidget *widget, gpointer user_data)
{
	capture = 1;
}

void
on_camera_switch_clicked(GtkWidget *widget, gpointer user_data)
{
	stop_capturing(video_fd);
	close(current_fd);
	if (current_is_rear == 1) {
		setup_front();
		current_is_rear = 0;
	} else {
		setup_rear();
		current_is_rear = 1;
	}
	printf("close() = %d\n", close(video_fd));
	printf("Opening %s again\n", dev_name);
	video_fd = open(dev_name, O_RDWR);
	if (video_fd == -1) {
		g_printerr("Error opening video device: %s\n", dev_name);
		return;
	}
	init_device(video_fd);
	start_capturing(video_fd);
}

int
main(int argc, char *argv[])
{
	if (argc != 2) {
		g_printerr("Usage: camera configfile\n");
		return 1;
	}

	GError *error = NULL;
	gtk_init(&argc, &argv);
	g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
	GtkBuilder *builder = gtk_builder_new();
	char *glade_file = "/usr/share/megapixels/ui/camera.glade";
	if (access("camera.glade", F_OK) != -1) {
		glade_file = "camera.glade";
	}
	if (gtk_builder_add_from_file(builder, glade_file, &error) == 0) {
		g_printerr("Error loading file: %s\n", error->message);
		g_clear_error(&error);
		return 1;
	}

	GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
	GtkWidget *preview_box = GTK_WIDGET(gtk_builder_get_object(builder, "preview_box"));
	GtkWidget *shutter = GTK_WIDGET(gtk_builder_get_object(builder, "shutter"));
	GtkWidget *switch_btn = GTK_WIDGET(gtk_builder_get_object(builder, "switch_camera"));
	GtkWidget *settings_btn = GTK_WIDGET(gtk_builder_get_object(builder, "settings"));
	preview = GTK_WIDGET(gtk_builder_get_object(builder, "preview"));
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	g_signal_connect(shutter, "clicked", G_CALLBACK(on_shutter_clicked), NULL);
	g_signal_connect(switch_btn, "clicked", G_CALLBACK(on_camera_switch_clicked), NULL);
	g_signal_connect(preview, "draw", G_CALLBACK(preview_draw), NULL);
	g_signal_connect(preview, "configure-event", G_CALLBACK(preview_configure), NULL);

	GtkCssProvider *provider = gtk_css_provider_new();
	if (access("camera.css", F_OK) != -1) {
		gtk_css_provider_load_from_path(provider, "camera.css", NULL);
	} else {
		gtk_css_provider_load_from_path(provider, "/usr/share/camera/ui/camera.css", NULL);
	}
	GtkStyleContext *context = gtk_widget_get_style_context(preview_box);
	gtk_style_context_add_provider(context,
		GTK_STYLE_PROVIDER(provider),
		GTK_STYLE_PROVIDER_PRIORITY_USER);

	int result = ini_parse(argv[1], config_ini_handler, NULL);
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

	if (find_media_fd() == -1) {
		g_printerr("Could not find the media node\n");
		return 1;
	}
	if (find_cameras() == -1) {
		g_printerr("Could not find the cameras\n");
		return 1;
	}
	setup_rear();

	int fd = open(dev_name, O_RDWR);
	if (fd == -1) {
		g_printerr("Error opening video device: %s\n", dev_name);
		return 1;
	}

	video_fd = fd;

	init_device(fd);
	start_capturing(fd);

	// Get a new frame every 34ms ~30fps
	printf("window show\n");
	gtk_widget_show(window);
	g_idle_add((GSourceFunc)get_frame, NULL);
	gtk_main();
	return 0;
}
