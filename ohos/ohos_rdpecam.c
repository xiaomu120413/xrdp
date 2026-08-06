/**
 * MS-RDPECAM server protocol for the xrdp OHOS backend.
 *
 * xrdp owns DVC framing. This module implements the standard camera
 * enumerator/device state machine and forwards bounded samples through the
 * public xrdp_ohos callback ABI.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_private.h"
#include "ohos_rdpecam_internal.h"

#include "log.h"
#include "os_calls.h"
#include "parse.h"
#include "string_calls.h"

#define OHOS_RDPECAM_ENUMERATOR_NAME "RDCamera_Device_Enumerator"
#define OHOS_RDPECAM_DVC_FLAGS 1
#define OHOS_RDPECAM_MIN_VERSION 1
#define OHOS_RDPECAM_MAX_VERSION 2
#define OHOS_RDPECAM_MAX_PDU_BYTES (32 * 1024 * 1024)
#define OHOS_RDPECAM_MAX_FRAME_BYTES (32 * 1024 * 1024)

#define CAM_MSG_SUCCESS_RESPONSE 0x01
#define CAM_MSG_ERROR_RESPONSE 0x02
#define CAM_MSG_SELECT_VERSION_REQUEST 0x03
#define CAM_MSG_SELECT_VERSION_RESPONSE 0x04
#define CAM_MSG_DEVICE_ADDED_NOTIFICATION 0x05
#define CAM_MSG_DEVICE_REMOVED_NOTIFICATION 0x06
#define CAM_MSG_ACTIVATE_DEVICE_REQUEST 0x07
#define CAM_MSG_DEACTIVATE_DEVICE_REQUEST 0x08
#define CAM_MSG_STREAM_LIST_REQUEST 0x09
#define CAM_MSG_STREAM_LIST_RESPONSE 0x0a
#define CAM_MSG_MEDIA_TYPE_LIST_REQUEST 0x0b
#define CAM_MSG_MEDIA_TYPE_LIST_RESPONSE 0x0c
#define CAM_MSG_START_STREAMS_REQUEST 0x0f
#define CAM_MSG_STOP_STREAMS_REQUEST 0x10
#define CAM_MSG_SAMPLE_REQUEST 0x11
#define CAM_MSG_SAMPLE_RESPONSE 0x12
#define CAM_MSG_SAMPLE_ERROR_RESPONSE 0x13


static struct ohos_rdpecam *
ohos_rdpecam_from_mod(struct mod *mod)
{
    struct ohos_mod *self;
    if (mod == 0)
    {
        return 0;
    }
    self = (struct ohos_mod *)mod->handle;
    return self == 0 ? 0 : &self->rdpecam;
}

static void
ohos_rdpecam_emit(struct ohos_rdpecam *camera, uint32_t type,
                  int32_t status, const void *data, uint32_t data_bytes)
{
    struct xrdp_ohos_rdpecam_event event;
    if (camera == 0)
    {
        return;
    }
    g_memset(&event, 0, sizeof(event));
    event.size = sizeof(event);
    event.version = XRDP_OHOS_RDPECAM_EVENT_VERSION;
    event.type = type;
    event.status = status;
    event.format = camera->selected_media.format;
    event.width = camera->selected_media.width;
    event.height = camera->selected_media.height;
    event.frame_rate_numerator = camera->selected_media.frame_rate_numerator;
    event.frame_rate_denominator = camera->selected_media.frame_rate_denominator;
    event.stream_index = camera->selected_stream < 0 ? 0 :
                         (uint32_t)camera->selected_stream;
    event.sequence = camera->sample_count;
    event.data = data;
    event.data_bytes = data_bytes;
    g_strncpy(event.device_name, camera->device_name,
              sizeof(event.device_name) - 1);
    g_strncpy(event.channel_name, camera->device_channel_name,
              sizeof(event.channel_name) - 1);
    ohos_forward_rdpecam_event(&event);
}

static void
ohos_rdpecam_free_fragment(struct stream **fragment)
{
    if (fragment != 0)
    {
        free_stream(*fragment);
        *fragment = 0;
    }
}

static void
ohos_rdpecam_reset_device_state(struct ohos_rdpecam *camera)
{
    if (camera == 0)
    {
        return;
    }
    ohos_rdpecam_free_fragment(&camera->device_fragment);
    camera->device_channel_id = -1;
    camera->device_open = 0;
    camera->selected_stream = -1;
    camera->sample_in_flight = 0;
    camera->consecutive_sample_errors = 0;
    camera->device_stage = OHOS_RDPECAM_DEVICE_IDLE;
    g_memset(&camera->selected_media, 0, sizeof(camera->selected_media));
}

static int
ohos_rdpecam_send(struct ohos_rdpecam *camera, int channel_id,
                  struct stream *s)
{
    int bytes;
    int rv;
    if (camera == 0 || s == 0 || channel_id < 0 || camera->mod == 0 ||
            camera->mod->server_drdynvc_data == 0)
    {
        free_stream(s);
        return 1;
    }
    s_mark_end(s);
    bytes = (int)(s->end - s->data);
    rv = camera->mod->server_drdynvc_data(
        camera->mod, channel_id, s->data, bytes);
    free_stream(s);
    if (rv != 0)
    {
        camera->error_count++;
    }
    return rv;
}

static int
ohos_rdpecam_send_header(struct ohos_rdpecam *camera, int channel_id,
                         uint8_t message_id)
{
    struct stream *s;
    make_stream(s);
    init_stream(s, 16);
    out_uint8(s, camera->protocol_version);
    out_uint8(s, message_id);
    return ohos_rdpecam_send(camera, channel_id, s);
}

static int
ohos_rdpecam_send_media_type_list_request(struct ohos_rdpecam *camera)
{
    struct stream *s;
    make_stream(s);
    init_stream(s, 16);
    out_uint8(s, camera->protocol_version);
    out_uint8(s, CAM_MSG_MEDIA_TYPE_LIST_REQUEST);
    out_uint8(s, camera->selected_stream);
    camera->device_stage = OHOS_RDPECAM_DEVICE_WAIT_MEDIA_TYPES;
    return ohos_rdpecam_send(camera, camera->device_channel_id, s);
}

static int
ohos_rdpecam_send_start_streams(struct ohos_rdpecam *camera)
{
    const struct ohos_rdpecam_media_type *media = &camera->selected_media;
    struct stream *s;
    make_stream(s);
    init_stream(s, 64);
    out_uint8(s, camera->protocol_version);
    out_uint8(s, CAM_MSG_START_STREAMS_REQUEST);
    out_uint8(s, camera->selected_stream);
    out_uint8(s, media->format);
    out_uint32_le(s, media->width);
    out_uint32_le(s, media->height);
    out_uint32_le(s, media->frame_rate_numerator);
    out_uint32_le(s, media->frame_rate_denominator);
    out_uint32_le(s, media->pixel_aspect_numerator);
    out_uint32_le(s, media->pixel_aspect_denominator);
    out_uint8(s, media->flags);
    camera->device_stage = OHOS_RDPECAM_DEVICE_WAIT_START;
    return ohos_rdpecam_send(camera, camera->device_channel_id, s);
}

static int
ohos_rdpecam_send_sample_request(struct ohos_rdpecam *camera)
{
    struct stream *s;
    if (camera == 0 || camera->device_stage != OHOS_RDPECAM_DEVICE_STREAMING ||
            camera->sample_in_flight)
    {
        return 0;
    }
    make_stream(s);
    init_stream(s, 16);
    out_uint8(s, camera->protocol_version);
    out_uint8(s, CAM_MSG_SAMPLE_REQUEST);
    out_uint8(s, camera->selected_stream);
    camera->sample_in_flight = 1;
    return ohos_rdpecam_send(camera, camera->device_channel_id, s);
}

static int
ohos_rdpecam_process_stream_list(struct ohos_rdpecam *camera,
                                 struct stream *s)
{
    int selected = -1;
    int offered = 0;
    if (ohos_rdpecam_select_stream(s, &selected, &offered) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: no color capture stream offered count=%d",
            offered);
        return 1;
    }
    camera->selected_stream = selected;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpecam: selected stream=%d offered=%d",
        selected, offered);
    return ohos_rdpecam_send_media_type_list_request(camera);
}

static int
ohos_rdpecam_process_media_types(struct ohos_rdpecam *camera,
                                 struct stream *s)
{
    struct ohos_rdpecam_media_type selected;
    int offered = 0;
    if (ohos_rdpecam_select_media(s, &selected, &offered) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: no valid media type offered");
        return 1;
    }
    camera->selected_media = selected;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpecam: selected media format=%s size=%ux%u fps=%u/%u flags=0x%02x offered=%d",
        ohos_rdpecam_format_name(selected.format), selected.width,
        selected.height, selected.frame_rate_numerator,
        selected.frame_rate_denominator, selected.flags, offered);
    return ohos_rdpecam_send_start_streams(camera);
}

static int
ohos_rdpecam_process_sample(struct ohos_rdpecam *camera,
                            struct stream *s)
{
    int stream_index;
    int bytes;
    if (!s_check_rem(s, 1))
    {
        return 1;
    }
    in_uint8(s, stream_index);
    bytes = (int)(s->end - s->p);
    camera->sample_in_flight = 0;
    if (stream_index != camera->selected_stream || bytes <= 0 ||
            bytes > OHOS_RDPECAM_MAX_FRAME_BYTES)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: invalid sample stream=%d expected=%d bytes=%d",
            stream_index, camera->selected_stream, bytes);
        camera->sample_error_count++;
        return 1;
    }
    camera->sample_count++;
    camera->sample_bytes += (uint64_t)bytes;
    camera->consecutive_sample_errors = 0;
    ohos_rdpecam_emit(camera, XRDP_OHOS_RDPECAM_EVENT_SAMPLE, 0,
                      s->p, (uint32_t)bytes);
    if (camera->sample_count <= 3 || (camera->sample_count % 300) == 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpecam: sample seq=%llu bytes=%d total_bytes=%llu",
            (unsigned long long)camera->sample_count, bytes,
            (unsigned long long)camera->sample_bytes);
    }
    return ohos_rdpecam_send_sample_request(camera);
}

static int
ohos_rdpecam_process_device_message(struct ohos_rdpecam *camera,
                                    struct stream *s)
{
    int version;
    int message_id;
    if (!s_check_rem(s, 2))
    {
        return 1;
    }
    in_uint8(s, version);
    in_uint8(s, message_id);
    if (version != camera->protocol_version)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: device version mismatch got=%d expected=%d",
            version, camera->protocol_version);
        return 1;
    }
    switch (message_id)
    {
        case CAM_MSG_SUCCESS_RESPONSE:
            if (camera->device_stage == OHOS_RDPECAM_DEVICE_WAIT_ACTIVATE)
            {
                camera->device_stage = OHOS_RDPECAM_DEVICE_WAIT_STREAM_LIST;
                return ohos_rdpecam_send_header(
                    camera, camera->device_channel_id,
                    CAM_MSG_STREAM_LIST_REQUEST);
            }
            if (camera->device_stage == OHOS_RDPECAM_DEVICE_WAIT_START)
            {
                camera->device_stage = OHOS_RDPECAM_DEVICE_STREAMING;
                camera->stream_start_count++;
                ohos_rdpecam_emit(camera,
                    XRDP_OHOS_RDPECAM_EVENT_STREAM_STARTED, 0, 0, 0);
                LOG(LOG_LEVEL_INFO,
                    "xrdp.ohos.rdpecam: stream started device=%s channel=%s",
                    camera->device_name, camera->device_channel_name);
                return ohos_rdpecam_send_sample_request(camera);
            }
            return 0;

        case CAM_MSG_ERROR_RESPONSE:
        {
            int error_code;
            if (!s_check_rem(s, 4))
            {
                return 1;
            }
            in_uint32_le(s, error_code);
            camera->error_count++;
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.rdpecam: client error stage=%d code=0x%08x",
                camera->device_stage, error_code);
            ohos_rdpecam_emit(camera, XRDP_OHOS_RDPECAM_EVENT_ERROR,
                              error_code, 0, 0);
            return 1;
        }

        case CAM_MSG_STREAM_LIST_RESPONSE:
            if (camera->device_stage != OHOS_RDPECAM_DEVICE_WAIT_STREAM_LIST)
            {
                return 1;
            }
            return ohos_rdpecam_process_stream_list(camera, s);

        case CAM_MSG_MEDIA_TYPE_LIST_RESPONSE:
            if (camera->device_stage != OHOS_RDPECAM_DEVICE_WAIT_MEDIA_TYPES)
            {
                return 1;
            }
            return ohos_rdpecam_process_media_types(camera, s);

        case CAM_MSG_SAMPLE_RESPONSE:
            if (camera->device_stage != OHOS_RDPECAM_DEVICE_STREAMING ||
                    !camera->sample_in_flight)
            {
                return 1;
            }
            return ohos_rdpecam_process_sample(camera, s);

        case CAM_MSG_SAMPLE_ERROR_RESPONSE:
        {
            int stream_index;
            int error_code;
            if (!s_check_rem(s, 5))
            {
                return 1;
            }
            in_uint8(s, stream_index);
            in_uint32_le(s, error_code);
            camera->sample_in_flight = 0;
            camera->sample_error_count++;
            camera->consecutive_sample_errors++;
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.rdpecam: sample error stream=%d code=0x%08x consecutive=%u",
                stream_index, error_code,
                camera->consecutive_sample_errors);
            ohos_rdpecam_emit(camera, XRDP_OHOS_RDPECAM_EVENT_ERROR,
                              error_code, 0, 0);
            return camera->consecutive_sample_errors >= 3 ? 1 :
                   ohos_rdpecam_send_sample_request(camera);
        }

        default:
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.rdpecam: unsupported device message=0x%02x",
                message_id);
            return 1;
    }
}

static int
ohos_rdpecam_open_device(struct ohos_rdpecam *camera)
{
    int rv;
    if (camera == 0 || camera->device_channel_name[0] == '\0' ||
            camera->device_channel_id >= 0 || camera->mod == 0 ||
            camera->mod->server_drdynvc_open == 0)
    {
        return 0;
    }
    rv = camera->mod->server_drdynvc_open(
        camera->mod, camera->device_channel_name, OHOS_RDPECAM_DVC_FLAGS,
        &camera->device_procs, &camera->device_channel_id);
    LOG(rv == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpecam: device channel open requested name=%s id=%d rv=%d",
        camera->device_channel_name, camera->device_channel_id, rv);
    if (rv != 0)
    {
        camera->device_channel_id = -1;
        camera->error_count++;
    }
    return rv;
}

static int
ohos_rdpecam_read_channel_name(struct stream *s, char *name, int capacity)
{
    int bytes;
    char *terminator;
    if (s == 0 || name == 0 || capacity <= 1)
    {
        return 1;
    }
    bytes = (int)(s->end - s->p);
    if (bytes <= 1 || bytes > OHOS_RDPECAM_MAX_CHANNEL_NAME + 1)
    {
        return 1;
    }
    terminator = s->p;
    while (terminator < s->end && *terminator != '\0')
    {
        terminator++;
    }
    if (terminator == 0 || terminator != s->end - 1)
    {
        return 1;
    }
    g_memset(name, 0, capacity);
    g_memcpy(name, s->p, bytes);
    s->p += bytes;
    return 0;
}

static int
ohos_rdpecam_process_device_added(struct ohos_rdpecam *camera,
                                  struct stream *s)
{
    unsigned int required;
    char device_name[OHOS_RDPECAM_MAX_DEVICE_NAME];
    char channel_name[OHOS_RDPECAM_MAX_CHANNEL_NAME + 1];
    char *scan;
    int found = 0;
    if (!s_check_rem(s, 4))
    {
        return 1;
    }
    for (scan = s->p; scan + 1 < s->end; scan += 2)
    {
        if (scan[0] == '\0' && scan[1] == '\0')
        {
            found = 1;
            break;
        }
    }
    if (!found)
    {
        return 1;
    }
    required = in_utf16_le_terminated_as_utf8(
        s, device_name, sizeof(device_name));
    if (required == 0 || required > sizeof(device_name) ||
            ohos_rdpecam_read_channel_name(
                s, channel_name, sizeof(channel_name)) != 0)
    {
        return 1;
    }
    camera->device_added_count++;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpecam: device added name=%s channel=%s total=%llu",
        device_name, channel_name,
        (unsigned long long)camera->device_added_count);
    if (camera->device_channel_id < 0 && camera->device_channel_name[0] == '\0')
    {
        g_strncpy(camera->device_name, device_name,
                  sizeof(camera->device_name) - 1);
        g_strncpy(camera->device_channel_name, channel_name,
                  sizeof(camera->device_channel_name) - 1);
        ohos_rdpecam_emit(camera, XRDP_OHOS_RDPECAM_EVENT_DEVICE_ADDED,
                          0, 0, 0);
        return ohos_rdpecam_open_device(camera);
    }
    return 0;
}

static int
ohos_rdpecam_process_device_removed(struct ohos_rdpecam *camera,
                                    struct stream *s)
{
    char channel_name[OHOS_RDPECAM_MAX_CHANNEL_NAME + 1];
    if (ohos_rdpecam_read_channel_name(
            s, channel_name, sizeof(channel_name)) != 0)
    {
        return 1;
    }
    camera->device_removed_count++;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpecam: device removed channel=%s total=%llu",
        channel_name, (unsigned long long)camera->device_removed_count);
    if (g_strncmp(channel_name, camera->device_channel_name,
                  sizeof(camera->device_channel_name)) == 0)
    {
        ohos_rdpecam_emit(camera, XRDP_OHOS_RDPECAM_EVENT_DEVICE_REMOVED,
                          0, 0, 0);
        if (camera->device_channel_id >= 0 && camera->mod != 0 &&
                camera->mod->server_drdynvc_close != 0)
        {
            (void)camera->mod->server_drdynvc_close(
                camera->mod, camera->device_channel_id);
        }
        ohos_rdpecam_reset_device_state(camera);
        camera->device_name[0] = '\0';
        camera->device_channel_name[0] = '\0';
    }
    return 0;
}

static int
ohos_rdpecam_process_enumerator_message(struct ohos_rdpecam *camera,
                                        struct stream *s)
{
    int version;
    int message_id;
    if (!s_check_rem(s, 2))
    {
        return 1;
    }
    in_uint8(s, version);
    in_uint8(s, message_id);
    if (version < OHOS_RDPECAM_MIN_VERSION ||
            version > OHOS_RDPECAM_MAX_VERSION)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: unsupported enumerator version=%d",
            version);
        return 1;
    }
    camera->protocol_version = (uint8_t)version;
    switch (message_id)
    {
        case CAM_MSG_SELECT_VERSION_REQUEST:
            camera->version_selected = 1;
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.rdpecam: protocol version selected=%d", version);
            return ohos_rdpecam_send_header(
                camera, camera->enumerator_channel_id,
                CAM_MSG_SELECT_VERSION_RESPONSE);

        case CAM_MSG_DEVICE_ADDED_NOTIFICATION:
            if (!camera->version_selected)
            {
                return 1;
            }
            return ohos_rdpecam_process_device_added(camera, s);

        case CAM_MSG_DEVICE_REMOVED_NOTIFICATION:
            if (!camera->version_selected)
            {
                return 1;
            }
            return ohos_rdpecam_process_device_removed(camera, s);

        default:
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.rdpecam: unsupported enumerator message=0x%02x",
                message_id);
            return 1;
    }
}

static int
ohos_rdpecam_process_complete(struct ohos_rdpecam *camera, int chan_id,
                              struct stream *s)
{
    int rv;
    if (chan_id == camera->enumerator_channel_id)
    {
        rv = ohos_rdpecam_process_enumerator_message(camera, s);
    }
    else if (chan_id == camera->device_channel_id)
    {
        rv = ohos_rdpecam_process_device_message(camera, s);
    }
    else
    {
        return 1;
    }
    if (rv != 0)
    {
        camera->error_count++;
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: protocol error channel=%d errors=%llu",
            chan_id, (unsigned long long)camera->error_count);
        ohos_rdpecam_emit(camera, XRDP_OHOS_RDPECAM_EVENT_ERROR,
                          -1, 0, 0);
        if (camera->mod != 0 && camera->mod->server_drdynvc_close != 0)
        {
            (void)camera->mod->server_drdynvc_close(camera->mod, chan_id);
        }
        return 0;
    }
    return 0;
}

static int
ohos_rdpecam_process_fragment(struct ohos_rdpecam *camera, int chan_id,
                              struct stream **fragment, char *data, int bytes)
{
    int rv;
    if (camera == 0 || fragment == 0 || *fragment == 0 || bytes < 0 ||
            !s_check_rem_out(*fragment, bytes))
    {
        return 1;
    }
    out_uint8a(*fragment, data, bytes);
    if ((*fragment)->p != (*fragment)->end)
    {
        return 0;
    }
    (*fragment)->p = (*fragment)->data;
    rv = ohos_rdpecam_process_complete(camera, chan_id, *fragment);
    ohos_rdpecam_free_fragment(fragment);
    return rv;
}

static int
ohos_rdpecam_dvc_data_first_common(void *module, int chan_id,
                                   char *data, int bytes, int total_bytes)
{
    struct ohos_rdpecam *camera =
        ohos_rdpecam_from_mod((struct mod *)module);
    struct stream **fragment;
    if (camera == 0 || total_bytes <= 0 ||
            total_bytes > OHOS_RDPECAM_MAX_PDU_BYTES ||
            bytes < 0 || bytes > total_bytes)
    {
        return 1;
    }
    if (chan_id == camera->enumerator_channel_id)
    {
        fragment = &camera->enumerator_fragment;
    }
    else if (chan_id == camera->device_channel_id)
    {
        fragment = &camera->device_fragment;
    }
    else
    {
        return 1;
    }
    ohos_rdpecam_free_fragment(fragment);
    make_stream(*fragment);
    init_stream(*fragment, total_bytes);
    (*fragment)->end = (*fragment)->data + total_bytes;
    return ohos_rdpecam_process_fragment(
        camera, chan_id, fragment, data, bytes);
}

static int
ohos_rdpecam_dvc_data_common(void *module, int chan_id,
                             char *data, int bytes)
{
    struct ohos_rdpecam *camera =
        ohos_rdpecam_from_mod((struct mod *)module);
    struct stream **fragment;
    struct stream local;
    if (camera == 0 || bytes < 0 || bytes > OHOS_RDPECAM_MAX_PDU_BYTES)
    {
        return 1;
    }
    if (chan_id == camera->enumerator_channel_id)
    {
        fragment = &camera->enumerator_fragment;
    }
    else if (chan_id == camera->device_channel_id)
    {
        fragment = &camera->device_fragment;
    }
    else
    {
        return 1;
    }
    if (*fragment != 0)
    {
        return ohos_rdpecam_process_fragment(
            camera, chan_id, fragment, data, bytes);
    }
    g_memset(&local, 0, sizeof(local));
    local.data = data;
    local.p = data;
    local.end = data + bytes;
    return ohos_rdpecam_process_complete(camera, chan_id, &local);
}

static int
ohos_rdpecam_enumerator_open_response(void *module, int chan_id,
                                      int creation_status)
{
    struct ohos_rdpecam *camera =
        ohos_rdpecam_from_mod((struct mod *)module);
    if (camera == 0 || chan_id != camera->enumerator_channel_id)
    {
        return 1;
    }
    if (creation_status != 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpecam: client enumerator unavailable status=0x%08x",
            creation_status);
        camera->enumerator_channel_id = -1;
        return 0;
    }
    camera->enumerator_open = 1;
    camera->enumerator_open_count++;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpecam: enumerator channel ready id=%d opens=%llu",
        chan_id, (unsigned long long)camera->enumerator_open_count);
    return 0;
}

static int
ohos_rdpecam_device_open_response(void *module, int chan_id,
                                  int creation_status)
{
    struct ohos_rdpecam *camera =
        ohos_rdpecam_from_mod((struct mod *)module);
    if (camera == 0 || chan_id != camera->device_channel_id)
    {
        return 1;
    }
    if (creation_status != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: client device channel unavailable name=%s status=0x%08x",
            camera->device_channel_name, creation_status);
        ohos_rdpecam_emit(camera, XRDP_OHOS_RDPECAM_EVENT_ERROR,
                          creation_status, 0, 0);
        ohos_rdpecam_reset_device_state(camera);
        return 0;
    }
    camera->device_open = 1;
    camera->device_open_count++;
    camera->device_stage = OHOS_RDPECAM_DEVICE_WAIT_ACTIVATE;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpecam: device channel ready id=%d opens=%llu",
        chan_id, (unsigned long long)camera->device_open_count);
    return ohos_rdpecam_send_header(
        camera, camera->device_channel_id,
        CAM_MSG_ACTIVATE_DEVICE_REQUEST);
}

static int
ohos_rdpecam_enumerator_close_response(void *module, int chan_id)
{
    struct ohos_rdpecam *camera =
        ohos_rdpecam_from_mod((struct mod *)module);
    if (camera != 0 && chan_id == camera->enumerator_channel_id)
    {
        camera->enumerator_channel_id = -1;
        camera->enumerator_open = 0;
        camera->version_selected = 0;
        ohos_rdpecam_free_fragment(&camera->enumerator_fragment);
    }
    return 0;
}

static int
ohos_rdpecam_device_close_response(void *module, int chan_id)
{
    struct ohos_rdpecam *camera =
        ohos_rdpecam_from_mod((struct mod *)module);
    if (camera != 0 && chan_id == camera->device_channel_id)
    {
        if (camera->device_stage == OHOS_RDPECAM_DEVICE_STREAMING)
        {
            ohos_rdpecam_emit(camera,
                XRDP_OHOS_RDPECAM_EVENT_STREAM_STOPPED, 0, 0, 0);
        }
        ohos_rdpecam_reset_device_state(camera);
    }
    return 0;
}

static int
ohos_rdpecam_start(struct ohos_rdpecam *camera)
{
    int rv;
    if (camera == 0 || !camera->enabled || !camera->connected ||
            !camera->dvc_ready || camera->enumerator_channel_id >= 0)
    {
        return 0;
    }
    if (!ohos_rdpecam_callback_registered())
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpecam: no native sample callback; channel deferred");
        return 0;
    }
    if (camera->mod == 0 || camera->mod->server_drdynvc_open == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.rdpecam: module DVC bridge unavailable");
        return 1;
    }
    rv = camera->mod->server_drdynvc_open(
        camera->mod, OHOS_RDPECAM_ENUMERATOR_NAME, OHOS_RDPECAM_DVC_FLAGS,
        &camera->enumerator_procs, &camera->enumerator_channel_id);
    LOG(rv == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.rdpecam: enumerator open requested id=%d rv=%d",
        camera->enumerator_channel_id, rv);
    if (rv != 0)
    {
        camera->enumerator_channel_id = -1;
        camera->error_count++;
    }
    return rv;
}

void
ohos_rdpecam_init(struct ohos_rdpecam *camera, struct mod *mod)
{
    if (camera == 0)
    {
        return;
    }
    g_memset(camera, 0, sizeof(*camera));
    camera->mod = mod;
    camera->enabled = 1;
    camera->enumerator_channel_id = -1;
    camera->device_channel_id = -1;
    camera->selected_stream = -1;
    camera->protocol_version = OHOS_RDPECAM_MAX_VERSION;
    camera->enumerator_procs.open_response =
        ohos_rdpecam_enumerator_open_response;
    camera->enumerator_procs.close_response =
        ohos_rdpecam_enumerator_close_response;
    camera->enumerator_procs.data_first =
        ohos_rdpecam_dvc_data_first_common;
    camera->enumerator_procs.data = ohos_rdpecam_dvc_data_common;
    camera->device_procs.open_response = ohos_rdpecam_device_open_response;
    camera->device_procs.close_response = ohos_rdpecam_device_close_response;
    camera->device_procs.data_first = ohos_rdpecam_dvc_data_first_common;
    camera->device_procs.data = ohos_rdpecam_dvc_data_common;
}

void
ohos_rdpecam_deinit(struct ohos_rdpecam *camera)
{
    ohos_rdpecam_disconnect(camera, "deinit");
}

void
ohos_rdpecam_set_enabled(struct ohos_rdpecam *camera, int enabled)
{
    if (camera != 0)
    {
        camera->enabled = enabled != 0;
    }
}

int
ohos_rdpecam_connect(struct ohos_rdpecam *camera)
{
    if (camera == 0)
    {
        return 1;
    }
    camera->connected = 1;
    return ohos_rdpecam_start(camera);
}

void
ohos_rdpecam_disconnect(struct ohos_rdpecam *camera, const char *reason)
{
    if (camera == 0)
    {
        return;
    }
    camera->connected = 0;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpecam: disconnect reason=%s enumerator_opens=%llu devices_added=%llu devices_removed=%llu device_opens=%llu streams=%llu samples=%llu bytes=%llu sample_errors=%llu errors=%llu",
        reason == 0 ? "" : reason,
        (unsigned long long)camera->enumerator_open_count,
        (unsigned long long)camera->device_added_count,
        (unsigned long long)camera->device_removed_count,
        (unsigned long long)camera->device_open_count,
        (unsigned long long)camera->stream_start_count,
        (unsigned long long)camera->sample_count,
        (unsigned long long)camera->sample_bytes,
        (unsigned long long)camera->sample_error_count,
        (unsigned long long)camera->error_count);
    if (camera->device_channel_id >= 0 && camera->mod != 0 &&
            camera->mod->server_drdynvc_close != 0)
    {
        if (camera->device_stage == OHOS_RDPECAM_DEVICE_STREAMING)
        {
            (void)ohos_rdpecam_send_header(
                camera, camera->device_channel_id,
                CAM_MSG_STOP_STREAMS_REQUEST);
            (void)ohos_rdpecam_send_header(
                camera, camera->device_channel_id,
                CAM_MSG_DEACTIVATE_DEVICE_REQUEST);
        }
        (void)camera->mod->server_drdynvc_close(
            camera->mod, camera->device_channel_id);
    }
    if (camera->enumerator_channel_id >= 0 && camera->mod != 0 &&
            camera->mod->server_drdynvc_close != 0)
    {
        (void)camera->mod->server_drdynvc_close(
            camera->mod, camera->enumerator_channel_id);
    }
    ohos_rdpecam_free_fragment(&camera->enumerator_fragment);
    ohos_rdpecam_reset_device_state(camera);
    camera->enumerator_channel_id = -1;
    camera->enumerator_open = 0;
    camera->version_selected = 0;
    camera->device_name[0] = '\0';
    camera->device_channel_name[0] = '\0';
}

int
ohos_rdpecam_drdynvc_ready(struct ohos_rdpecam *camera)
{
    if (camera == 0)
    {
        return 1;
    }
    camera->dvc_ready = 1;
    return ohos_rdpecam_start(camera);
}
