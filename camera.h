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
	MP_PIXEL_FMT_BGGR10P,
	MP_PIXEL_FMT_GBRG10P,
	MP_PIXEL_FMT_GRBG10P,
	MP_PIXEL_FMT_RGGB10P,
	MP_PIXEL_FMT_UYVY,
	MP_PIXEL_FMT_YUYV,

	MP_PIXEL_FMT_MAX,
} MPPixelFormat;

const char *mp_pixel_format_to_str(MPPixelFormat pixel_format);
MPPixelFormat mp_pixel_format_from_str(const char *str);

MPPixelFormat mp_pixel_format_from_v4l_pixel_format(uint32_t v4l_pixel_format);
MPPixelFormat mp_pixel_format_from_v4l_bus_code(uint32_t v4l_bus_code);
uint32_t mp_pixel_format_to_v4l_pixel_format(MPPixelFormat pixel_format);
uint32_t mp_pixel_format_to_v4l_bus_code(MPPixelFormat pixel_format);

uint32_t mp_pixel_format_bits_per_pixel(MPPixelFormat pixel_format);
uint32_t mp_pixel_format_width_to_bytes(MPPixelFormat pixel_format, uint32_t width);
uint32_t mp_pixel_format_width_to_colors(MPPixelFormat pixel_format, uint32_t width);
uint32_t mp_pixel_format_height_to_colors(MPPixelFormat pixel_format,
					  uint32_t height);

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
bool mp_camera_capture_image(MPCamera *camera, void (*callback)(MPImage, void *),
			     void *user_data);

typedef struct _MPCameraModeList MPCameraModeList;

MPCameraModeList *mp_camera_list_supported_modes(MPCamera *camera);
MPCameraModeList *mp_camera_list_available_modes(MPCamera *camera);
MPCameraMode *mp_camera_mode_list_get(MPCameraModeList *list);
MPCameraModeList *mp_camera_mode_list_next(MPCameraModeList *list);
void mp_camera_mode_list_free(MPCameraModeList *list);

typedef struct {
	uint32_t id;
	uint32_t type;
	char name[32];

	int32_t min;
	int32_t max;
	int32_t step;
	int32_t default_value;

	uint32_t flags;

	uint32_t element_size;
	uint32_t element_count;
	uint32_t dimensions_count;
	uint32_t dimensions[V4L2_CTRL_MAX_DIMS];
} MPControl;

const char *mp_control_id_to_str(uint32_t id);
const char *mp_control_type_to_str(uint32_t type);

typedef struct _MPControlList MPControlList;

MPControlList *mp_camera_list_controls(MPCamera *camera);
MPControl *mp_control_list_get(MPControlList *list);
MPControlList *mp_control_list_next(MPControlList *list);
void mp_control_list_free(MPControlList *list);

bool mp_camera_query_control(MPCamera *camera, uint32_t id, MPControl *control);

bool mp_camera_control_try_int32(MPCamera *camera, uint32_t id, int32_t *v);
bool mp_camera_control_set_int32(MPCamera *camera, uint32_t id, int32_t v);
int32_t mp_camera_control_get_int32(MPCamera *camera, uint32_t id);

bool mp_camera_control_try_bool(MPCamera *camera, uint32_t id, bool *v);
bool mp_camera_control_set_bool(MPCamera *camera, uint32_t id, bool v);
bool mp_camera_control_get_bool(MPCamera *camera, uint32_t id);
