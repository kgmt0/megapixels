#include "device.h"
#include <linux/media.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
	MPDeviceList *list = mp_device_list_new();

	while (list) {
		MPDevice *device = mp_device_list_get(list);

		const struct media_device_info *info = mp_device_get_info(device);
		printf("%s (%s) %s\n", info->model, info->driver, info->serial);
		printf("  Bus Info: %s\n", info->bus_info);
		printf("  Media Version: %d\n", info->media_version);
		printf("  HW Revision: %d\n", info->hw_revision);
		printf("  Driver Version: %d\n", info->driver_version);

		const struct media_v2_entity *entities =
			mp_device_get_entities(device);
		size_t num = mp_device_get_num_entities(device);
		printf("  Entities (%ld):\n", num);
		for (int i = 0; i < num; ++i) {
			printf("    %d %s (%d)\n", entities[i].id, entities[i].name,
			       entities[i].function);
		}

		const struct media_v2_interface *interfaces =
			mp_device_get_interfaces(device);
		num = mp_device_get_num_interfaces(device);
		printf("  Interfaces (%ld):\n", num);
		for (int i = 0; i < num; ++i) {
			printf("    %d (%d - %d) devnode %d:%d\n", interfaces[i].id,
			       interfaces[i].intf_type, interfaces[i].flags,
			       interfaces[i].devnode.major,
			       interfaces[i].devnode.minor);
		}

		const struct media_v2_pad *pads = mp_device_get_pads(device);
		num = mp_device_get_num_pads(device);
		printf("  Pads (%ld):\n", num);
		for (int i = 0; i < num; ++i) {
			printf("    %d for device:%d (%d)\n", pads[i].id,
			       pads[i].entity_id, pads[i].flags);
		}

		const struct media_v2_link *links = mp_device_get_links(device);
		num = mp_device_get_num_links(device);
		printf("  Links (%ld):\n", num);
		for (int i = 0; i < num; ++i) {
			printf("    %d from:%d to:%d (%d)\n", links[i].id,
			       links[i].source_id, links[i].sink_id, links[i].flags);
		}

		list = mp_device_list_next(list);
	}

	mp_device_list_free(list);
}
