#pragma once

#include "mode.h"

#include <linux/media.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool
mp_find_device_path(struct media_v2_intf_devnode devnode, char *path, int length);

typedef struct _MPDevice MPDevice;

MPDevice *mp_device_find(const char *driver_name, const char *dev_name);
MPDevice *mp_device_open(const char *path);
MPDevice *mp_device_new(int fd);
void mp_device_close(MPDevice *device);

int mp_device_get_fd(const MPDevice *device);

bool mp_device_setup_entity_link(MPDevice *device,
                                 uint32_t source_entity_id,
                                 uint32_t sink_entity_id,
                                 uint32_t source_index,
                                 uint32_t sink_index,
                                 bool enabled);

bool mp_device_setup_link(MPDevice *device,
                          uint32_t source_pad_id,
                          uint32_t sink_pad_id,
                          bool enabled);

bool mp_entity_pad_set_format(MPDevice *device,
                              const struct media_v2_entity *entity,
                              uint32_t pad,
                              MPMode *mode);

const struct media_device_info *mp_device_get_info(const MPDevice *device);
const struct media_v2_entity *mp_device_find_entity(const MPDevice *device,
                                                    const char *driver_name);
const struct media_v2_entity *mp_device_find_entity_type(const MPDevice *device,
                                                         const uint32_t type);
const struct media_v2_entity *mp_device_get_entity(const MPDevice *device,
                                                   uint32_t id);
const struct media_v2_entity *mp_device_get_entities(const MPDevice *device);
size_t mp_device_get_num_entities(const MPDevice *device);
const struct media_v2_interface *
mp_device_find_entity_interface(const MPDevice *device, uint32_t entity_id);
const struct media_v2_interface *mp_device_get_interface(const MPDevice *device,
                                                         uint32_t id);
const struct media_v2_interface *mp_device_get_interfaces(const MPDevice *device);
size_t mp_device_get_num_interfaces(const MPDevice *device);
const struct media_v2_pad *mp_device_get_pad_from_entity(const MPDevice *device,
                                                         uint32_t entity_id);
const struct media_v2_pad *mp_device_get_pad(const MPDevice *device, uint32_t id);
const struct media_v2_pad *mp_device_get_pads(const MPDevice *device);
size_t mp_device_get_num_pads(const MPDevice *device);
const struct media_v2_link *mp_device_find_entity_link(const MPDevice *device,
                                                       uint32_t entity_id);
const struct media_v2_link *mp_device_find_link_from(const MPDevice *device,
                                                     uint32_t source);
const struct media_v2_link *mp_device_find_link_to(const MPDevice *device,
                                                   uint32_t sink);
const struct media_v2_link *
mp_device_find_link_between(const MPDevice *device, uint32_t source, uint32_t sink);
const struct media_v2_link *mp_device_get_link(const MPDevice *device, uint32_t id);
const struct media_v2_link *mp_device_get_links(const MPDevice *device);
size_t mp_device_get_num_links(const MPDevice *device);

typedef struct _MPDeviceList MPDeviceList;

MPDeviceList *mp_device_list_new();
void mp_device_list_free(MPDeviceList *device_list);

MPDevice *mp_device_list_find_remove(MPDeviceList **device_list,
                                     const char *driver_name,
                                     const char *dev_name);
MPDevice *mp_device_list_remove(MPDeviceList **device_list);

MPDevice *mp_device_list_get(const MPDeviceList *device_list);
const char *mp_device_list_get_path(const MPDeviceList *device_list);
MPDeviceList *mp_device_list_next(const MPDeviceList *device_list);
