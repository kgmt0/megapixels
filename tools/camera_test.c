#include "camera.h"
#include "device.h"
#include "mode.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

double
get_time()
{
        struct timeval t;
        struct timezone tzp;
        gettimeofday(&t, &tzp);
        return t.tv_sec + t.tv_usec * 1e-6;
}

int
main(int argc, char *argv[])
{
        if (argc != 3) {
                printf("Usage: %s <media_device_name> <sub_device_name>\n", argv[0]);
                return 1;
        }

        char *video_name = argv[1];
        char *subdev_name = argv[2];

        double find_start = get_time();

        // First find the device
        MPDevice *device = mp_device_find(video_name, subdev_name);
        if (!device) {
                printf("Device not found\n");
                return 1;
        }

        double find_end = get_time();

        printf("Finding the device took %fms\n", (find_end - find_start) * 1000);

        int video_fd;
        uint32_t video_entity_id;
        {
                const struct media_v2_entity *entity =
                        mp_device_find_entity(device, video_name);
                if (!entity) {
                        printf("Unable to find video device interface\n");
                        return 1;
                }

                video_entity_id = entity->id;

                const struct media_v2_interface *iface =
                        mp_device_find_entity_interface(device, video_entity_id);

                char buf[256];
                if (!mp_find_device_path(iface->devnode, buf, 256)) {
                        printf("Unable to find video device path\n");
                        return 1;
                }

                video_fd = open(buf, O_RDWR);
                if (video_fd == -1) {
                        printf("Unable to open video device\n");
                        return 1;
                }
        }

        int subdev_fd = -1;
        const struct media_v2_entity *entity =
                mp_device_find_entity(device, subdev_name);
        if (!entity) {
                printf("Unable to find sub-device\n");
                return 1;
        }

        const struct media_v2_pad *source_pad =
                mp_device_get_pad_from_entity(device, entity->id);
        const struct media_v2_pad *sink_pad =
                mp_device_get_pad_from_entity(device, video_entity_id);

        // Disable other links
        const struct media_v2_entity *entities = mp_device_get_entities(device);
        for (int i = 0; i < mp_device_get_num_entities(device); ++i) {
                if (entities[i].id != video_entity_id &&
                    entities[i].id != entity->id) {
                        const struct media_v2_pad *pad =
                                mp_device_get_pad_from_entity(device,
                                                              entities[i].id);
                        mp_device_setup_link(device, pad->id, sink_pad->id, false);
                }
        }

        // Then enable ours
        mp_device_setup_link(device, source_pad->id, sink_pad->id, true);

        const struct media_v2_interface *iface =
                mp_device_find_entity_interface(device, entity->id);

        char buf[256];
        if (!mp_find_device_path(iface->devnode, buf, 256)) {
                printf("Unable to find sub-device path\n");
                return 1;
        }

        subdev_fd = open(buf, O_RDWR);
        if (subdev_fd == -1) {
                printf("Unable to open sub-device\n");
                return 1;
        }

        double open_end = get_time();

        printf("Opening the device took %fms\n", (open_end - find_end) * 1000);

        MPCamera *camera = mp_camera_new(video_fd, subdev_fd);

        MPControlList *controls = mp_camera_list_controls(camera);

        double control_list_end = get_time();

        printf("Available controls: (took %fms)\n",
               (control_list_end - open_end) * 1000);
        for (MPControlList *list = controls; list;
             list = mp_control_list_next(list)) {
                MPControl *c = mp_control_list_get(list);

                printf("  %32s id:%s type:%s default:%d\n",
                       c->name,
                       mp_control_id_to_str(c->id),
                       mp_control_type_to_str(c->type),
                       c->default_value);
        }

        double mode_list_begin = get_time();

        MPModeList *modes = mp_camera_list_available_modes(camera);

        double mode_list_end = get_time();

        printf("Available modes: (took %fms)\n",
               (mode_list_end - mode_list_begin) * 1000);
        for (MPModeList *list = modes; list; list = mp_camera_mode_list_next(list)) {
                MPMode *m = mp_camera_mode_list_get(list);
                printf("  %dx%d interval:%d/%d fmt:%s\n",
                       m->width,
                       m->height,
                       m->frame_interval.numerator,
                       m->frame_interval.denominator,
                       mp_pixel_format_to_str(m->pixel_format));

                // Skip really slow framerates
                if (m->frame_interval.denominator < 15) {
                        printf("    Skipping…\n");
                        continue;
                }

                double start_capture = get_time();

                mp_camera_set_mode(camera, m);
                mp_camera_start_capture(camera);

                double last = get_time();
                printf("    Testing 10 captures, starting took %fms\n",
                       (last - start_capture) * 1000);

                for (int i = 0; i < 10; ++i) {
                        MPBuffer buffer;
                        if (!mp_camera_capture_buffer(camera, &buffer)) {
                                printf("      Failed to capture buffer\n");
                        }

                        size_t num_bytes = mp_pixel_format_width_to_bytes(
                                                   m->pixel_format, m->width) *
                                           m->height;
                        uint8_t *data = malloc(num_bytes);
                        memcpy(data, buffer.data, num_bytes);

                        printf("      first byte: %d.", data[0]);

                        free(data);

                        mp_camera_release_buffer(camera, buffer.index);

                        double now = get_time();
                        printf(" capture took %fms\n", (now - last) * 1000);
                        last = now;
                }

                mp_camera_stop_capture(camera);
        }

        double cleanup_start = get_time();

        mp_camera_free(camera);

        close(video_fd);
        if (subdev_fd != -1)
                close(subdev_fd);

        mp_device_close(device);

        double cleanup_end = get_time();

        printf("Cleanup took %fms\n", (cleanup_end - cleanup_start) * 1000);
}
