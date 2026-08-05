/**
 * MS-RDPEAI audio-input protocol for the xrdp OHOS backend.
 *
 * The protocol flow follows the xrdp chansrv audin implementation. Only the
 * platform sink is OHOS-specific.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_private.h"
#include "ohos_audin_renderer.h"

#include "log.h"
#include "os_calls.h"
#include "parse.h"
#include "xrdp_constants.h"

#define MSG_SNDIN_VERSION       1
#define MSG_SNDIN_FORMATS       2
#define MSG_SNDIN_OPEN          3
#define MSG_SNDIN_OPEN_REPLY    4
#define MSG_SNDIN_DATA_INCOMING 5
#define MSG_SNDIN_DATA          6
#define MSG_SNDIN_FORMATCHANGE  7

#define OHOS_AUDIN_VERSION 1
#define OHOS_AUDIN_NAME "AUDIO_INPUT"
#define OHOS_AUDIN_FLAGS 1
#define OHOS_AUDIN_FRAMES_PER_PACKET 2048
#define OHOS_AUDIN_MAX_PDU_BYTES (1024 * 1024)

static const struct ohos_audin_format g_server_formats[] =
{
    { WAVE_FORMAT_PCM, 1, 48000, 96000, 2, 16 },
    { WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16 },
    { WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16 },
    { WAVE_FORMAT_PCM, 1, 8000, 16000, 2, 16 },
    { WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16 },
    { WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16 }
};

static struct ohos_audin *
ohos_audin_from_mod(struct mod *mod)
{
    struct ohos_mod *self;
    if (mod == 0)
    {
        return 0;
    }
    self = (struct ohos_mod *)mod->handle;
    return self == 0 ? 0 : &self->audin;
}

static int
ohos_audin_format_supported(const struct ohos_audin_format *format)
{
    int index;
    if (format == 0)
    {
        return 0;
    }
    for (index = 0;
            index < (int)(sizeof(g_server_formats) /
                          sizeof(g_server_formats[0]));
            index++)
    {
        const struct ohos_audin_format *server = &g_server_formats[index];
        if (format->tag == server->tag &&
                format->channels == server->channels &&
                format->rate == server->rate &&
                format->bits == server->bits &&
                format->block_align == server->block_align)
        {
            return 1;
        }
    }
    return 0;
}

static int
ohos_audin_send(struct ohos_audin *audin, struct stream *s)
{
    int bytes;
    if (audin == 0 || s == 0 || audin->channel_id < 0 ||
            audin->mod == 0 || audin->mod->server_drdynvc_data == 0)
    {
        return 1;
    }
    s_mark_end(s);
    bytes = (int)(s->end - s->data);
    return audin->mod->server_drdynvc_data(
        audin->mod, audin->channel_id, s->data, bytes);
}

static int
ohos_audin_send_version(struct ohos_audin *audin)
{
    struct stream *s;
    int rv;
    make_stream(s);
    init_stream(s, 16);
    out_uint8(s, MSG_SNDIN_VERSION);
    out_uint32_le(s, OHOS_AUDIN_VERSION);
    rv = ohos_audin_send(audin, s);
    free_stream(s);
    return rv;
}

static int
ohos_audin_send_formats(struct ohos_audin *audin)
{
    const int count = (int)(sizeof(g_server_formats) /
                            sizeof(g_server_formats[0]));
    struct stream *s;
    int index;
    int rv;
    make_stream(s);
    init_stream(s, 64 + count * 18);
    out_uint8(s, MSG_SNDIN_FORMATS);
    out_uint32_le(s, count);
    out_uint32_le(s, 0);
    for (index = 0; index < count; index++)
    {
        const struct ohos_audin_format *format = &g_server_formats[index];
        out_uint16_le(s, format->tag);
        out_uint16_le(s, format->channels);
        out_uint32_le(s, format->rate);
        out_uint32_le(s, format->average_bytes);
        out_uint16_le(s, format->block_align);
        out_uint16_le(s, format->bits);
        out_uint16_le(s, 0);
    }
    rv = ohos_audin_send(audin, s);
    free_stream(s);
    return rv;
}

static int
ohos_audin_send_open(struct ohos_audin *audin)
{
    const struct ohos_audin_format *format;
    struct stream *s;
    int rv;
    if (audin == 0 || audin->selected_format < 0 ||
            audin->selected_format >= audin->client_format_count)
    {
        return 1;
    }
    format = &audin->client_formats[audin->selected_format];
    make_stream(s);
    init_stream(s, 64);
    out_uint8(s, MSG_SNDIN_OPEN);
    out_uint32_le(s, OHOS_AUDIN_FRAMES_PER_PACKET);
    out_uint32_le(s, audin->selected_format);
    out_uint16_le(s, format->tag);
    out_uint16_le(s, format->channels);
    out_uint32_le(s, format->rate);
    out_uint32_le(s, format->average_bytes);
    out_uint16_le(s, format->block_align);
    out_uint16_le(s, format->bits);
    out_uint16_le(s, 0);
    rv = ohos_audin_send(audin, s);
    free_stream(s);
    return rv;
}

static int
ohos_audin_process_formats(struct ohos_audin *audin, struct stream *s)
{
    int advertised_count;
    int index;
    int selected = -1;
    if (!s_check_rem(s, 8))
    {
        return 1;
    }
    in_uint32_le(s, advertised_count);
    in_uint8s(s, 4);
    if (advertised_count <= 0 || advertised_count > OHOS_AUDIN_MAX_FORMATS)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.audin: invalid client format count=%d",
            advertised_count);
        return 1;
    }
    g_memset(audin->client_formats, 0, sizeof(audin->client_formats));
    audin->client_format_count = advertised_count;
    for (index = 0; index < advertised_count; index++)
    {
        struct ohos_audin_format *format = &audin->client_formats[index];
        int extra_bytes;
        if (!s_check_rem(s, 18))
        {
            return 1;
        }
        in_uint16_le(s, format->tag);
        in_uint16_le(s, format->channels);
        in_uint32_le(s, format->rate);
        in_uint32_le(s, format->average_bytes);
        in_uint16_le(s, format->block_align);
        in_uint16_le(s, format->bits);
        in_uint16_le(s, extra_bytes);
        if (extra_bytes < 0 || extra_bytes > 4096 ||
                !s_check_rem(s, extra_bytes))
        {
            return 1;
        }
        in_uint8s(s, extra_bytes);
        if (selected < 0 && ohos_audin_format_supported(format))
        {
            selected = index;
        }
    }
    if (selected < 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.audin: client offered no supported PCM format");
        return 1;
    }
    audin->selected_format = selected;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.audin: selected format index=%d rate=%d channels=%d bits=%d",
        selected, audin->client_formats[selected].rate,
        audin->client_formats[selected].channels,
        audin->client_formats[selected].bits);
    return ohos_audin_send_open(audin);
}

static int
ohos_audin_open_renderer(struct ohos_audin *audin, int format_index)
{
    const struct ohos_audin_format *format;
    if (audin == 0 || format_index < 0 ||
            format_index >= audin->client_format_count ||
            !ohos_audin_format_supported(
                &audin->client_formats[format_index]))
    {
        return 1;
    }
    format = &audin->client_formats[format_index];
    audin->selected_format = format_index;
    return ohos_audin_renderer_open(
        audin->renderer, (uint32_t)format->rate,
        (uint16_t)format->channels, (uint16_t)format->bits);
}

static int
ohos_audin_process_message(struct ohos_audin *audin, struct stream *s)
{
    int code;
    if (audin == 0 || s == 0 || !s_check_rem(s, 1))
    {
        return 1;
    }
    in_uint8(s, code);
    switch (code)
    {
        case MSG_SNDIN_VERSION:
            if (!s_check_rem(s, 4))
            {
                return 1;
            }
            in_uint8s(s, 4);
            return ohos_audin_send_formats(audin);

        case MSG_SNDIN_FORMATS:
            return ohos_audin_process_formats(audin, s);

        case MSG_SNDIN_OPEN_REPLY:
        {
            int result;
            if (!s_check_rem(s, 4))
            {
                return 1;
            }
            in_uint32_le(s, result);
            if (result != 0 ||
                    ohos_audin_open_renderer(audin,
                                             audin->selected_format) != 0)
            {
                LOG(LOG_LEVEL_WARNING,
                    "xrdp.ohos.audin: open rejected result=0x%08x",
                    result);
                return 1;
            }
            audin->open_count++;
            LOG(LOG_LEVEL_INFO, "xrdp.ohos.audin: stream open");
            return 0;
        }

        case MSG_SNDIN_DATA_INCOMING:
            return 0;

        case MSG_SNDIN_DATA:
        {
            int bytes = (int)(s->end - s->p);
            audin->data_count++;
            audin->data_bytes += bytes;
            if (bytes <= 0 || ohos_audin_renderer_push(
                    audin->renderer, s->p, (size_t)bytes) != 0)
            {
                audin->dropped_bytes += bytes > 0 ? bytes : 0;
                return bytes <= 0 ? 1 : 0;
            }
            return 0;
        }

        case MSG_SNDIN_FORMATCHANGE:
        {
            int format_index;
            if (!s_check_rem(s, 4))
            {
                return 1;
            }
            in_uint32_le(s, format_index);
            return ohos_audin_open_renderer(audin, format_index);
        }

        default:
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.audin: unsupported message=%d", code);
            return 0;
    }
}

static int
ohos_audin_process_fragment(struct ohos_audin *audin, int chan_id,
                            char *data, int bytes)
{
    int rv;
    if (audin == 0 || chan_id != audin->channel_id ||
            audin->fragment == 0 || bytes < 0 ||
            !s_check_rem_out(audin->fragment, bytes))
    {
        return 1;
    }
    out_uint8a(audin->fragment, data, bytes);
    if (audin->fragment->p != audin->fragment->end)
    {
        return 0;
    }
    audin->fragment->p = audin->fragment->data;
    rv = ohos_audin_process_message(audin, audin->fragment);
    free_stream(audin->fragment);
    audin->fragment = 0;
    return rv;
}

static int
ohos_audin_dvc_open_response(void *module, int chan_id,
                             int creation_status)
{
    struct mod *mod = (struct mod *)module;
    struct ohos_audin *audin = ohos_audin_from_mod(mod);
    if (audin == 0 || chan_id != audin->channel_id)
    {
        return 1;
    }
    if (creation_status != 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.audin: client channel unavailable status=0x%08x",
            creation_status);
        audin->channel_id = -1;
        return 0;
    }
    audin->channel_open = 1;
    return ohos_audin_send_version(audin);
}

static int
ohos_audin_dvc_close_response(void *module, int chan_id)
{
    struct mod *mod = (struct mod *)module;
    struct ohos_audin *audin = ohos_audin_from_mod(mod);
    if (audin != 0 && chan_id == audin->channel_id)
    {
        audin->channel_id = -1;
        audin->channel_open = 0;
        ohos_audin_renderer_close(audin->renderer);
    }
    return 0;
}

static int
ohos_audin_dvc_data_first(void *module, int chan_id, char *data,
                          int bytes, int total_bytes)
{
    struct mod *mod = (struct mod *)module;
    struct ohos_audin *audin = ohos_audin_from_mod(mod);
    if (audin == 0 || chan_id != audin->channel_id || total_bytes <= 0 ||
            total_bytes > OHOS_AUDIN_MAX_PDU_BYTES ||
            bytes < 0 || bytes > total_bytes)
    {
        return 1;
    }
    free_stream(audin->fragment);
    audin->fragment = 0;
    make_stream(audin->fragment);
    init_stream(audin->fragment, total_bytes);
    audin->fragment->end = audin->fragment->data + total_bytes;
    return ohos_audin_process_fragment(audin, chan_id, data, bytes);
}

static int
ohos_audin_dvc_data(void *module, int chan_id, char *data, int bytes)
{
    struct mod *mod = (struct mod *)module;
    struct ohos_audin *audin = ohos_audin_from_mod(mod);
    struct stream local;
    if (audin == 0 || chan_id != audin->channel_id || bytes < 0 ||
            bytes > OHOS_AUDIN_MAX_PDU_BYTES)
    {
        return 1;
    }
    if (audin->fragment != 0)
    {
        return ohos_audin_process_fragment(audin, chan_id, data, bytes);
    }
    g_memset(&local, 0, sizeof(local));
    local.data = data;
    local.p = data;
    local.end = data + bytes;
    return ohos_audin_process_message(audin, &local);
}

static int
ohos_audin_start(struct ohos_audin *audin)
{
    int rv;
    if (audin == 0 || !audin->enabled || !audin->connected ||
            !audin->dvc_ready || audin->channel_id >= 0)
    {
        return 0;
    }
    if (audin->mod == 0 || audin->mod->server_drdynvc_open == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.audin: module DVC bridge unavailable");
        return 1;
    }
    rv = audin->mod->server_drdynvc_open(
        audin->mod, OHOS_AUDIN_NAME, OHOS_AUDIN_FLAGS,
        &audin->dvc_procs, &audin->channel_id);
    LOG(rv == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.audin: channel open requested id=%d rv=%d",
        audin->channel_id, rv);
    if (rv != 0)
    {
        audin->channel_id = -1;
        audin->error_count++;
    }
    return rv;
}

void
ohos_audin_init(struct ohos_audin *audin, struct mod *mod)
{
    if (audin == 0)
    {
        return;
    }
    g_memset(audin, 0, sizeof(*audin));
    audin->mod = mod;
    audin->renderer = ohos_audin_renderer_create();
    audin->selected_format = -1;
    audin->channel_id = -1;
    audin->enabled = 1;
    audin->dvc_procs.open_response = ohos_audin_dvc_open_response;
    audin->dvc_procs.close_response = ohos_audin_dvc_close_response;
    audin->dvc_procs.data_first = ohos_audin_dvc_data_first;
    audin->dvc_procs.data = ohos_audin_dvc_data;
}

void
ohos_audin_deinit(struct ohos_audin *audin)
{
    if (audin == 0)
    {
        return;
    }
    ohos_audin_disconnect(audin, "deinit");
    ohos_audin_renderer_destroy(audin->renderer);
    audin->renderer = 0;
}

void
ohos_audin_set_enabled(struct ohos_audin *audin, int enabled)
{
    if (audin != 0)
    {
        audin->enabled = enabled != 0;
    }
}

int
ohos_audin_connect(struct ohos_audin *audin)
{
    if (audin == 0)
    {
        return 1;
    }
    audin->connected = 1;
    return ohos_audin_start(audin);
}

void
ohos_audin_disconnect(struct ohos_audin *audin, const char *reason)
{
    if (audin == 0)
    {
        return;
    }
    audin->connected = 0;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.audin: disconnect reason=%s opens=%llu packets=%llu bytes=%llu dropped=%llu errors=%llu",
        reason == 0 ? "" : reason,
        (unsigned long long)audin->open_count,
        (unsigned long long)audin->data_count,
        (unsigned long long)audin->data_bytes,
        (unsigned long long)audin->dropped_bytes,
        (unsigned long long)audin->error_count);
    if (audin->channel_id >= 0 && audin->mod != 0 &&
            audin->mod->server_drdynvc_close != 0)
    {
        (void)audin->mod->server_drdynvc_close(
            audin->mod, audin->channel_id);
    }
    audin->channel_id = -1;
    audin->channel_open = 0;
    free_stream(audin->fragment);
    audin->fragment = 0;
    ohos_audin_renderer_close(audin->renderer);
}

int
ohos_audin_drdynvc_ready(struct ohos_audin *audin)
{
    if (audin == 0)
    {
        return 1;
    }
    audin->dvc_ready = 1;
    return ohos_audin_start(audin);
}
