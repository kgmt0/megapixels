#include "device.h"
#include "mode.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

bool
mp_find_device_path(struct media_v2_intf_devnode devnode, char *path, int length)
{
        char uevent_path[256];
        snprintf(uevent_path,
                 256,
                 "/sys/dev/char/%d:%d/uevent",
                 devnode.major,
                 devnode.minor);

        FILE *f = fopen(uevent_path, "r");
        if (!f) {
                return false;
        }

        char line[512];
        while (fgets(line, 512, f)) {
                if (strncmp(line, "DEVNAME=", 8) == 0) {
                        // Drop newline
                        int length = strlen(line);
                        if (line[length - 1] == '\n')
                                line[length - 1] = '\0';

                        snprintf(path, length, "/dev/%s", line + 8);
                        return true;
                }
        }

        fclose(f);

        return false;
}

struct _MPDevice {
        int fd;

        struct media_device_info info;

        struct media_v2_entity *entities;
        size_t num_entities;
        struct media_v2_interface *interfaces;
        size_t num_interfaces;
        struct media_v2_pad *pads;
        size_t num_pads;
        struct media_v2_link *links;
        size_t num_links;
};

static void
errno_printerr(const char *s)
{
        g_printerr("MPDevice: %s error %d, %s\n", s, errno, strerror(errno));
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

MPDevice *
mp_device_find(const char *driver_name, const char *dev_name)
{
        MPDeviceList *list = mp_device_list_new();

        MPDevice *found_device =
                mp_device_list_find_remove(&list, driver_name, dev_name);

        mp_device_list_free(list);

        return found_device;
}

MPDevice *
mp_device_open(const char *path)
{
        int fd = open(path, O_RDWR);
        if (fd == -1) {
                errno_printerr("open");
                return NULL;
        }

        return mp_device_new(fd);
}

MPDevice *
mp_device_new(int fd)
{
        // Get the topology of the media device
        struct media_v2_topology topology = {};
        if (xioctl(fd, MEDIA_IOC_G_TOPOLOGY, &topology) == -1 ||
            topology.num_entities == 0) {
                close(fd);
                return NULL;
        }

        // Create the device
        MPDevice *device = calloc(1, sizeof(MPDevice));
        device->fd = fd;
        device->entities =
                calloc(topology.num_entities, sizeof(struct media_v2_entity));
        device->num_entities = topology.num_entities;
        device->interfaces =
                calloc(topology.num_interfaces, sizeof(struct media_v2_interface));
        device->num_interfaces = topology.num_interfaces;
        device->pads = calloc(topology.num_pads, sizeof(struct media_v2_pad));
        device->num_pads = topology.num_pads;
        device->links = calloc(topology.num_links, sizeof(struct media_v2_link));
        device->num_links = topology.num_links;

        // Get the actual devices and interfaces
        topology.ptr_entities = (uint64_t)device->entities;
        topology.ptr_interfaces = (uint64_t)device->interfaces;
        topology.ptr_pads = (uint64_t)device->pads;
        topology.ptr_links = (uint64_t)device->links;
        if (xioctl(fd, MEDIA_IOC_G_TOPOLOGY, &topology) == -1) {
                errno_printerr("MEDIA_IOC_G_TOPOLOGY");
                mp_device_close(device);
                return NULL;
        }

        // Get device info
        if (xioctl(fd, MEDIA_IOC_DEVICE_INFO, &device->info) == -1) {
                errno_printerr("MEDIA_IOC_DEVICE_INFO");
                mp_device_close(device);
                return NULL;
        }

        return device;
}

void
mp_device_close(MPDevice *device)
{
        close(device->fd);
        free(device->entities);
        free(device->interfaces);
        free(device->pads);
        free(device->links);
        free(device);
}

int
mp_device_get_fd(const MPDevice *device)
{
        return device->fd;
}

bool
mp_device_setup_entity_link(MPDevice *device,
                            uint32_t source_entity_id,
                            uint32_t sink_entity_id,
                            uint32_t source_index,
                            uint32_t sink_index,
                            bool enabled)
{
        struct media_link_desc link = {};
        link.flags = enabled ? MEDIA_LNK_FL_ENABLED : 0;
        link.source.entity = source_entity_id;
        link.source.index = source_index;
        link.sink.entity = sink_entity_id;
        link.sink.index = sink_index;
        if (xioctl(device->fd, MEDIA_IOC_SETUP_LINK, &link) == -1) {
                errno_printerr("MEDIA_IOC_SETUP_LINK");
                return false;
        }

        return true;
}

bool
mp_device_setup_link(MPDevice *device,
                     uint32_t source_pad_id,
                     uint32_t sink_pad_id,
                     bool enabled)
{
        const struct media_v2_pad *source_pad =
                mp_device_get_pad(device, source_pad_id);
        g_return_val_if_fail(source_pad, false);

        const struct media_v2_pad *sink_pad = mp_device_get_pad(device, sink_pad_id);
        g_return_val_if_fail(sink_pad, false);

        return mp_device_setup_entity_link(
                device, source_pad->entity_id, sink_pad->entity_id, source_pad->index, sink_pad->index, enabled);
}

bool
mp_entity_pad_set_format(MPDevice *device,
                         const struct media_v2_entity *entity,
                         uint32_t pad,
                         MPMode *mode)
{
        const struct media_v2_interface *interface =
                mp_device_find_entity_interface(device, entity->id);
        char path[260];
        if (!mp_find_device_path(interface->devnode, path, 260)) {
                g_printerr("Could not find path to %s\n", entity->name);
                return false;
        }

        int fd = open(path, O_WRONLY);
        if (fd == -1) {
                errno_printerr("open");
                return false;
        }

        struct v4l2_subdev_format fmt = {};
        fmt.pad = pad;
        fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        fmt.format.width = mode->width;
        fmt.format.height = mode->height;
        fmt.format.code = mp_pixel_format_to_v4l_bus_code(mode->pixel_format);
        fmt.format.field = V4L2_FIELD_ANY;
        if (xioctl(fd, VIDIOC_SUBDEV_S_FMT, &fmt) == -1) {
                errno_printerr("VIDIOC_SUBDEV_S_FMT");
                return false;
        }

        close(fd);

        return true;
}

const struct media_v2_entity *
mp_device_find_entity(const MPDevice *device, const char *driver_name)
{
        int length = strlen(driver_name);

        // Find the entity from the name
        for (uint32_t i = 0; i < device->num_entities; ++i) {
                if (strncmp(device->entities[i].name, driver_name, length) == 0) {
                        return &device->entities[i];
                }
        }
        return NULL;
}

const struct media_v2_entity *
mp_device_find_entity_type(const MPDevice *device, const uint32_t type)
{
        // Find the entity from the entity type
        for (uint32_t i = 0; i < device->num_entities; ++i) {
                if (device->entities[i].function == type) {
                        return &device->entities[i];
                }
        }
        return NULL;
}

const struct media_device_info *
mp_device_get_info(const MPDevice *device)
{
        return &device->info;
}

const struct media_v2_entity *
mp_device_get_entity(const MPDevice *device, uint32_t id)
{
        for (int i = 0; i < device->num_entities; ++i) {
                if (device->entities[i].id == id) {
                        return &device->entities[i];
                }
        }
        return NULL;
}

const struct media_v2_entity *
mp_device_get_entities(const MPDevice *device)
{
        return device->entities;
}

size_t
mp_device_get_num_entities(const MPDevice *device)
{
        return device->num_entities;
}

const struct media_v2_interface *
mp_device_find_entity_interface(const MPDevice *device, uint32_t entity_id)
{
        // Find the interface through the link
        const struct media_v2_link *link = mp_device_find_link_to(device, entity_id);
        if (!link) {
                return NULL;
        }
        return mp_device_get_interface(device, link->source_id);
}

const struct media_v2_interface *
mp_device_get_interface(const MPDevice *device, uint32_t id)
{
        for (int i = 0; i < device->num_interfaces; ++i) {
                if (device->interfaces[i].id == id) {
                        return &device->interfaces[i];
                }
        }
        return NULL;
}

const struct media_v2_interface *
mp_device_get_interfaces(const MPDevice *device)
{
        return device->interfaces;
}

size_t
mp_device_get_num_interfaces(const MPDevice *device)
{
        return device->num_interfaces;
}

const struct media_v2_pad *
mp_device_get_pad_from_entity(const MPDevice *device, uint32_t entity_id)
{
        for (int i = 0; i < device->num_pads; ++i) {
                if (device->pads[i].entity_id == entity_id) {
                        return &device->pads[i];
                }
        }
        return NULL;
}

const struct media_v2_pad *
mp_device_get_pad(const MPDevice *device, uint32_t id)
{
        for (int i = 0; i < device->num_pads; ++i) {
                if (device->pads[i].id == id) {
                        return &device->pads[i];
                }
        }
        return NULL;
}

const struct media_v2_pad *
mp_device_get_pads(const MPDevice *device)
{
        return device->pads;
}

size_t
mp_device_get_num_pads(const MPDevice *device)
{
        return device->num_pads;
}

const struct media_v2_link *
mp_device_find_entity_link(const MPDevice *device, uint32_t entity_id)
{
        const struct media_v2_pad *pad =
                mp_device_get_pad_from_entity(device, entity_id);
        const struct media_v2_link *link = mp_device_find_link_to(device, pad->id);
        if (link) {
                return link;
        }
        return mp_device_find_link_from(device, pad->id);
}

const struct media_v2_link *
mp_device_find_link_from(const MPDevice *device, uint32_t source)
{
        for (int i = 0; i < device->num_links; ++i) {
                if (device->links[i].source_id == source) {
                        return &device->links[i];
                }
        }
        return NULL;
}

const struct media_v2_link *
mp_device_find_link_to(const MPDevice *device, uint32_t sink)
{
        for (int i = 0; i < device->num_links; ++i) {
                if (device->links[i].sink_id == sink) {
                        return &device->links[i];
                }
        }
        return NULL;
}

const struct media_v2_link *
mp_device_find_link_between(const MPDevice *device, uint32_t source, uint32_t sink)
{
        for (int i = 0; i < device->num_links; ++i) {
                if (device->links[i].source_id == source &&
                    device->links[i].sink_id == sink) {
                        return &device->links[i];
                }
        }
        return NULL;
}

const struct media_v2_link *
mp_device_get_link(const MPDevice *device, uint32_t id)
{
        for (int i = 0; i < device->num_links; ++i) {
                if (device->links[i].id == id) {
                        return &device->links[i];
                }
        }
        return NULL;
}

const struct media_v2_link *
mp_device_get_links(const MPDevice *device)
{
        return device->links;
}

size_t
mp_device_get_num_links(const MPDevice *device)
{
        return device->num_links;
}

struct _MPDeviceList {
        MPDevice *device;
        MPDeviceList *next;
        char path[PATH_MAX];
};

MPDeviceList *
mp_device_list_new()
{
        MPDeviceList *current = NULL;

        // Enumerate media device files
        struct dirent *dir;
        DIR *d = opendir("/dev");
        while ((dir = readdir(d)) != NULL) {
                if (strncmp(dir->d_name, "media", 5) == 0) {
                        char path[PATH_MAX];
                        snprintf(path, PATH_MAX, "/dev/%s", dir->d_name);

                        MPDevice *device = mp_device_open(path);

                        if (device) {
                                MPDeviceList *next = malloc(sizeof(MPDeviceList));
                                next->device = device;
                                next->next = current;
                                memcpy(next->path, path, sizeof(path));
                                current = next;
                        }
                }
        }
        closedir(d);

        return current;
}

void
mp_device_list_free(MPDeviceList *device_list)
{
        while (device_list) {
                MPDeviceList *tmp = device_list;
                device_list = tmp->next;

                mp_device_close(tmp->device);
                free(tmp);
        }
}

MPDevice *
mp_device_list_find_remove(MPDeviceList **list,
                           const char *driver_name,
                           const char *dev_name)
{
        MPDevice *found_device = NULL;
        int length = strlen(driver_name);

        while (*list) {
                MPDevice *device = mp_device_list_get(*list);
                const struct media_device_info *info = mp_device_get_info(device);

                if (strncmp(info->driver, driver_name, length) == 0 &&
                    mp_device_find_entity(device, dev_name)) {
                        found_device = mp_device_list_remove(list);
                        break;
                }

                list = &(*list)->next;
        }

        return found_device;
}

MPDevice *
mp_device_list_remove(MPDeviceList **device_list)
{
        MPDevice *device = (*device_list)->device;

        if ((*device_list)->next) {
                MPDeviceList *tmp = (*device_list)->next;
                **device_list = *tmp;
                free(tmp);
        } else {
                free(*device_list);
                *device_list = NULL;
        }

        return device;
}

MPDevice *
mp_device_list_get(const MPDeviceList *device_list)
{
        return device_list->device;
}

const char *
mp_device_list_get_path(const MPDeviceList *device_list)
{
        return device_list->path;
}

MPDeviceList *
mp_device_list_next(const MPDeviceList *device_list)
{
        return device_list->next;
}
