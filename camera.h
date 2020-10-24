#pragma once

#include <linux/v4l2-subdev.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MP_PIXEL_FMT_UNSUPPORTED,
    MP_PIXEL_FMT_BGGR8,
    MP_PIXEL_FMT_GBRG8,
    MP_PIXEL_FMT_GRBG8,
    MP_PIXEL_FMT_RGGB8,

    MP_PIXEL_FMT_MAX,
} MPPixelFormat;

MPPixelFormat mp_pixel_format_from_str(const char *str);
const char *mp_pixel_format_to_str(MPPixelFormat pixel_format);

MPPixelFormat mp_pixel_format_from_v4l_pixel_format(uint32_t v4l_pixel_format);
MPPixelFormat mp_pixel_format_from_v4l_bus_code(uint32_t v4l_bus_code);
uint32_t mp_pixel_format_to_v4l_pixel_format(MPPixelFormat pixel_format);
uint32_t mp_pixel_format_to_v4l_bus_code(MPPixelFormat pixel_format);

uint32_t mp_pixel_format_bytes_per_pixel(MPPixelFormat pixel_format);

typedef struct {
    MPPixelFormat pixel_format;

    struct v4l2_fract frame_interval;
    uint32_t width;
    uint32_t height;
} MPCameraMode;

bool mp_camera_mode_is_equivalent(const MPCameraMode *m1, const MPCameraMode *m2);

typedef struct {
    uint32_t pixel_format;
    uint32_t width;
    uint32_t height;
    uint8_t *data;
} MPImage;

typedef struct _MPCamera MPCamera;

MPCamera *mp_camera_new(int video_fd, int subdev_fd);
void mp_camera_free(MPCamera *camera);

bool mp_camera_is_subdev(MPCamera *camera);
int mp_camera_get_video_fd(MPCamera *camera);
int mp_camera_get_subdev_fd(MPCamera *camera);

const MPCameraMode *mp_camera_get_mode(const MPCamera *camera);
bool mp_camera_try_mode(MPCamera *camera, MPCameraMode *mode);

bool mp_camera_set_mode(MPCamera *camera, MPCameraMode *mode);
bool mp_camera_start_capture(MPCamera *camera);
bool mp_camera_stop_capture(MPCamera *camera);
bool mp_camera_is_capturing(MPCamera *camera);
bool mp_camera_capture_image(MPCamera *camera, void (*callback)(MPImage, void *), void *user_data);

typedef struct _MPCameraModeList MPCameraModeList;

MPCameraModeList *mp_camera_list_supported_modes(MPCamera *camera);
MPCameraModeList *mp_camera_list_available_modes(MPCamera *camera);
MPCameraMode *mp_camera_mode_list_get(MPCameraModeList *list);
MPCameraModeList *mp_camera_mode_list_next(MPCameraModeList *list);
void mp_camera_mode_list_free(MPCameraModeList *list);
