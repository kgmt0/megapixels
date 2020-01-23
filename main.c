#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <asm/errno.h>
#include <gtk/gtk.h>
#include "ini.h"

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
static char *rear_dev_name;
static char *front_dev_name;
static char *dev_name;
static enum io_method io = IO_METHOD_MMAP;

static int preview_width = -1;
static int preview_height = -1;
static int preview_fmt = V4L2_PIX_FMT_RGB24;

GObject *preview_image;

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
	switch (io) {
	case IO_METHOD_READ:
		/* Nothing to do. */
		break;
	case IO_METHOD_MMAP:
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
		break;
	case IO_METHOD_USERPTR:
		for (int i = 0; i < n_buffers; ++i) {
			struct v4l2_buffer buf = {
				.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
				.memory = V4L2_MEMORY_USERPTR,
				.index = i,
			};
			buf.m.userptr = (unsigned long)buffers[i].start;
			buf.length = buffers[i].length;

			if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
				errno_exit("VIDIOC_QBUF");
			}
		}

		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (xioctl(fd, VIDIOC_STREAMON, &type) == -1) {
			errno_exit("VIDIOC_STREAMON");
		}
		break;
	}
}

static void
init_mmap(int fd)
{
	struct v4l2_requestbuffers req = { 0 };
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

	switch (io) {
	case IO_METHOD_READ:
		if (!(cap.capabilities & V4L2_CAP_READWRITE)) {
			fprintf(stderr, "%s does not support read i/o\n",
					dev_name);
			exit(EXIT_FAILURE);
		}
		break;
	case IO_METHOD_MMAP:
	case IO_METHOD_USERPTR:
		if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
			fprintf(stderr, "%s does not support streaming i/o\n",
					dev_name);
			exit(EXIT_FAILURE);
		}
		break;
	}

	/* Select video input, video standard and tune here. */
	struct v4l2_cropcap cropcap = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};

	struct v4l2_crop crop = { 0 };
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
	if (preview_width > 0) {
		fmt.fmt.pix.width = preview_width;
		fmt.fmt.pix.height = preview_height;
		fmt.fmt.pix.pixelformat = preview_fmt;
		fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;

		if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
			errno_exit("VIDIOC_S_FMT");
		}

		/* Note VIDIOC_S_FMT may change width and height. */
	} else {
		/* Preserve original settings as set by v4l2-ctl for example */
		if (xioctl(fd, VIDIOC_G_FMT, &fmt) == -1) {
			errno_exit("VIDIOC_G_FMT");
		}
	}

	/* Buggy driver paranoia. */
	unsigned int min = fmt.fmt.pix.width * 2;
	if (fmt.fmt.pix.bytesperline < min) {
		fmt.fmt.pix.bytesperline = min;
	}
	min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
	if (fmt.fmt.pix.sizeimage < min) {
		fmt.fmt.pix.sizeimage = min;
	}

	switch (io) {
	case IO_METHOD_READ:
		//init_read(fmt.fmt.pix.sizeimage);
		break;

	case IO_METHOD_MMAP:
		init_mmap(fd);
		break;

	case IO_METHOD_USERPTR:
		//init_userp(fmt.fmt.pix.sizeimage);
		break;
	}
}

static void
process_image(const void *p, int size)
{
	GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data(p, GDK_COLORSPACE_RGB,
			FALSE, 8, 640, 480, 2 * 640, NULL, NULL);
	gtk_image_set_from_pixbuf(preview_image, pixbuf);
}

static int
read_frame(int fd)
{
	struct v4l2_buffer buf = { 0 };

	switch (io) {
	case IO_METHOD_READ:
		if (read(fd, buffers[0].start, buffers[0].length) == -1) {
			switch (errno) {
			case EAGAIN:
				return 0;
			case EIO:
				/* Could ignore EIO, see spec. */
				/* fallthrough */
			default:
				errno_exit("read");
				break;
			}
		}
		process_image(buffers[0].start, buffers[0].length);
		break;
	case IO_METHOD_MMAP:
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
		break;
	case IO_METHOD_USERPTR:
		buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buf.memory = V4L2_MEMORY_USERPTR;
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
		unsigned int i;
		for (i = 0; i < n_buffers; ++i) {
			if (buf.m.userptr == (unsigned long)buffers[i].start
					&& buf.length == buffers[i].length) {
				break;
			}
		}

		//assert(i < n_buffers);

		process_image((void *)buf.m.userptr, buf.bytesused);
		if (xioctl(fd, VIDIOC_QBUF, &buf) == -1) {
			errno_exit("VIDIOC_QBUF");
		}
		break;
	}

	return 1;
}

static void
get_frame(int fd)
{
	while (1) {
		fd_set fds;
		struct timeval tv;
		int r;

		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		/* Timeout. */
		tv.tv_sec = 2;
		tv.tv_usec = 0;

		r = select(fd + 1, &fds, NULL, NULL, &tv);

		if (r == -1) {
			if (EINTR == errno) {
				continue;
			}
			errno_exit("select");
		} else if (r == 0) {
			fprintf(stderr, "select timeout\\n");
			exit(EXIT_FAILURE);
		}

		if (read_frame(fd)) {
			break;
		}
		/* EAGAIN - continue select loop. */
	}
}

static int
config_ini_handler(void *user, const char *section, const char *name,
		const char *value) {
	if (strcmp(section, "preview") == 0) {
		if (strcmp(name, "width") == 0) {
			preview_width = strtol(value, NULL, 10);
		} else if (strcmp(name, "height") == 0) {
			preview_height = strtol(value, NULL, 10);
		} else if (strcmp(name, "fmt") == 0) {
			if (strcmp(value, "RGB") == 0){
				preview_fmt = V4L2_PIX_FMT_RGB24;
			} else if (strcmp(value, "UYVY8") == 0) {
				preview_fmt = V4L2_PIX_FMT_UYVY;
			} else if (strcmp(value, "JPEG") == 0) {
				preview_fmt = V4L2_PIX_FMT_JPEG;
			} else if (strcmp(value, "NV12") == 0) {
				preview_fmt = V4L2_PIX_FMT_NV12;
			} else {
				g_printerr("Unsupported pixelformat %s\n", value);
				exit(1);
			}
			preview_fmt = strtol(value, NULL, 10);
		} else {
			g_printerr("Unknown key '%s' in [preview]\n", name);
			exit(1);
		}
	} else if (strcmp(section, "device") == 0) {
		if (strcmp(name, "rear") == 0) {
			rear_dev_name = strdup(value);
		} else if (strcmp(name, "front") == 0) {
			front_dev_name = strdup(value);
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
main(int argc, char *argv[])
{
	if (argc != 2) {
		g_printerr("Usage: camera configfile\n");
		return 1;
	}

	GError *error = NULL;
	gtk_init(&argc, &argv);
	GtkBuilder *builder = gtk_builder_new();
	if (gtk_builder_add_from_file(builder, "camera.glade", &error) == 0) {
		g_printerr("Error loading file: %s\n", error->message);
		g_clear_error(&error);
		return 1;
	}

	GObject *window = gtk_builder_get_object(builder, "window");
	GObject *preview_box = gtk_builder_get_object(builder, "preview_box");
	preview_image = gtk_builder_get_object(builder, "preview");
	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkCssProvider *provider = gtk_css_provider_new();
	gtk_css_provider_load_from_path(provider, "camera.css", NULL);
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

	dev_name = rear_dev_name;

	int fd = open(dev_name, O_RDWR);
	if (fd == -1) {
		g_printerr("Error opening video device: %s\n", dev_name);
		return 1;
	}

	init_device(fd);
	start_capturing(fd);
	get_frame(fd);

	gtk_widget_show(window);
	gtk_main();
	return 0;
}
