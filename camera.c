#include "camera.h"

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define MAX_VIDEO_BUFFERS 20

static const char *pixel_format_names[MP_PIXEL_FMT_MAX] = {
    "unsupported",
    "BGGR8",
    "GBRG8",
    "GRBG8",
    "RGGB8",
    "BGGR10P",
    "GBRG10P",
    "GRBG10P",
    "RGGB10P",
    "UYVY",
    "YUYV",
};

const char *mp_pixel_format_to_str(uint32_t pixel_format)
{
    g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, "INVALID");
    return pixel_format_names[pixel_format];
}

MPPixelFormat mp_pixel_format_from_str(const char *name)
{
    for (MPPixelFormat i = 0; i < MP_PIXEL_FMT_MAX; ++i) {
        if (strcasecmp(pixel_format_names[i], name) == 0) {
            return i;
        }
    }
    g_return_val_if_reached(MP_PIXEL_FMT_UNSUPPORTED);
}

static const uint32_t pixel_format_v4l_pixel_formats[MP_PIXEL_FMT_MAX] = {
    0,
    V4L2_PIX_FMT_SBGGR8,
    V4L2_PIX_FMT_SGBRG8,
    V4L2_PIX_FMT_SGRBG8,
    V4L2_PIX_FMT_SRGGB8,
    V4L2_PIX_FMT_SBGGR10P,
    V4L2_PIX_FMT_SGBRG10P,
    V4L2_PIX_FMT_SGRBG10P,
    V4L2_PIX_FMT_SRGGB10P,
    V4L2_PIX_FMT_UYVY,
    V4L2_PIX_FMT_YUYV,
};

uint32_t mp_pixel_format_to_v4l_pixel_format(MPPixelFormat pixel_format)
{
    g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
    return pixel_format_v4l_pixel_formats[pixel_format];
}

MPPixelFormat mp_pixel_format_from_v4l_pixel_format(uint32_t v4l_pixel_format)
{
    for (MPPixelFormat i = 0; i < MP_PIXEL_FMT_MAX; ++i) {
        if (pixel_format_v4l_pixel_formats[i] == v4l_pixel_format) {
            return i;
        }
    }
    return MP_PIXEL_FMT_UNSUPPORTED;
}

static const uint32_t pixel_format_v4l_bus_codes[MP_PIXEL_FMT_MAX] = {
    0,
    MEDIA_BUS_FMT_SBGGR8_1X8,
    MEDIA_BUS_FMT_SGBRG8_1X8,
    MEDIA_BUS_FMT_SGRBG8_1X8,
    MEDIA_BUS_FMT_SRGGB8_1X8,
    MEDIA_BUS_FMT_SBGGR10_1X10,
    MEDIA_BUS_FMT_SGBRG10_1X10,
    MEDIA_BUS_FMT_SGRBG10_1X10,
    MEDIA_BUS_FMT_SRGGB10_1X10,
    MEDIA_BUS_FMT_UYVY8_2X8,
    MEDIA_BUS_FMT_YUYV8_2X8,
};

uint32_t mp_pixel_format_to_v4l_bus_code(MPPixelFormat pixel_format)
{
    g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
    return pixel_format_v4l_bus_codes[pixel_format];
}

MPPixelFormat mp_pixel_format_from_v4l_bus_code(uint32_t v4l_bus_code)
{
    for (MPPixelFormat i = 0; i < MP_PIXEL_FMT_MAX; ++i) {
        if (pixel_format_v4l_bus_codes[i] == v4l_bus_code) {
            return i;
        }
    }
    return MP_PIXEL_FMT_UNSUPPORTED;
}

uint32_t mp_pixel_format_bits_per_pixel(MPPixelFormat pixel_format)
{
    g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
    switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8: return 8;
        case MP_PIXEL_FMT_BGGR10P:
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P: return 10;
        case MP_PIXEL_FMT_UYVY:
        case MP_PIXEL_FMT_YUYV: return 16;
        default: return 0;
    }
}

uint32_t mp_pixel_format_width_to_bytes(MPPixelFormat pixel_format, uint32_t width)
{
    uint32_t bits_per_pixel = mp_pixel_format_bits_per_pixel(pixel_format);
    uint64_t bits_per_width = width * (uint64_t) bits_per_pixel;

    uint64_t remainder = bits_per_width % 8;
    if (remainder == 0)
        return bits_per_width / 8;

    return (bits_per_width + 8 - remainder) / 8;
}

uint32_t mp_pixel_format_width_to_colors(MPPixelFormat pixel_format, uint32_t width)
{
    g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
    switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8: return width / 2;
        case MP_PIXEL_FMT_BGGR10P:
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P: return width / 2 * 5;
        case MP_PIXEL_FMT_UYVY:
        case MP_PIXEL_FMT_YUYV: return width;
        default: return 0;
    }
}

uint32_t mp_pixel_format_height_to_colors(MPPixelFormat pixel_format, uint32_t height)
{
    g_return_val_if_fail(pixel_format < MP_PIXEL_FMT_MAX, 0);
    switch (pixel_format) {
        case MP_PIXEL_FMT_BGGR8:
        case MP_PIXEL_FMT_GBRG8:
        case MP_PIXEL_FMT_GRBG8:
        case MP_PIXEL_FMT_RGGB8:
        case MP_PIXEL_FMT_BGGR10P:
        case MP_PIXEL_FMT_GBRG10P:
        case MP_PIXEL_FMT_GRBG10P:
        case MP_PIXEL_FMT_RGGB10P: return height / 2;
        case MP_PIXEL_FMT_UYVY:
        case MP_PIXEL_FMT_YUYV: return height;
        default: return 0;
    }
}

bool mp_camera_mode_is_equivalent(const MPCameraMode *m1, const MPCameraMode *m2)
{
    return m1->pixel_format == m2->pixel_format
        && m1->frame_interval.numerator == m2->frame_interval.numerator
        && m1->frame_interval.denominator == m2->frame_interval.denominator
        && m1->width == m2->width
        && m1->height == m2->height;
}

static void errno_printerr(const char *s)
{
    g_printerr("MPCamera: %s error %d, %s\n", s, errno, strerror(errno));
}

static int xioctl(int fd, int request, void *arg)
{
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

struct video_buffer {
    uint32_t length;
    uint8_t *data;
};

struct _MPCamera {
    int video_fd;
    int subdev_fd;

    bool has_set_mode;
    MPCameraMode current_mode;

    struct video_buffer buffers[MAX_VIDEO_BUFFERS];
    uint32_t num_buffers;

    bool use_mplane;
};

MPCamera *mp_camera_new(int video_fd, int subdev_fd)
{
    g_return_val_if_fail(video_fd != -1, NULL);

    // Query capabilities
    struct v4l2_capability cap;
    if (xioctl(video_fd, VIDIOC_QUERYCAP, &cap) == -1) {
        return NULL;
    }

    // Check whether this is a video capture device
    bool use_mplane;
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        use_mplane = true;
        printf("!!\n");
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        use_mplane = false;
    } else {
        return NULL;
    }

    MPCamera *camera = malloc(sizeof(MPCamera));
    camera->video_fd = video_fd;
    camera->subdev_fd = subdev_fd;
    camera->has_set_mode = false;
    camera->num_buffers = 0;
    camera->use_mplane = use_mplane;
    return camera;
}

void mp_camera_free(MPCamera *camera)
{
    g_warn_if_fail(camera->num_buffers == 0);
    if (camera->num_buffers != 0) {
        mp_camera_stop_capture(camera);
    }

    free(camera);
}

bool mp_camera_is_subdev(MPCamera *camera)
{
    return camera->subdev_fd != -1;
}

int mp_camera_get_video_fd(MPCamera *camera)
{
    return camera->video_fd;
}

int mp_camera_get_subdev_fd(MPCamera *camera)
{
    return camera->subdev_fd;
}

static bool camera_mode_impl(MPCamera *camera, int request, MPCameraMode *mode)
{
    uint32_t pixfmt = mp_pixel_format_from_v4l_pixel_format(mode->pixel_format);
    struct v4l2_format fmt = {};
    if (camera->use_mplane) {
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        fmt.fmt.pix_mp.width = mode->width;
        fmt.fmt.pix_mp.height = mode->height;
        fmt.fmt.pix_mp.pixelformat = pixfmt;
        fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
    } else {
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = mode->width;
        fmt.fmt.pix.height = mode->height;
        fmt.fmt.pix.pixelformat = pixfmt;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
    }

    if (xioctl(camera->video_fd, request, &fmt) == -1) {
        return false;
    }

    if (camera->use_mplane) {
        mode->width = fmt.fmt.pix_mp.width;
        mode->height = fmt.fmt.pix_mp.height;
        mode->pixel_format = mp_pixel_format_from_v4l_pixel_format(
            fmt.fmt.pix_mp.pixelformat);
    } else {
        mode->width = fmt.fmt.pix.width;
        mode->height = fmt.fmt.pix.height;
        mode->pixel_format = mp_pixel_format_from_v4l_pixel_format(
            fmt.fmt.pix.pixelformat);
    }

    return true;
}

bool mp_camera_try_mode(MPCamera *camera, MPCameraMode *mode)
{
    if (!camera_mode_impl(camera, VIDIOC_TRY_FMT, mode)) {
        errno_printerr("VIDIOC_S_FMT");
        return false;
    }
    return true;
}

const MPCameraMode *mp_camera_get_mode(const MPCamera *camera)
{
    return &camera->current_mode;
}

bool mp_camera_set_mode(MPCamera *camera, MPCameraMode *mode)
{
    // Set the mode in the subdev the camera is one
    if (mp_camera_is_subdev(camera))
    {
        struct v4l2_subdev_frame_interval interval = {};
        interval.pad = 0;
        interval.interval = mode->frame_interval;
        if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &interval) == -1) {
            errno_printerr("VIDIOC_SUBDEV_S_FRAME_INTERVAL");
            return false;
        }

        bool did_set_frame_rate =
            interval.interval.numerator == mode->frame_interval.numerator
            && interval.interval.denominator == mode->frame_interval.denominator;

        struct v4l2_subdev_format fmt = {};
        fmt.pad = 0;
        fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        fmt.format.width = mode->width;
        fmt.format.height = mode->height;
        fmt.format.code = mp_pixel_format_to_v4l_bus_code(mode->pixel_format);
        fmt.format.field = V4L2_FIELD_ANY;
        if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_S_FMT, &fmt) == -1) {
            errno_printerr("VIDIOC_SUBDEV_S_FMT");
            return false;
        }

        // Some drivers like ov5640 don't allow you to set the frame format with
        // too high a frame-rate, but that means the frame-rate won't be set
        // after the format change. So we need to try again here if we didn't
        // succeed before. Ideally we'd be able to set both at once.
        if (!did_set_frame_rate)
        {
            interval.interval = mode->frame_interval;
            if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_S_FRAME_INTERVAL, &interval) == -1) {
                errno_printerr("VIDIOC_SUBDEV_S_FRAME_INTERVAL");
                return false;
            }
        }

        // Update the mode
        mode->pixel_format = mp_pixel_format_from_v4l_bus_code(fmt.format.code);
        mode->frame_interval = interval.interval;
        mode->width = fmt.format.width;
        mode->height = fmt.format.height;
    }

    // Set the mode for the video device
    {
        if (!camera_mode_impl(camera, VIDIOC_S_FMT, mode)) {
            errno_printerr("VIDIOC_S_FMT");
            return false;
        }
    }

    camera->has_set_mode = true;
    camera->current_mode = *mode;

    return true;
}

bool mp_camera_start_capture(MPCamera *camera)
{
    g_return_val_if_fail(camera->has_set_mode, false);
    g_return_val_if_fail(camera->num_buffers == 0, false);

    // Start by requesting buffers
    struct v4l2_requestbuffers req = {};
    req.count = MAX_VIDEO_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(camera->video_fd, VIDIOC_REQBUFS, &req) == -1) {
        errno_printerr("VIDIOC_REQBUFS");
        return false;
    }

    if (req.count < 2) {
        g_printerr("Insufficient buffer memory. Only %d buffers available.\n",
                   req.count);
        goto error;
    }

    for (uint32_t i = 0; i < req.count; ++i) {
        // Query each buffer and mmap it
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };

        struct v4l2_plane planes[1];
        if (camera->use_mplane) {
            buf.m.planes = planes;
            buf.length = 1;
        }

        if (xioctl(camera->video_fd, VIDIOC_QUERYBUF, &buf) == -1) {
            errno_printerr("VIDIOC_QUERYBUF");
            break;
        }

        if (camera->use_mplane) {
            camera->buffers[i].length = planes[0].length;
            camera->buffers[i].data = mmap(
                NULL,
                planes[0].length,
                PROT_READ,
                MAP_SHARED,
                camera->video_fd,
                planes[0].m.mem_offset);
        } else {
            camera->buffers[i].length = buf.length;
            camera->buffers[i].data = mmap(
                NULL,
                buf.length,
                PROT_READ,
                MAP_SHARED,
                camera->video_fd,
                buf.m.offset);
        }

        if (camera->buffers[i].data == MAP_FAILED) {
            errno_printerr("mmap");
            break;
        }

        ++camera->num_buffers;
    }

    if (camera->num_buffers != req.count) {
        g_printerr("Unable to map all buffers\n");
        goto error;
    }

    for (uint32_t i = 0; i < camera->num_buffers; ++i) {
        struct v4l2_buffer buf = {
            .type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
            .memory = V4L2_MEMORY_MMAP,
            .index = i,
        };

        struct v4l2_plane planes[1];
        if (camera->use_mplane) {
            buf.m.planes = planes;
            buf.length = 1;
        }

        // Queue the buffer for capture
        if (xioctl(camera->video_fd, VIDIOC_QBUF, &buf) == -1) {
            errno_printerr("VIDIOC_QBUF");
            goto error;
        }
    }

    // Start capture
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(camera->video_fd, VIDIOC_STREAMON, &type) == -1) {
        errno_printerr("VIDIOC_STREAMON");
        goto error;
    }

    return true;

error:
    // Unmap any mapped buffers
    assert(camera->num_buffers <= MAX_VIDEO_BUFFERS);
    for (uint32_t i = 0; i < camera->num_buffers; ++i) {
        if (munmap(camera->buffers[i].data, camera->buffers[i].length) == -1) {
            errno_printerr("munmap");
        }
    }

    // Reset allocated buffers
    {
        struct v4l2_requestbuffers req = {};
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (xioctl(camera->video_fd, VIDIOC_REQBUFS, &req) == -1) {
            errno_printerr("VIDIOC_REQBUFS");
        }
    }

    return false;
}

bool mp_camera_stop_capture(MPCamera *camera)
{
    g_return_val_if_fail(camera->num_buffers > 0, false);

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(camera->video_fd, VIDIOC_STREAMOFF, &type) == -1) {
        errno_printerr("VIDIOC_STREAMOFF");
    }

    assert(camera->num_buffers <= MAX_VIDEO_BUFFERS);
    for (int i = 0; i < camera->num_buffers; ++i) {
        if (munmap(camera->buffers[i].data, camera->buffers[i].length) == -1) {
            errno_printerr("munmap");
        }
    }

    camera->num_buffers = 0;

    struct v4l2_requestbuffers req = {};
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(camera->video_fd, VIDIOC_REQBUFS, &req) == -1) {
        errno_printerr("VIDIOC_REQBUFS");
    }

    return true;
}

bool mp_camera_is_capturing(MPCamera *camera)
{
    return camera->num_buffers > 0;
}

bool mp_camera_capture_image(MPCamera *camera, void (*callback)(MPImage, void *), void *user_data)
{
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    struct v4l2_plane planes[1];
    if (camera->use_mplane) {
        buf.m.planes = planes;
        buf.length = 1;
    }

    if (xioctl(camera->video_fd, VIDIOC_DQBUF, &buf) == -1) {
        switch (errno) {
            case EAGAIN:
                return true;
            case EIO:
                /* Could ignore EIO, see spec. */
                /* fallthrough */
            default:
                errno_printerr("VIDIOC_DQBUF");
                return false;
        }
    }

    uint32_t pixel_format = camera->current_mode.pixel_format;
    uint32_t width = camera->current_mode.width;
    uint32_t height = camera->current_mode.height;

    uint32_t bytesused;
    if (camera->use_mplane) {
        bytesused = planes[0].bytesused;
    } else {
        bytesused = buf.bytesused;
    }

    assert(bytesused == mp_pixel_format_width_to_bytes(pixel_format, width) * height);
    assert(bytesused == camera->buffers[buf.index].length);

    MPImage image = {
        .pixel_format = pixel_format,
        .width = width,
        .height = height,
        .data = camera->buffers[buf.index].data,
    };

    callback(image, user_data);

    // The callback may have stopped the capture, only queue the buffer if we're
    // still capturing.
    if (mp_camera_is_capturing(camera)) {
        if (xioctl(camera->video_fd, VIDIOC_QBUF, &buf) == -1) {
            errno_printerr("VIDIOC_QBUF");
            return false;
        }
    }

    return true;
}

struct _MPCameraModeList {
    MPCameraMode mode;
    MPCameraModeList *next;
};

static MPCameraModeList *
get_subdev_modes(MPCamera *camera, bool (*check)(MPCamera *, MPCameraMode *))
{
    MPCameraModeList *item = NULL;

    for (uint32_t fmt_index = 0;; ++fmt_index) {
        struct v4l2_subdev_mbus_code_enum fmt = {};
        fmt.index = fmt_index;
        fmt.pad = 0;
        fmt.which = V4L2_SUBDEV_FORMAT_TRY;
        if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_ENUM_MBUS_CODE, &fmt) == -1) {
            if (errno != EINVAL) {
                errno_printerr("VIDIOC_SUBDEV_ENUM_MBUS_CODE");
            }
            break;
        }

        // Skip unsupported formats
        uint32_t format = mp_pixel_format_from_v4l_bus_code(fmt.code);
        if (format == MP_PIXEL_FMT_UNSUPPORTED) {
            continue;
        }

        for (uint32_t frame_index = 0;; ++frame_index) {
            struct v4l2_subdev_frame_size_enum frame = {};
            frame.index = frame_index;
            frame.pad = 0;
            frame.code = fmt.code;
            frame.which = V4L2_SUBDEV_FORMAT_TRY;
            if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_ENUM_FRAME_SIZE, &frame) == -1) {
                if (errno != EINVAL) {
                    errno_printerr("VIDIOC_SUBDEV_ENUM_FRAME_SIZE");
                }
                break;
            }

            // TODO: Handle other types
            if (frame.min_width != frame.max_width
                || frame.min_height != frame.max_height) {
                break;
            }

            for (uint32_t interval_index = 0;; ++interval_index) {
                struct v4l2_subdev_frame_interval_enum interval = {};
                interval.index = interval_index;
                interval.pad = 0;
                interval.code = fmt.code;
                interval.width = frame.max_width;
                interval.height = frame.max_height;
                interval.which = V4L2_SUBDEV_FORMAT_TRY;
                if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL, &interval) == -1) {
                    if (errno != EINVAL) {
                        errno_printerr("VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL");
                    }
                    break;
                }

                MPCameraMode mode = {
                    .pixel_format = format,
                    .frame_interval = interval.interval,
                    .width = frame.max_width,
                    .height = frame.max_height,
                };

                if (!check(camera, &mode)) {
                    continue;
                }

                MPCameraModeList *new_item = malloc(sizeof(MPCameraModeList));
                new_item->mode = mode;
                new_item->next = item;
                item = new_item;
            }
        }
    }

    return item;
}

static MPCameraModeList *
get_video_modes(MPCamera *camera, bool (*check)(MPCamera *, MPCameraMode *))
{
    MPCameraModeList *item = NULL;

    for (uint32_t fmt_index = 0;; ++fmt_index) {
        struct v4l2_fmtdesc fmt = {};
        fmt.index = fmt_index;
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(camera->video_fd, VIDIOC_ENUM_FMT, &fmt) == -1) {
            if (errno != EINVAL) {
                errno_printerr("VIDIOC_ENUM_FMT");
            }
            break;
        }

        // Skip unsupported formats
        uint32_t format = mp_pixel_format_from_v4l_pixel_format(fmt.pixelformat);
        if (format == MP_PIXEL_FMT_UNSUPPORTED) {
            continue;
        }

        for (uint32_t frame_index = 0;; ++frame_index) {
            struct v4l2_frmsizeenum frame = {};
            frame.index = frame_index;
            frame.pixel_format = fmt.pixelformat;
            if (xioctl(camera->video_fd, VIDIOC_ENUM_FRAMESIZES, &frame) == -1) {
                if (errno != EINVAL) {
                    errno_printerr("VIDIOC_ENUM_FRAMESIZES");
                }
                break;
            }

            // TODO: Handle other types
            if (frame.type != V4L2_FRMSIZE_TYPE_DISCRETE) {
                break;
            }

            for (uint32_t interval_index = 0;; ++interval_index) {
                struct v4l2_frmivalenum interval = {};
                interval.index = interval_index;
                interval.pixel_format = fmt.pixelformat;
                interval.width = frame.discrete.width;
                interval.height = frame.discrete.height;
                if (xioctl(camera->video_fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) == -1) {
                    if (errno != EINVAL) {
                        errno_printerr("VIDIOC_ENUM_FRAMESIZES");
                    }
                    break;
                }

                // TODO: Handle other types
                if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                    break;
                }

                MPCameraMode mode = {
                    .pixel_format = format,
                    .frame_interval = interval.discrete,
                    .width = frame.discrete.width,
                    .height = frame.discrete.height,
                };

                if (!check(camera, &mode)) {
                    continue;
                }

                MPCameraModeList *new_item = malloc(sizeof(MPCameraModeList));
                new_item->mode = mode;
                new_item->next = item;
                item = new_item;
            }
        }
    }

    return item;
}

static bool all_modes(MPCamera *camera, MPCameraMode *mode)
{
    return true;
}

static bool available_modes(MPCamera *camera, MPCameraMode *mode)
{
    MPCameraMode attempt = *mode;
    return mp_camera_try_mode(camera, &attempt)
        && mp_camera_mode_is_equivalent(mode, &attempt);
}

MPCameraModeList *mp_camera_list_supported_modes(MPCamera *camera)
{
    if (mp_camera_is_subdev(camera)) {
        return get_subdev_modes(camera, all_modes);
    } else {
        return get_video_modes(camera, all_modes);
    }
}

MPCameraModeList *mp_camera_list_available_modes(MPCamera *camera)
{
    if (mp_camera_is_subdev(camera)) {
        return get_subdev_modes(camera, available_modes);
    } else {
        return get_video_modes(camera, available_modes);
    }
}

MPCameraMode *mp_camera_mode_list_get(MPCameraModeList *list)
{
    g_return_val_if_fail(list, NULL);
    return &list->mode;
}

MPCameraModeList *mp_camera_mode_list_next(MPCameraModeList *list)
{
    g_return_val_if_fail(list, NULL);
    return list->next;
}

void mp_camera_mode_list_free(MPCameraModeList *list)
{
    while (list) {
        MPCameraModeList *tmp = list;
        list = tmp->next;
        free(tmp);
    }
}
