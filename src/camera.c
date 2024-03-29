#include "camera.h"
#include "mode.h"

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <linux/v4l2-subdev.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_VIDEO_BUFFERS 20
#define MAX_BG_TASKS 8

static void
errno_printerr(const char *s)
{
        g_printerr("MPCamera: %s error %d, %s\n", s, errno, strerror(errno));
}

static int
xioctl(int fd, int request, void *arg)
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
        int fd;
};

struct _MPCamera {
        int video_fd;
        int subdev_fd;
        int bridge_fd;

        bool has_set_mode;
        MPMode current_mode;

        struct video_buffer buffers[MAX_VIDEO_BUFFERS];
        uint32_t num_buffers;

        // keeping track of background task child-PIDs for cleanup code
        int child_bg_pids[MAX_BG_TASKS];

        bool use_mplane;
};

MPCamera *
mp_camera_new(int video_fd, int subdev_fd, int bridge_fd)
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
        } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                use_mplane = false;
        } else {
                return NULL;
        }

        MPCamera *camera = malloc(sizeof(MPCamera));
        camera->video_fd = video_fd;
        camera->subdev_fd = subdev_fd;
        camera->bridge_fd = bridge_fd;
        camera->has_set_mode = false;
        camera->num_buffers = 0;
        camera->use_mplane = use_mplane;
        memset(camera->child_bg_pids,
               0,
               sizeof(camera->child_bg_pids[0]) * MAX_BG_TASKS);
        return camera;
}

void
mp_camera_free(MPCamera *camera)
{
        mp_camera_wait_bg_tasks(camera);

        g_warn_if_fail(camera->num_buffers == 0);
        if (camera->num_buffers != 0) {
                mp_camera_stop_capture(camera);
        }

        free(camera);
}

void
mp_camera_add_bg_task(MPCamera *camera, pid_t pid)
{
        int status;
        while (true) {
                for (size_t i = 0; i < MAX_BG_TASKS; ++i) {
                        if (camera->child_bg_pids[i] == 0) {
                                camera->child_bg_pids[i] = pid;
                                return;
                        } else {
                                // error == -1, still running == 0
                                if (waitpid(camera->child_bg_pids[i],
                                            &status,
                                            WNOHANG) <= 0)
                                        continue; // consider errored wait still
                                                  // running

                                if (WIFEXITED(status)) {
                                        // replace exited
                                        camera->child_bg_pids[i] = pid;
                                        return;
                                }
                        }
                }

                // wait for any status change on child processes
                pid_t changed = waitpid(-1, &status, 0);
                if (WIFEXITED(status)) {
                        // some child exited
                        for (size_t i = 0; i < MAX_BG_TASKS; ++i) {
                                if (camera->child_bg_pids[i] == changed) {
                                        camera->child_bg_pids[i] = pid;
                                        return;
                                }
                        }
                }

                // no luck, repeat and check if something exited maybe
        }
}

void
mp_camera_wait_bg_tasks(MPCamera *camera)
{
        for (size_t i = 0; i < MAX_BG_TASKS; ++i) {
                if (camera->child_bg_pids[i] != 0) {
                        // ignore errors
                        waitpid(camera->child_bg_pids[i], NULL, 0);
                }
        }
}

bool
mp_camera_check_task_complete(MPCamera *camera, pid_t pid)
{
        // this method is potentially unsafe because pid could already be reused at
        // this point, but extremely unlikely so we won't implement this.
        int status;

        if (pid == 0)
                return true;

        // ignore errors (-1), no exit == 0
        int pidchange = waitpid(pid, &status, WNOHANG);
        if (pidchange == -1) // error or exists and runs
                return false;

        if (WIFEXITED(status)) {
                for (size_t i = 0; i < MAX_BG_TASKS; ++i) {
                        if (camera->child_bg_pids[i] == pid) {
                                camera->child_bg_pids[i] = 0;
                                break;
                        }
                }
                return true;
        } else {
                return false;
        }
}

bool
mp_camera_is_subdev(MPCamera *camera)
{
        return camera->subdev_fd != -1;
}

int
mp_camera_get_video_fd(MPCamera *camera)
{
        return camera->video_fd;
}

int
mp_camera_get_subdev_fd(MPCamera *camera)
{
        return camera->subdev_fd;
}

static enum v4l2_buf_type
get_buf_type(MPCamera *camera)
{
        if (camera->use_mplane) {
                return V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        }
        return V4L2_BUF_TYPE_VIDEO_CAPTURE;
}

static bool
camera_mode_impl(MPCamera *camera, int request, MPMode *mode)
{
        uint32_t pixfmt = mp_pixel_format_to_v4l_pixel_format(mode->pixel_format);
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

bool
mp_camera_try_mode(MPCamera *camera, MPMode *mode)
{
        if (!camera_mode_impl(camera, VIDIOC_TRY_FMT, mode)) {
                errno_printerr("VIDIOC_S_FMT");
                return false;
        }
        return true;
}

const MPMode *
mp_camera_get_mode(const MPCamera *camera)
{
        return &camera->current_mode;
}

bool
mp_camera_set_mode(MPCamera *camera, MPMode *mode)
{
        // Set the mode in the subdev the camera is one
        if (mp_camera_is_subdev(camera)) {
                struct v4l2_subdev_frame_interval interval = {};
                interval.pad = 0;
                interval.interval = mode->frame_interval;
                if (xioctl(camera->subdev_fd,
                           VIDIOC_SUBDEV_S_FRAME_INTERVAL,
                           &interval) == -1) {
                        errno_printerr("VIDIOC_SUBDEV_S_FRAME_INTERVAL");
                }

                bool did_set_frame_rate = interval.interval.numerator ==
                                                  mode->frame_interval.numerator &&
                                          interval.interval.denominator ==
                                                  mode->frame_interval.denominator;

                struct v4l2_subdev_format fmt = {};
                fmt.pad = 0;
                fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
                fmt.format.width = mode->width;
                fmt.format.height = mode->height;
                fmt.format.code =
                        mp_pixel_format_to_v4l_bus_code(mode->pixel_format);
                fmt.format.field = V4L2_FIELD_ANY;
                if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_S_FMT, &fmt) == -1) {
                        errno_printerr("VIDIOC_SUBDEV_S_FMT");
                        return false;
                }

                // sun6i-csi-bridge will return EINVAL when trying to read
                // frames if the resolution it's configured with doesn't match
                // the resolution of its input.
                if (camera->bridge_fd > 0) {
                        struct v4l2_subdev_format bridge_fmt = fmt;

                        if (xioctl(camera->bridge_fd,
                                   VIDIOC_SUBDEV_S_FMT,
                                   &bridge_fmt) == -1) {
                                errno_printerr("VIDIOC_SUBDEV_S_FMT");
                                return false;
                        }

                        if (fmt.format.width != bridge_fmt.format.width ||
                            fmt.format.height != bridge_fmt.format.height) {
                                g_printerr("Bridge format resolution mismatch\n");
                                return false;
                        }
                }

                // Some drivers like ov5640 don't allow you to set the frame format
                // with too high a frame-rate, but that means the frame-rate won't be
                // set after the format change. So we need to try again here if we
                // didn't succeed before. Ideally we'd be able to set both at once.
                if (!did_set_frame_rate) {
                        interval.interval = mode->frame_interval;
                        if (xioctl(camera->subdev_fd,
                                   VIDIOC_SUBDEV_S_FRAME_INTERVAL,
                                   &interval) == -1) {
                                errno_printerr("VIDIOC_SUBDEV_S_FRAME_INTERVAL");
                        }
                }

                // Update the mode

                // TODO: Some how the format gets changed to YUYV if this isn't
                // commented out.
                //mode->pixel_format =
                //        mp_pixel_format_from_v4l_bus_code(fmt.format.code);

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

bool
mp_camera_start_capture(MPCamera *camera)
{
        g_return_val_if_fail(camera->has_set_mode, false);
        g_return_val_if_fail(camera->num_buffers == 0, false);

        const enum v4l2_buf_type buftype = get_buf_type(camera);

        // Start by requesting buffers
        struct v4l2_requestbuffers req = {};
        req.count = MAX_VIDEO_BUFFERS;
        req.type = buftype;
        req.memory = V4L2_MEMORY_MMAP;

        if (xioctl(camera->video_fd, VIDIOC_REQBUFS, &req) == -1) {
                errno_printerr("VIDIOC_REQBUFS");
                return false;
        }

        if (req.count < 2) {
                g_printerr(
                        "Insufficient buffer memory. Only %d buffers available.\n",
                        req.count);
                goto error;
        }

        for (uint32_t i = 0; i < req.count; ++i) {
                // Query each buffer and mmap it
                struct v4l2_buffer buf = {
                        .type = buftype,
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
                        camera->buffers[i].data = mmap(NULL,
                                                       planes[0].length,
                                                       PROT_READ,
                                                       MAP_SHARED,
                                                       camera->video_fd,
                                                       planes[0].m.mem_offset);
                } else {
                        camera->buffers[i].length = buf.length;
                        camera->buffers[i].data = mmap(NULL,
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

                struct v4l2_exportbuffer expbuf = {
                        .type = buftype,
                        .index = i,
                };
                if (xioctl(camera->video_fd, VIDIOC_EXPBUF, &expbuf) == -1) {
                        errno_printerr("VIDIOC_EXPBUF");
                        break;
                }

                camera->buffers[i].fd = expbuf.fd;

                ++camera->num_buffers;
        }

        if (camera->num_buffers != req.count) {
                g_printerr("Unable to map all buffers\n");
                goto error;
        }

        for (uint32_t i = 0; i < camera->num_buffers; ++i) {
                struct v4l2_buffer buf = {
                        .type = buftype,
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
        enum v4l2_buf_type type = buftype;
        if (xioctl(camera->video_fd, VIDIOC_STREAMON, &type) == -1) {
                errno_printerr("VIDIOC_STREAMON");
                goto error;
        }

        return true;

error:
        // Unmap any mapped buffers
        assert(camera->num_buffers <= MAX_VIDEO_BUFFERS);
        for (uint32_t i = 0; i < camera->num_buffers; ++i) {
                if (munmap(camera->buffers[i].data, camera->buffers[i].length) ==
                    -1) {
                        errno_printerr("munmap");
                }

                if (close(camera->buffers[i].fd) == -1) {
                        errno_printerr("close");
                }
        }

        // Reset allocated buffers
        {
                struct v4l2_requestbuffers req = {};
                req.count = 0;
                req.type = buftype;
                req.memory = V4L2_MEMORY_MMAP;

                if (xioctl(camera->video_fd, VIDIOC_REQBUFS, &req) == -1) {
                        errno_printerr("VIDIOC_REQBUFS");
                }
        }

        return false;
}

bool
mp_camera_stop_capture(MPCamera *camera)
{
        g_return_val_if_fail(camera->num_buffers > 0, false);

        const enum v4l2_buf_type buftype = get_buf_type(camera);

        enum v4l2_buf_type type = buftype;
        if (xioctl(camera->video_fd, VIDIOC_STREAMOFF, &type) == -1) {
                errno_printerr("VIDIOC_STREAMOFF");
        }

        assert(camera->num_buffers <= MAX_VIDEO_BUFFERS);
        for (int i = 0; i < camera->num_buffers; ++i) {
                if (munmap(camera->buffers[i].data, camera->buffers[i].length) ==
                    -1) {
                        errno_printerr("munmap");
                }

                if (close(camera->buffers[i].fd) == -1) {
                        errno_printerr("close");
                }
        }

        camera->num_buffers = 0;

        struct v4l2_requestbuffers req = {};
        req.count = 0;
        req.type = buftype;
        req.memory = V4L2_MEMORY_MMAP;
        if (xioctl(camera->video_fd, VIDIOC_REQBUFS, &req) == -1) {
                errno_printerr("VIDIOC_REQBUFS");
        }

        return true;
}

bool
mp_camera_is_capturing(MPCamera *camera)
{
        return camera->num_buffers > 0;
}

bool
mp_camera_capture_buffer(MPCamera *camera, MPBuffer *buffer)
{
        const enum v4l2_buf_type buftype = get_buf_type(camera);

        struct v4l2_buffer buf = {};
        buf.type = buftype;
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
                        exit(1);
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

        assert(bytesused == (mp_pixel_format_width_to_bytes(pixel_format, width) +
                             mp_pixel_format_width_to_padding(pixel_format, width)) *
                                    height);
        assert(bytesused == camera->buffers[buf.index].length);

        buffer->index = buf.index;
        buffer->data = camera->buffers[buf.index].data;
        buffer->fd = camera->buffers[buf.index].fd;

        return true;
}

bool
mp_camera_release_buffer(MPCamera *camera, uint32_t buffer_index)
{
        const enum v4l2_buf_type buftype = get_buf_type(camera);

        struct v4l2_buffer buf = {};
        buf.type = buftype;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = buffer_index;

        struct v4l2_plane planes[1];
        if (camera->use_mplane) {
                buf.m.planes = planes;
                buf.length = 1;
        }

        if (xioctl(camera->video_fd, VIDIOC_QBUF, &buf) == -1) {
                errno_printerr("VIDIOC_QBUF");
                return false;
        }
        return true;
}

static MPModeList *
get_subdev_modes(MPCamera *camera, bool (*check)(MPCamera *, MPMode *))
{
        MPModeList *item = NULL;

        for (uint32_t fmt_index = 0;; ++fmt_index) {
                struct v4l2_subdev_mbus_code_enum fmt = {};
                fmt.index = fmt_index;
                fmt.pad = 0;
                fmt.which = V4L2_SUBDEV_FORMAT_TRY;
                if (xioctl(camera->subdev_fd, VIDIOC_SUBDEV_ENUM_MBUS_CODE, &fmt) ==
                    -1) {
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
                        if (xioctl(camera->subdev_fd,
                                   VIDIOC_SUBDEV_ENUM_FRAME_SIZE,
                                   &frame) == -1) {
                                if (errno != EINVAL) {
                                        errno_printerr(
                                                "VIDIOC_SUBDEV_ENUM_FRAME_SIZE");
                                }
                                break;
                        }

                        // TODO: Handle other types
                        if (frame.min_width != frame.max_width ||
                            frame.min_height != frame.max_height) {
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
                                if (xioctl(camera->subdev_fd,
                                           VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL,
                                           &interval) == -1) {
                                        if (errno != EINVAL) {
                                                errno_printerr(
                                                        "VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL");
                                        }
                                        break;
                                }

                                MPMode mode = {
                                        .pixel_format = format,
                                        .frame_interval = interval.interval,
                                        .width = frame.max_width,
                                        .height = frame.max_height,
                                };

                                if (!check(camera, &mode)) {
                                        continue;
                                }

                                MPModeList *new_item = malloc(sizeof(MPModeList));
                                new_item->mode = mode;
                                new_item->next = item;
                                item = new_item;
                        }
                }
        }

        return item;
}

static MPModeList *
get_video_modes(MPCamera *camera, bool (*check)(MPCamera *, MPMode *))
{
        const enum v4l2_buf_type buftype = get_buf_type(camera);

        MPModeList *item = NULL;

        for (uint32_t fmt_index = 0;; ++fmt_index) {
                struct v4l2_fmtdesc fmt = {};
                fmt.index = fmt_index;
                fmt.type = buftype;
                if (xioctl(camera->video_fd, VIDIOC_ENUM_FMT, &fmt) == -1) {
                        if (errno != EINVAL) {
                                errno_printerr("VIDIOC_ENUM_FMT");
                        }
                        break;
                }

                // Skip unsupported formats
                uint32_t format =
                        mp_pixel_format_from_v4l_pixel_format(fmt.pixelformat);
                if (format == MP_PIXEL_FMT_UNSUPPORTED) {
                        continue;
                }

                for (uint32_t frame_index = 0;; ++frame_index) {
                        struct v4l2_frmsizeenum frame = {};
                        frame.index = frame_index;
                        frame.pixel_format = fmt.pixelformat;
                        if (xioctl(camera->video_fd,
                                   VIDIOC_ENUM_FRAMESIZES,
                                   &frame) == -1) {
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
                                if (xioctl(camera->video_fd,
                                           VIDIOC_ENUM_FRAMEINTERVALS,
                                           &interval) == -1) {
                                        if (errno != EINVAL) {
                                                errno_printerr(
                                                        "VIDIOC_ENUM_FRAMESIZES");
                                        }
                                        break;
                                }

                                // TODO: Handle other types
                                if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE) {
                                        break;
                                }

                                MPMode mode = {
                                        .pixel_format = format,
                                        .frame_interval = interval.discrete,
                                        .width = frame.discrete.width,
                                        .height = frame.discrete.height,
                                };

                                if (!check(camera, &mode)) {
                                        continue;
                                }

                                MPModeList *new_item = malloc(sizeof(MPModeList));
                                new_item->mode = mode;
                                new_item->next = item;
                                item = new_item;
                        }
                }
        }

        return item;
}

static bool
all_modes(MPCamera *camera, MPMode *mode)
{
        return true;
}

static bool
available_modes(MPCamera *camera, MPMode *mode)
{
        MPMode attempt = *mode;
        return mp_camera_try_mode(camera, &attempt) &&
               mp_mode_is_equivalent(mode, &attempt);
}

MPModeList *
mp_camera_list_supported_modes(MPCamera *camera)
{
        if (mp_camera_is_subdev(camera)) {
                return get_subdev_modes(camera, all_modes);
        } else {
                return get_video_modes(camera, all_modes);
        }
}

MPModeList *
mp_camera_list_available_modes(MPCamera *camera)
{
        if (mp_camera_is_subdev(camera)) {
                return get_subdev_modes(camera, available_modes);
        } else {
                return get_video_modes(camera, available_modes);
        }
}

MPMode *
mp_camera_mode_list_get(MPModeList *list)
{
        g_return_val_if_fail(list, NULL);
        return &list->mode;
}

MPModeList *
mp_camera_mode_list_next(MPModeList *list)
{
        g_return_val_if_fail(list, NULL);
        return list->next;
}

void
mp_camera_mode_list_free(MPModeList *list)
{
        while (list) {
                MPModeList *tmp = list;
                list = tmp->next;
                free(tmp);
        }
}

struct int_str_pair {
        uint32_t value;
        const char *str;
};

struct int_str_pair control_id_names[] = {
        { V4L2_CID_BRIGHTNESS, "BRIGHTNESS" },
        { V4L2_CID_CONTRAST, "CONTRAST" },
        { V4L2_CID_SATURATION, "SATURATION" },
        { V4L2_CID_HUE, "HUE" },
        { V4L2_CID_AUDIO_VOLUME, "AUDIO_VOLUME" },
        { V4L2_CID_AUDIO_BALANCE, "AUDIO_BALANCE" },
        { V4L2_CID_AUDIO_BASS, "AUDIO_BASS" },
        { V4L2_CID_AUDIO_TREBLE, "AUDIO_TREBLE" },
        { V4L2_CID_AUDIO_MUTE, "AUDIO_MUTE" },
        { V4L2_CID_AUDIO_LOUDNESS, "AUDIO_LOUDNESS" },
        { V4L2_CID_BLACK_LEVEL, "BLACK_LEVEL" },
        { V4L2_CID_AUTO_WHITE_BALANCE, "AUTO_WHITE_BALANCE" },
        { V4L2_CID_DO_WHITE_BALANCE, "DO_WHITE_BALANCE" },
        { V4L2_CID_RED_BALANCE, "RED_BALANCE" },
        { V4L2_CID_BLUE_BALANCE, "BLUE_BALANCE" },
        { V4L2_CID_GAMMA, "GAMMA" },
        { V4L2_CID_WHITENESS, "WHITENESS" },
        { V4L2_CID_EXPOSURE, "EXPOSURE" },
        { V4L2_CID_AUTOGAIN, "AUTOGAIN" },
        { V4L2_CID_GAIN, "GAIN" },
        { V4L2_CID_HFLIP, "HFLIP" },
        { V4L2_CID_VFLIP, "VFLIP" },
        { V4L2_CID_POWER_LINE_FREQUENCY, "POWER_LINE_FREQUENCY" },
        { V4L2_CID_HUE_AUTO, "HUE_AUTO" },
        { V4L2_CID_WHITE_BALANCE_TEMPERATURE, "WHITE_BALANCE_TEMPERATURE" },
        { V4L2_CID_SHARPNESS, "SHARPNESS" },
        { V4L2_CID_BACKLIGHT_COMPENSATION, "BACKLIGHT_COMPENSATION" },
        { V4L2_CID_CHROMA_AGC, "CHROMA_AGC" },
        { V4L2_CID_COLOR_KILLER, "COLOR_KILLER" },
        { V4L2_CID_COLORFX, "COLORFX" },
        { V4L2_CID_AUTOBRIGHTNESS, "AUTOBRIGHTNESS" },
        { V4L2_CID_BAND_STOP_FILTER, "BAND_STOP_FILTER" },
        { V4L2_CID_ROTATE, "ROTATE" },
        { V4L2_CID_BG_COLOR, "BG_COLOR" },
        { V4L2_CID_CHROMA_GAIN, "CHROMA_GAIN" },
        { V4L2_CID_ILLUMINATORS_1, "ILLUMINATORS_1" },
        { V4L2_CID_ILLUMINATORS_2, "ILLUMINATORS_2" },
        { V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, "MIN_BUFFERS_FOR_CAPTURE" },
        { V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, "MIN_BUFFERS_FOR_OUTPUT" },
        { V4L2_CID_ALPHA_COMPONENT, "ALPHA_COMPONENT" },
        { V4L2_CID_COLORFX_CBCR, "COLORFX_CBCR" },
        { V4L2_CID_LASTP1, "LASTP1" },
        { V4L2_CID_USER_MEYE_BASE, "USER_MEYE_BASE" },
        { V4L2_CID_USER_BTTV_BASE, "USER_BTTV_BASE" },
        { V4L2_CID_USER_S2255_BASE, "USER_S2255_BASE" },
        { V4L2_CID_USER_SI476X_BASE, "USER_SI476X_BASE" },
        { V4L2_CID_USER_TI_VPE_BASE, "USER_TI_VPE_BASE" },
        { V4L2_CID_USER_SAA7134_BASE, "USER_SAA7134_BASE" },
        { V4L2_CID_USER_ADV7180_BASE, "USER_ADV7180_BASE" },
        { V4L2_CID_USER_TC358743_BASE, "USER_TC358743_BASE" },
        { V4L2_CID_USER_MAX217X_BASE, "USER_MAX217X_BASE" },
        { V4L2_CID_USER_IMX_BASE, "USER_IMX_BASE" },
        // { V4L2_CID_USER_ATMEL_ISC_BASE, "USER_ATMEL_ISC_BASE" },
        { V4L2_CID_CAMERA_CLASS_BASE, "CAMERA_CLASS_BASE" },
        { V4L2_CID_CAMERA_CLASS, "CAMERA_CLASS" },
        { V4L2_CID_EXPOSURE_AUTO, "EXPOSURE_AUTO" },
        { V4L2_CID_EXPOSURE_ABSOLUTE, "EXPOSURE_ABSOLUTE" },
        { V4L2_CID_EXPOSURE_AUTO_PRIORITY, "EXPOSURE_AUTO_PRIORITY" },
        { V4L2_CID_PAN_RELATIVE, "PAN_RELATIVE" },
        { V4L2_CID_TILT_RELATIVE, "TILT_RELATIVE" },
        { V4L2_CID_PAN_RESET, "PAN_RESET" },
        { V4L2_CID_TILT_RESET, "TILT_RESET" },
        { V4L2_CID_PAN_ABSOLUTE, "PAN_ABSOLUTE" },
        { V4L2_CID_TILT_ABSOLUTE, "TILT_ABSOLUTE" },
        { V4L2_CID_FOCUS_ABSOLUTE, "FOCUS_ABSOLUTE" },
        { V4L2_CID_FOCUS_RELATIVE, "FOCUS_RELATIVE" },
        { V4L2_CID_FOCUS_AUTO, "FOCUS_AUTO" },
        { V4L2_CID_ZOOM_ABSOLUTE, "ZOOM_ABSOLUTE" },
        { V4L2_CID_ZOOM_RELATIVE, "ZOOM_RELATIVE" },
        { V4L2_CID_ZOOM_CONTINUOUS, "ZOOM_CONTINUOUS" },
        { V4L2_CID_PRIVACY, "PRIVACY" },
        { V4L2_CID_IRIS_ABSOLUTE, "IRIS_ABSOLUTE" },
        { V4L2_CID_IRIS_RELATIVE, "IRIS_RELATIVE" },
        { V4L2_CID_AUTO_EXPOSURE_BIAS, "AUTO_EXPOSURE_BIAS" },
        { V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE, "AUTO_N_PRESET_WHITE_BALANCE" },
        { V4L2_CID_WIDE_DYNAMIC_RANGE, "WIDE_DYNAMIC_RANGE" },
        { V4L2_CID_IMAGE_STABILIZATION, "IMAGE_STABILIZATION" },
        { V4L2_CID_ISO_SENSITIVITY, "ISO_SENSITIVITY" },
        { V4L2_CID_ISO_SENSITIVITY_AUTO, "ISO_SENSITIVITY_AUTO" },
        { V4L2_CID_EXPOSURE_METERING, "EXPOSURE_METERING" },
        { V4L2_CID_SCENE_MODE, "SCENE_MODE" },
        { V4L2_CID_3A_LOCK, "3A_LOCK" },
        { V4L2_CID_AUTO_FOCUS_START, "AUTO_FOCUS_START" },
        { V4L2_CID_AUTO_FOCUS_STOP, "AUTO_FOCUS_STOP" },
        { V4L2_CID_AUTO_FOCUS_STATUS, "AUTO_FOCUS_STATUS" },
        { V4L2_CID_AUTO_FOCUS_RANGE, "AUTO_FOCUS_RANGE" },
        { V4L2_CID_PAN_SPEED, "PAN_SPEED" },
        { V4L2_CID_TILT_SPEED, "TILT_SPEED" },
        // { V4L2_CID_CAMERA_ORIENTATION, "CAMERA_ORIENTATION" },
        // { V4L2_CID_CAMERA_SENSOR_ROTATION, "CAMERA_SENSOR_ROTATION" },
        { V4L2_CID_FLASH_LED_MODE, "FLASH_LED_MODE" },
        { V4L2_CID_FLASH_STROBE_SOURCE, "FLASH_STROBE_SOURCE" },
        { V4L2_CID_FLASH_STROBE, "FLASH_STROBE" },
        { V4L2_CID_FLASH_STROBE_STOP, "FLASH_STROBE_STOP" },
        { V4L2_CID_FLASH_STROBE_STATUS, "FLASH_STROBE_STATUS" },
        { V4L2_CID_FLASH_TIMEOUT, "FLASH_TIMEOUT" },
        { V4L2_CID_FLASH_INTENSITY, "FLASH_INTENSITY" },
        { V4L2_CID_FLASH_TORCH_INTENSITY, "FLASH_TORCH_INTENSITY" },
        { V4L2_CID_FLASH_INDICATOR_INTENSITY, "FLASH_INDICATOR_INTENSITY" },
        { V4L2_CID_FLASH_FAULT, "FLASH_FAULT" },
        { V4L2_CID_FLASH_CHARGE, "FLASH_CHARGE" },
        { V4L2_CID_FLASH_READY, "FLASH_READY" },
};

const char *
mp_control_id_to_str(uint32_t id)
{
        size_t size = sizeof(control_id_names) / sizeof(*control_id_names);

        for (size_t i = 0; i < size; ++i) {
                if (control_id_names[i].value == id) {
                        return control_id_names[i].str;
                }
        }

        return "UNKNOWN";
}

struct int_str_pair control_type_names[] = {
        { V4L2_CTRL_TYPE_INTEGER, "INTEGER" },
        { V4L2_CTRL_TYPE_BOOLEAN, "BOOLEAN" },
        { V4L2_CTRL_TYPE_MENU, "MENU" },
        { V4L2_CTRL_TYPE_INTEGER_MENU, "INTEGER_MENU" },
        { V4L2_CTRL_TYPE_BITMASK, "BITMASK" },
        { V4L2_CTRL_TYPE_BUTTON, "BUTTON" },
        { V4L2_CTRL_TYPE_INTEGER64, "INTEGER64" },
        { V4L2_CTRL_TYPE_STRING, "STRING" },
        { V4L2_CTRL_TYPE_CTRL_CLASS, "CTRL_CLASS" },
        { V4L2_CTRL_TYPE_U8, "U8" },
        { V4L2_CTRL_TYPE_U16, "U16" },
        { V4L2_CTRL_TYPE_U32, "U32" },
        // { V4L2_CTRL_TYPE_MPEG2_SLICE_PARAMS, "MPEG2_SLICE_PARAMS" },
        // { V4L2_CTRL_TYPE_MPEG2_QUANTIZATION, "MPEG2_QUANTIZATION" },
        // { V4L2_CTRL_TYPE_AREA, "AREA" },
        // { V4L2_CTRL_TYPE_H264_SPS, "H264_SPS" },
        // { V4L2_CTRL_TYPE_H264_PPS, "H264_PPS" },
        // { V4L2_CTRL_TYPE_H264_SCALING_MATRIX, "H264_SCALING_MATRIX" },
        // { V4L2_CTRL_TYPE_H264_SLICE_PARAMS, "H264_SLICE_PARAMS" },
        // { V4L2_CTRL_TYPE_H264_DECODE_PARAMS, "H264_DECODE_PARAMS" },
        // { V4L2_CTRL_TYPE_HEVC_SPS, "HEVC_SPS" },
        // { V4L2_CTRL_TYPE_HEVC_PPS, "HEVC_PPS" },
        // { V4L2_CTRL_TYPE_HEVC_SLICE_PARAMS, "HEVC_SLICE_PARAMS" },
};

const char *
mp_control_type_to_str(uint32_t type)
{
        size_t size = sizeof(control_type_names) / sizeof(*control_type_names);

        for (size_t i = 0; i < size; ++i) {
                if (control_type_names[i].value == type) {
                        return control_type_names[i].str;
                }
        }

        return "UNKNOWN";
}

struct _MPControlList {
        MPControl control;
        MPControlList *next;
};

static int
control_fd(MPCamera *camera)
{
        if (camera->subdev_fd != -1) {
                return camera->subdev_fd;
        }
        return camera->video_fd;
}

MPControlList *
mp_camera_list_controls(MPCamera *camera)
{
        MPControlList *item = NULL;

        struct v4l2_query_ext_ctrl ctrl = {};
        ctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
        while (true) {
                if (xioctl(control_fd(camera), VIDIOC_QUERY_EXT_CTRL, &ctrl) == -1) {
                        if (errno != EINVAL) {
                                errno_printerr("VIDIOC_QUERY_EXT_CTRL");
                        }
                        break;
                }

                MPControl control = {
                        .id = ctrl.id,
                        .type = ctrl.type,
                        .name = {},
                        .min = ctrl.minimum,
                        .max = ctrl.maximum,
                        .step = ctrl.step,
                        .default_value = ctrl.default_value,
                        .flags = ctrl.flags,
                        .element_size = ctrl.elem_size,
                        .element_count = ctrl.elems,
                        .dimensions_count = ctrl.nr_of_dims,
                        .dimensions = {},
                };

                strcpy(control.name, ctrl.name);
                memcpy(control.dimensions,
                       ctrl.dims,
                       sizeof(uint32_t) * V4L2_CTRL_MAX_DIMS);

                MPControlList *new_item = malloc(sizeof(MPControlList));
                new_item->control = control;
                new_item->next = item;
                item = new_item;

                ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL | V4L2_CTRL_FLAG_NEXT_COMPOUND;
        }

        return item;
}

MPControl *
mp_control_list_get(MPControlList *list)
{
        g_return_val_if_fail(list, NULL);
        return &list->control;
}

MPControlList *
mp_control_list_next(MPControlList *list)
{
        g_return_val_if_fail(list, NULL);
        return list->next;
}

void
mp_control_list_free(MPControlList *list)
{
        while (list) {
                MPControlList *tmp = list;
                list = tmp->next;
                free(tmp);
        }
}

bool
mp_camera_query_control(MPCamera *camera, uint32_t id, MPControl *control)
{
        struct v4l2_query_ext_ctrl ctrl = {};
        ctrl.id = id;
        if (xioctl(control_fd(camera), VIDIOC_QUERY_EXT_CTRL, &ctrl) == -1) {
                if (errno != EINVAL) {
                        errno_printerr("VIDIOC_QUERY_EXT_CTRL");
                }
                return false;
        }

        if (control) {
                control->id = ctrl.id;
                control->type = ctrl.type;
                strcpy(control->name, ctrl.name);
                control->min = ctrl.minimum;
                control->max = ctrl.maximum;
                control->step = ctrl.step;
                control->default_value = ctrl.default_value;
                control->flags = ctrl.flags;
                control->element_size = ctrl.elem_size;
                control->element_count = ctrl.elems;
                control->dimensions_count = ctrl.nr_of_dims;
                memcpy(control->dimensions,
                       ctrl.dims,
                       sizeof(uint32_t) * V4L2_CTRL_MAX_DIMS);
        }
        return true;
}

static bool
control_impl_int32(MPCamera *camera, uint32_t id, int request, int32_t *value)
{
        struct v4l2_ext_control ctrl = {};
        ctrl.id = id;
        ctrl.value = *value;

        struct v4l2_ext_controls ctrls = {
                .ctrl_class = 0,
                .which = V4L2_CTRL_WHICH_CUR_VAL,
                .count = 1,
                .controls = &ctrl,
        };
        if (xioctl(control_fd(camera), request, &ctrls) == -1) {
                return false;
        }

        *value = ctrl.value;
        return true;
}

pid_t
mp_camera_control_set_int32_bg(MPCamera *camera, uint32_t id, int32_t v)
{
        struct v4l2_ext_control ctrl = {};
        ctrl.id = id;
        ctrl.value = v;

        struct v4l2_ext_controls ctrls = {
                .ctrl_class = 0,
                .which = V4L2_CTRL_WHICH_CUR_VAL,
                .count = 1,
                .controls = &ctrl,
        };

        int fd = control_fd(camera);

        // fork only after all the memory has been read
        pid_t pid = fork();
        if (pid == -1) {
                return 0; // discard errors, nothing to do in parent process
        } else if (pid != 0) {
                // parent process adding pid to wait list (to clear zombie processes)
                mp_camera_add_bg_task(camera, pid);
                return pid;
        }

        // ignore errors
        xioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls);
        // exit without calling exit handlers
        _exit(0);
}

bool
mp_camera_control_try_int32(MPCamera *camera, uint32_t id, int32_t *v)
{
        return control_impl_int32(camera, id, VIDIOC_TRY_EXT_CTRLS, v);
}

bool
mp_camera_control_set_int32(MPCamera *camera, uint32_t id, int32_t v)
{
        return control_impl_int32(camera, id, VIDIOC_S_EXT_CTRLS, &v);
}

int32_t
mp_camera_control_get_int32(MPCamera *camera, uint32_t id)
{
        int32_t v = 0;
        control_impl_int32(camera, id, VIDIOC_G_EXT_CTRLS, &v);
        return v;
}

bool
mp_camera_control_try_boolean(MPCamera *camera, uint32_t id, bool *v)
{
        int32_t value = *v;
        bool s = control_impl_int32(camera, id, VIDIOC_TRY_EXT_CTRLS, &value);
        *v = value;
        return s;
}

bool
mp_camera_control_set_bool(MPCamera *camera, uint32_t id, bool v)
{
        int32_t value = v;
        return control_impl_int32(camera, id, VIDIOC_S_EXT_CTRLS, &value);
}

bool
mp_camera_control_get_bool(MPCamera *camera, uint32_t id)
{
        int32_t v = false;
        control_impl_int32(camera, id, VIDIOC_G_EXT_CTRLS, &v);
        return v;
}

pid_t
mp_camera_control_set_bool_bg(MPCamera *camera, uint32_t id, bool v)
{
        int32_t value = v;
        return mp_camera_control_set_int32_bg(camera, id, value);
}
