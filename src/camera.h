#pragma once

#include "mode.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/wait.h>

typedef struct {
        uint32_t index;

        uint8_t *data;
        int fd;
} MPBuffer;

typedef struct _MPCamera MPCamera;

MPCamera *mp_camera_new(int video_fd, int subdev_fd, int bridge_fd);
void mp_camera_free(MPCamera *camera);

void mp_camera_add_bg_task(MPCamera *camera, pid_t pid);
void mp_camera_wait_bg_tasks(MPCamera *camera);
bool mp_camera_check_task_complete(MPCamera *camera, pid_t pid);

bool mp_camera_is_subdev(MPCamera *camera);
int mp_camera_get_video_fd(MPCamera *camera);
int mp_camera_get_subdev_fd(MPCamera *camera);

const MPMode *mp_camera_get_mode(const MPCamera *camera);
bool mp_camera_try_mode(MPCamera *camera, MPMode *mode);

bool mp_camera_set_mode(MPCamera *camera, MPMode *mode);
bool mp_camera_start_capture(MPCamera *camera);
bool mp_camera_stop_capture(MPCamera *camera);
bool mp_camera_is_capturing(MPCamera *camera);
bool mp_camera_capture_buffer(MPCamera *camera, MPBuffer *buffer);
bool mp_camera_release_buffer(MPCamera *camera, uint32_t buffer_index);

MPModeList *mp_camera_list_supported_modes(MPCamera *camera);
MPModeList *mp_camera_list_available_modes(MPCamera *camera);
MPMode *mp_camera_mode_list_get(MPModeList *list);
MPModeList *mp_camera_mode_list_next(MPModeList *list);
void mp_camera_mode_list_free(MPModeList *list);

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
// set the value in the background, discards result
pid_t mp_camera_control_set_int32_bg(MPCamera *camera, uint32_t id, int32_t v);

bool mp_camera_control_try_bool(MPCamera *camera, uint32_t id, bool *v);
bool mp_camera_control_set_bool(MPCamera *camera, uint32_t id, bool v);
bool mp_camera_control_get_bool(MPCamera *camera, uint32_t id);
// set the value in the background, discards result
pid_t mp_camera_control_set_bool_bg(MPCamera *camera, uint32_t id, bool v);
