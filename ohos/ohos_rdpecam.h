#ifndef XRDP_OHOS_RDPECAM_H
#define XRDP_OHOS_RDPECAM_H

#include "arch.h"
#include "xup.h"

#include <stdint.h>

struct stream;

#define OHOS_RDPECAM_MAX_DEVICE_NAME 256
#define OHOS_RDPECAM_MAX_CHANNEL_NAME 260

struct ohos_rdpecam_media_type
{
    uint8_t format;
    uint32_t width;
    uint32_t height;
    uint32_t frame_rate_numerator;
    uint32_t frame_rate_denominator;
    uint32_t pixel_aspect_numerator;
    uint32_t pixel_aspect_denominator;
    uint8_t flags;
};

enum ohos_rdpecam_device_stage
{
    OHOS_RDPECAM_DEVICE_IDLE = 0,
    OHOS_RDPECAM_DEVICE_WAIT_ACTIVATE,
    OHOS_RDPECAM_DEVICE_WAIT_STREAM_LIST,
    OHOS_RDPECAM_DEVICE_WAIT_MEDIA_TYPES,
    OHOS_RDPECAM_DEVICE_WAIT_START,
    OHOS_RDPECAM_DEVICE_STREAMING
};

struct ohos_rdpecam
{
    struct mod *mod;
    struct xrdp_mod_drdynvc_procs enumerator_procs;
    struct xrdp_mod_drdynvc_procs device_procs;
    struct stream *enumerator_fragment;
    struct stream *device_fragment;
    struct ohos_rdpecam_media_type selected_media;
    char device_name[OHOS_RDPECAM_MAX_DEVICE_NAME];
    char device_channel_name[OHOS_RDPECAM_MAX_CHANNEL_NAME + 1];
    int enumerator_channel_id;
    int device_channel_id;
    int enabled;
    int connected;
    int dvc_ready;
    int enumerator_open;
    int device_open;
    int version_selected;
    int selected_stream;
    int sample_in_flight;
    enum ohos_rdpecam_device_stage device_stage;
    uint8_t protocol_version;
    uint64_t enumerator_open_count;
    uint64_t device_added_count;
    uint64_t device_removed_count;
    uint64_t device_open_count;
    uint64_t stream_start_count;
    uint64_t sample_count;
    uint64_t sample_bytes;
    uint64_t sample_error_count;
    uint32_t consecutive_sample_errors;
    uint64_t error_count;
};

void
ohos_rdpecam_init(struct ohos_rdpecam *camera, struct mod *mod);

void
ohos_rdpecam_deinit(struct ohos_rdpecam *camera);

void
ohos_rdpecam_set_enabled(struct ohos_rdpecam *camera, int enabled);

int
ohos_rdpecam_connect(struct ohos_rdpecam *camera);

void
ohos_rdpecam_disconnect(struct ohos_rdpecam *camera, const char *reason);

int
ohos_rdpecam_drdynvc_ready(struct ohos_rdpecam *camera);

#endif
