#include "device.h"
#include <assert.h>
#include <linux/limits.h>
#include <linux/media.h>
#include <stdio.h>

const char *
entity_type_str(uint32_t type)
{
        switch (type) {
        case MEDIA_ENT_F_UNKNOWN:
                return "UNKNOWN";
        case MEDIA_ENT_F_V4L2_SUBDEV_UNKNOWN:
                return "V4L2_SUBDEV_UNKNOWN";
        case MEDIA_ENT_F_IO_V4L:
                return "IO_V4L";
        case MEDIA_ENT_F_IO_VBI:
                return "IO_VBI";
        case MEDIA_ENT_F_IO_SWRADIO:
                return "IO_SWRADIO";
        case MEDIA_ENT_F_IO_DTV:
                return "IO_DTV";
        case MEDIA_ENT_F_DTV_DEMOD:
                return "DTV_DEMOD";
        case MEDIA_ENT_F_TS_DEMUX:
                return "TS_DEMUX";
        case MEDIA_ENT_F_DTV_CA:
                return "DTV_CA";
        case MEDIA_ENT_F_DTV_NET_DECAP:
                return "DTV_NET_DECAP";
        case MEDIA_ENT_F_CAM_SENSOR:
                return "CAM_SENSOR";
        case MEDIA_ENT_F_FLASH:
                return "FLASH";
        case MEDIA_ENT_F_LENS:
                return "LENS";
        case MEDIA_ENT_F_ATV_DECODER:
                return "ATV_DECODER";
        case MEDIA_ENT_F_TUNER:
                return "TUNER";
        case MEDIA_ENT_F_IF_VID_DECODER:
                return "IF_VID_DECODER";
        case MEDIA_ENT_F_IF_AUD_DECODER:
                return "IF_AUD_DECODER";
        case MEDIA_ENT_F_AUDIO_CAPTURE:
                return "AUDIO_CAPTURE";
        case MEDIA_ENT_F_AUDIO_PLAYBACK:
                return "AUDIO_PLAYBACK";
        case MEDIA_ENT_F_AUDIO_MIXER:
                return "AUDIO_MIXER";
        case MEDIA_ENT_F_PROC_VIDEO_COMPOSER:
                return "PROC_VIDEO_COMPOSER";
        case MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER:
                return "PROC_VIDEO_PIXEL_FORMATTER";
        case MEDIA_ENT_F_PROC_VIDEO_PIXEL_ENC_CONV:
                return "PROC_VIDEO_PIXEL_ENC_CONV";
        case MEDIA_ENT_F_PROC_VIDEO_LUT:
                return "PROC_VIDEO_LUT";
        case MEDIA_ENT_F_PROC_VIDEO_SCALER:
                return "PROC_VIDEO_SCALER";
        case MEDIA_ENT_F_PROC_VIDEO_STATISTICS:
                return "PROC_VIDEO_STATISTICS";
        default:
                return "invalid type";
        }
}

const char *
intf_type_str(uint32_t type)
{
        switch (type) {
        case MEDIA_INTF_T_DVB_FE:
                return "DVB_FE";
        case MEDIA_INTF_T_DVB_DEMUX:
                return "DVB_DEMUX";
        case MEDIA_INTF_T_DVB_DVR:
                return "DVB_DVR";
        case MEDIA_INTF_T_DVB_CA:
                return "DVB_CA";
        case MEDIA_INTF_T_DVB_NET:
                return "DVB_NET";
        case MEDIA_INTF_T_V4L_VIDEO:
                return "V4L_VIDEO";
        case MEDIA_INTF_T_V4L_VBI:
                return "V4L_VBI";
        case MEDIA_INTF_T_V4L_RADIO:
                return "V4L_RADIO";
        case MEDIA_INTF_T_V4L_SUBDEV:
                return "V4L_SUBDEV";
        case MEDIA_INTF_T_V4L_SWRADIO:
                return "V4L_SWRADIO";
        case MEDIA_INTF_T_V4L_TOUCH:
                return "V4L_TOUCH";
        case MEDIA_INTF_T_ALSA_PCM_CAPTURE:
                return "ALSA_PCM_CAPTURE";
        case MEDIA_INTF_T_ALSA_PCM_PLAYBACK:
                return "ALSA_PCM_PLAYBACK";
        case MEDIA_INTF_T_ALSA_CONTROL:
                return "ALSA_CONTROL";
        case MEDIA_INTF_T_ALSA_COMPRESS:
                return "ALSA_COMPRESS";
        case MEDIA_INTF_T_ALSA_RAWMIDI:
                return "ALSA_RAWMIDI";
        case MEDIA_INTF_T_ALSA_HWDEP:
                return "ALSA_HWDEP";
        case MEDIA_INTF_T_ALSA_SEQUENCER:
                return "ALSA_SEQUENCER";
        case MEDIA_INTF_T_ALSA_TIMER:
                return "ALSA_TIMER";
        default:
                return "invalid type";
        }
}

int
main(int argc, char *argv[])
{
        MPDeviceList *list = mp_device_list_new();

        while (list) {
                MPDevice *device = mp_device_list_get(list);

                const struct media_device_info *info = mp_device_get_info(device);
                printf("%s (%s) %s\n", info->model, info->driver, info->serial);
                printf("  Path: %s\n", mp_device_list_get_path(list));
                printf("  Bus Info: %s\n", info->bus_info);
                printf("  Media Version: %d\n", info->media_version);
                printf("  HW Revision: %d\n", info->hw_revision);
                printf("  Driver Version: %d\n", info->driver_version);

                const struct media_v2_entity *entities =
                        mp_device_get_entities(device);
                size_t num = mp_device_get_num_entities(device);
                printf("  Entities (%ld):\n", num);
                for (int i = 0; i < num; ++i) {
                        printf("    %d %s (%s)\n",
                               entities[i].id,
                               entities[i].name,
                               entity_type_str(entities[i].function));
                }

                const struct media_v2_interface *interfaces =
                        mp_device_get_interfaces(device);
                num = mp_device_get_num_interfaces(device);
                printf("  Interfaces (%ld):\n", num);
                for (int i = 0; i < num; ++i) {
                        // Unused
                        assert(interfaces[i].flags == 0);

                        char buf[PATH_MAX];
                        buf[0] = '\0';
                        mp_find_device_path(interfaces[i].devnode, buf, PATH_MAX);

                        printf("    %d (%s) devnode %d:%d %s\n",
                               interfaces[i].id,
                               intf_type_str(interfaces[i].intf_type),
                               interfaces[i].devnode.major,
                               interfaces[i].devnode.minor,
                               buf);
                }

                const struct media_v2_pad *pads = mp_device_get_pads(device);
                num = mp_device_get_num_pads(device);
                printf("  Pads (%ld):\n", num);
                for (int i = 0; i < num; ++i) {
                        printf("    %d for device:%d (",
                               pads[i].id,
                               pads[i].entity_id);

                        if (pads[i].flags & MEDIA_PAD_FL_SINK)
                                printf("SINK ");
                        if (pads[i].flags & MEDIA_PAD_FL_SOURCE)
                                printf("SOURCE ");
                        if (pads[i].flags & MEDIA_PAD_FL_MUST_CONNECT)
                                printf("MUST_CONNECT ");
                        printf(")\n");
                }

                const struct media_v2_link *links = mp_device_get_links(device);
                num = mp_device_get_num_links(device);
                printf("  Links (%ld):\n", num);
                for (int i = 0; i < num; ++i) {
                        printf("    %d from:%d to:%d (",
                               links[i].id,
                               links[i].source_id,
                               links[i].sink_id);

                        if (links[i].flags & MEDIA_LNK_FL_ENABLED)
                                printf("ENABLED ");
                        if (links[i].flags & MEDIA_LNK_FL_IMMUTABLE)
                                printf("IMMUTABLE ");
                        if (links[i].flags & MEDIA_LNK_FL_DYNAMIC)
                                printf("DYNAMIC ");

                        uint32_t type = links[i].flags & MEDIA_LNK_FL_LINK_TYPE;
                        if (type == MEDIA_LNK_FL_INTERFACE_LINK) {
                                printf("INTERFACE)\n");
                        } else {
                                assert(type == MEDIA_LNK_FL_DATA_LINK);
                                printf("DATA)\n");
                        }
                }

                list = mp_device_list_next(list);
        }

        mp_device_list_free(list);
}
