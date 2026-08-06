#ifndef XRDP_OHOS_RDPECAM_INTERNAL_H
#define XRDP_OHOS_RDPECAM_INTERNAL_H

#include "ohos_rdpecam.h"
#include "xrdp_ohos.h"

struct stream;

#define CAM_MEDIA_FORMAT_H264 0x01
#define CAM_MEDIA_FORMAT_MJPG 0x02
#define CAM_MEDIA_FORMAT_YUY2 0x03
#define CAM_MEDIA_FORMAT_NV12 0x04
#define CAM_MEDIA_FORMAT_I420 0x05
#define CAM_MEDIA_FORMAT_RGB24 0x06
#define CAM_MEDIA_FORMAT_RGB32 0x07

const char *
ohos_rdpecam_format_name(uint8_t format);

int
ohos_rdpecam_select_stream(struct stream *s, int *selected, int *offered);

int
ohos_rdpecam_select_media(struct stream *s,
                          struct ohos_rdpecam_media_type *selected,
                          int *offered);

#endif
