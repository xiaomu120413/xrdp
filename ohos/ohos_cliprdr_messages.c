/*
 * Outgoing [MS-RDPECLIP] messages for the xrdp OHOS cliprdr backend.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "log.h"
#include "ms-rdpeclip.h"
#include "os_calls.h"
#include "parse.h"
#include "string_calls.h"
#include "xrdp_constants.h"

int
ohos_cliprdr_send_capabilities(struct ohos_cliprdr *cliprdr)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 128);
    if (s->data == 0)
    {
        free_stream(s);
        return 1;
    }

    ohos_cliprdr_out_header(s, CB_CLIP_CAPS, 0);
    out_uint16_le(s, 1);
    out_uint16_le(s, 0);
    out_uint16_le(s, CB_CAPSTYPE_GENERAL);
    out_uint16_le(s, 12);
    out_uint32_le(s, CB_CAPS_VERSION_2);
    out_uint32_le(s, cliprdr->capability_flags);
    s_mark_end(s);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    return rv;
}

int
ohos_cliprdr_send_monitor_ready(struct ohos_cliprdr *cliprdr)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64);
    if (s->data == 0)
    {
        free_stream(s);
        return 1;
    }

    ohos_cliprdr_out_header(s, CB_MONITOR_READY, 0);
    s_mark_end(s);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    return rv;
}

static int
ohos_cliprdr_send_format_list_response(struct ohos_cliprdr *cliprdr,
                                       int accepted)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64);
    if (s->data == 0)
    {
        free_stream(s);
        return 1;
    }
    ohos_cliprdr_out_header(s, CB_FORMAT_LIST_RESPONSE,
                            accepted ? CB_RESPONSE_OK : CB_RESPONSE_FAIL);
    s_mark_end(s);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    return rv;
}

int
ohos_cliprdr_send_format_data_request(struct ohos_cliprdr *cliprdr,
                                      int format_id)
{
    struct stream *s;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 64);
    if (s->data == 0)
    {
        free_stream(s);
        return 1;
    }
    ohos_cliprdr_out_header(s, CB_FORMAT_DATA_REQUEST, 0);
    out_uint32_le(s, format_id);
    s_mark_end(s);

    cliprdr->requested_format = format_id;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: requesting remote clipboard data format=%d",
        format_id);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    return rv;
}

int
ohos_cliprdr_send_format_data_response(struct ohos_cliprdr *cliprdr,
                                       int format_id, const char *text)
{
    struct stream *s;
    int rv;
    int data_ok = (text != 0);
    int text_len = data_ok ? g_strlen(text) : 0;
    int stream_size = 64;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    if (data_ok)
    {
        if (format_id == CF_TEXT || format_id == CF_OEMTEXT)
        {
            stream_size += text_len + 1;
        }
        else
        {
            stream_size +=
                (int)utf8_as_utf16_word_count(text, text_len) * 2 + 2;
        }
    }
    init_stream(s, stream_size);
    if (s->data == 0)
    {
        free_stream(s);
        return 1;
    }
    ohos_cliprdr_out_header(s, CB_FORMAT_DATA_RESPONSE,
                            data_ok ? CB_RESPONSE_OK : CB_RESPONSE_FAIL);
    if (data_ok)
    {
        if (format_id == CF_TEXT || format_id == CF_OEMTEXT)
        {
            out_uint8p(s, text, text_len + 1);
        }
        else
        {
            out_utf8_as_utf16_le(s, text, text_len);
            out_uint16_le(s, 0);
        }
    }
    s_mark_end(s);

    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    return rv;
}

int
ohos_cliprdr_send_local_format_list(struct ohos_cliprdr *cliprdr,
                                    const char *reason, int allow_empty)
{
    struct stream *s;
    char *text = 0;
    int has_text;
    int rv;

    if (cliprdr == 0 || !cliprdr->connected || !cliprdr->channel_ready)
    {
        return 0;
    }

    has_text = (ohos_cliprdr_pasteboard_read_plain_text(cliprdr, &text) == 0);
    if (!has_text && !allow_empty)
    {
        return 0;
    }

    make_stream(s);
    if (s == 0)
    {
        g_free(text);
        return 1;
    }
    init_stream(s, 256);
    if (s->data == 0)
    {
        free_stream(s);
        g_free(text);
        return 1;
    }

    ohos_cliprdr_out_header(s, CB_FORMAT_LIST, 0);
    if (has_text)
    {
        out_uint32_le(s, CF_UNICODETEXT);
        out_uint16_le(s, 0);
    }
    s_mark_end(s);

    rv = ohos_cliprdr_send_stream(cliprdr, s);
    if (rv == 0)
    {
        cliprdr->local_format_lists_sent++;
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: sent local format list has_text=%d reason=%s",
            has_text, reason == 0 ? "" : reason);
    }
    else
    {
        cliprdr->errors++;
    }

    free_stream(s);
    g_free(text);
    return rv;
}

int
ohos_cliprdr_send_format_list_ok(struct ohos_cliprdr *cliprdr)
{
    return ohos_cliprdr_send_format_list_response(cliprdr, 1);
}
