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
                                      int format_id, int request_kind)
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
    cliprdr->requested_kind = request_kind;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: requesting remote clipboard data format=%d(%s) kind=%s(%d)",
        format_id, ohos_cliprdr_format_display_name(format_id),
        ohos_cliprdr_request_kind_name(request_kind), request_kind);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    return rv;
}

int
ohos_cliprdr_send_format_data_response(struct ohos_cliprdr *cliprdr,
                                       const char *data, int bytes)
{
    struct stream *s;
    int rv;
    int data_ok = (data != 0 && bytes > 0);
    int stream_size = 64 + (data_ok ? bytes : 0);

    make_stream(s);
    if (s == 0)
    {
        return 1;
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
        out_uint8p(s, data, bytes);
    }
    s_mark_end(s);

    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    return rv;
}

static void
ohos_cliprdr_out_format(struct stream *s, int format_id, const char *name)
{
    out_uint32_le(s, format_id);
    if (name != 0 && name[0] != '\0')
    {
        out_utf8_as_utf16_le(s, name, g_strlen(name));
        out_uint16_le(s, 0);
    }
    else
    {
        out_uint16_le(s, 0);
    }
}

int
ohos_cliprdr_send_local_format_list(struct ohos_cliprdr *cliprdr,
                                    const char *reason, int allow_empty)
{
    struct stream *s;
    char *text = 0;
    char *html = 0;
    char *uri = 0;
    int has_text;
    int has_html;
    int has_uri;
    int has_image;
    int has_file;
    int image_format = 0;
    int rv;

    if (cliprdr == 0 || !cliprdr->connected || !cliprdr->channel_ready)
    {
        return 0;
    }

    has_text = (ohos_cliprdr_pasteboard_read_plain_text(cliprdr, &text) == 0);
    has_html = (ohos_cliprdr_pasteboard_read_html(cliprdr, &html, 0) == 0);
    has_uri = (ohos_cliprdr_pasteboard_read_uri(cliprdr, &uri) == 0);
    has_image = ohos_cliprdr_has_local_image(cliprdr, &image_format);
    has_file = ohos_cliprdr_has_local_file(cliprdr);
    if (!has_text && !has_html && !has_uri && !has_image && !has_file &&
            !allow_empty)
    {
        return 0;
    }

    make_stream(s);
    if (s == 0)
    {
        g_free(text);
        g_free(html);
        g_free(uri);
        return 1;
    }
    init_stream(s, 1024);
    if (s->data == 0)
    {
        free_stream(s);
        g_free(text);
        g_free(html);
        g_free(uri);
        return 1;
    }

    ohos_cliprdr_out_header(s, CB_FORMAT_LIST, 0);
    if (has_text || has_html || has_uri)
    {
        ohos_cliprdr_out_format(s, CF_UNICODETEXT, 0);
    }
    if (has_html)
    {
        ohos_cliprdr_out_format(s, OHOS_CLIPRDR_FORMAT_HTML,
                                ohos_cliprdr_format_name(OHOS_CLIPRDR_FORMAT_HTML));
    }
    if (has_uri)
    {
        ohos_cliprdr_out_format(s, OHOS_CLIPRDR_FORMAT_URIW,
                                ohos_cliprdr_format_name(OHOS_CLIPRDR_FORMAT_URIW));
        ohos_cliprdr_out_format(s, OHOS_CLIPRDR_FORMAT_URI_LIST,
                                ohos_cliprdr_format_name(OHOS_CLIPRDR_FORMAT_URI_LIST));
    }
    if (has_image)
    {
        ohos_cliprdr_out_format(s, CF_DIB, 0);
        if (image_format != 0)
        {
            ohos_cliprdr_out_format(s, image_format,
                                    ohos_cliprdr_format_name(image_format));
        }
    }
    if (has_file)
    {
        ohos_cliprdr_out_format(s,
                                OHOS_CLIPRDR_FORMAT_FILE_GROUP_DESCRIPTOR,
                                ohos_cliprdr_format_name(OHOS_CLIPRDR_FORMAT_FILE_GROUP_DESCRIPTOR));
        ohos_cliprdr_out_format(s, OHOS_CLIPRDR_FORMAT_FILE_CONTENTS,
                                ohos_cliprdr_format_name(OHOS_CLIPRDR_FORMAT_FILE_CONTENTS));
    }
    s_mark_end(s);

    rv = ohos_cliprdr_send_stream(cliprdr, s);
    if (rv == 0)
    {
        cliprdr->local_format_lists_sent++;
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: sent local formats text=%d html=%d uri=%d image=%d image-format=%d(%s) file=%d reason=%s",
            has_text, has_html, has_uri, has_image, image_format,
            ohos_cliprdr_format_display_name(image_format),
            has_file, reason == 0 ? "" : reason);
    }
    else
    {
        cliprdr->errors++;
    }

    free_stream(s);
    g_free(text);
    g_free(html);
    g_free(uri);
    return rv;
}

int
ohos_cliprdr_send_format_list_ok(struct ohos_cliprdr *cliprdr)
{
    return ohos_cliprdr_send_format_list_response(cliprdr, 1);
}
