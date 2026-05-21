/*
 * Incoming [MS-RDPECLIP] protocol handling for the xrdp OHOS backend.
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

static char *
ohos_cliprdr_utf16le_to_utf8(const char *data, int bytes)
{
    struct stream s = { 0 };
    unsigned int text_bytes;
    char *text;

    if (data == 0 || bytes <= 0)
    {
        return 0;
    }

    s.data = (char *)data;
    s.p = (char *)data;
    s.end = (char *)data + bytes;
    s.size = bytes;
    text_bytes = in_utf16_le_terminated_as_utf8_length(&s);
    if (text_bytes == 0)
    {
        return 0;
    }

    text = (char *)g_malloc(text_bytes, 1);
    if (text == 0)
    {
        return 0;
    }

    s.p = s.data;
    (void)in_utf16_le_terminated_as_utf8(&s, text, text_bytes);
    return text;
}

static char *
ohos_cliprdr_bytes_to_text(const char *data, int bytes)
{
    int len;

    if (data == 0 || bytes <= 0)
    {
        return 0;
    }

    for (len = 0; len < bytes && data[len] != '\0'; len++)
    {
    }
    if (len <= 0)
    {
        return 0;
    }

    return g_strndup(data, len);
}

static int
ohos_cliprdr_process_format_list(struct ohos_cliprdr *cliprdr,
                                 int msg_flags, struct stream *s)
{
    int requested = 0;
    int seen_text = 0;
    int format_id;

    while (s_check_rem(s, 4))
    {
        in_uint32_le(s, format_id);

        if ((cliprdr->capability_flags & CB_USE_LONG_FORMAT_NAMES) != 0 &&
                ((msg_flags & CB_ASCII_NAMES) == 0))
        {
            int wchar = 1;
            while (s_check_rem(s, 2) && wchar != 0)
            {
                in_uint16_le(s, wchar);
            }
        }
        else
        {
            int skip = s_rem(s) < 32 ? s_rem(s) : 32;
            in_uint8s(s, skip);
        }

        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote format id=%d", format_id);
        if (format_id == CF_UNICODETEXT)
        {
            requested = CF_UNICODETEXT;
            seen_text = 1;
        }
        else if (!seen_text &&
                 (format_id == CF_TEXT || format_id == CF_OEMTEXT))
        {
            requested = format_id;
        }
    }

    cliprdr->remote_format_lists_received++;
    (void)ohos_cliprdr_send_format_list_ok(cliprdr);
    if (requested != 0)
    {
        return ohos_cliprdr_send_format_data_request(cliprdr, requested);
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote format list has no supported text format");
    return 0;
}

static int
ohos_cliprdr_process_format_data_request(struct ohos_cliprdr *cliprdr,
                                         struct stream *s)
{
    int format_id = 0;
    char *text = 0;
    int has_text;
    int rv;

    if (!s_check_rem_and_log(s, 4, "OHOS cliprdr data request"))
    {
        return 1;
    }
    in_uint32_le(s, format_id);
    has_text = (format_id == CF_UNICODETEXT ||
                format_id == CF_TEXT ||
                format_id == CF_OEMTEXT) &&
               (ohos_cliprdr_pasteboard_read_plain_text(cliprdr, &text) == 0);

    cliprdr->local_requests_received++;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote requested local format=%d has_text=%d",
        format_id, has_text);
    rv = ohos_cliprdr_send_format_data_response(cliprdr, format_id,
                                                has_text ? text : 0);
    g_free(text);
    return rv;
}

static int
ohos_cliprdr_process_format_data_response(struct ohos_cliprdr *cliprdr,
                                          int msg_flags, struct stream *s,
                                          int data_len)
{
    char *text = 0;
    int format_id = cliprdr->requested_format;
    int rv = 0;

    cliprdr->requested_format = 0;
    if ((msg_flags & CB_RESPONSE_FAIL) != 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: remote data response failed format=%d",
            format_id);
        return 0;
    }
    if (data_len <= 0 || !s_check_rem_and_log(s, data_len,
                                              "OHOS cliprdr data response"))
    {
        return 1;
    }

    if (format_id == CF_UNICODETEXT)
    {
        text = ohos_cliprdr_utf16le_to_utf8(s->p, data_len);
    }
    else if (format_id == CF_TEXT || format_id == CF_OEMTEXT)
    {
        text = ohos_cliprdr_bytes_to_text(s->p, data_len);
    }
    else
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: ignored response for unsupported format=%d",
            format_id);
        return 0;
    }

    if (text == 0 || text[0] == '\0')
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: remote text response was empty");
        g_free(text);
        return 0;
    }

    rv = ohos_cliprdr_pasteboard_write_plain_text(cliprdr, text);
    if (rv == 0)
    {
        cliprdr->remote_responses_received++;
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: copied remote text to Pasteboard bytes=%d",
            (int)g_strlen(text));
    }
    g_free(text);
    return rv;
}

static int
ohos_cliprdr_process_caps(struct ohos_cliprdr *cliprdr, struct stream *s)
{
    int cap_count;
    int pad;
    int index;

    if (!s_check_rem_and_log(s, 4, "OHOS cliprdr caps"))
    {
        return 1;
    }
    in_uint16_le(s, cap_count);
    in_uint16_le(s, pad);
    (void)pad;

    for (index = 0; index < cap_count && s_check_rem(s, 4); index++)
    {
        int cap_type;
        int cap_len;
        char *cap_end;

        in_uint16_le(s, cap_type);
        in_uint16_le(s, cap_len);
        if (cap_len < 4 || cap_len - 4 > s_rem(s))
        {
            return 1;
        }
        cap_end = s->p + cap_len - 4;
        if (cap_type == CB_CAPSTYPE_GENERAL && cap_len >= 12 &&
                s_check_rem(s, 8))
        {
            int version;
            int flags;

            in_uint32_le(s, version);
            in_uint32_le(s, flags);
            cliprdr->remote_capability_flags = flags;
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.cliprdr: remote caps version=%d flags=0x%8.8x",
                version, flags);
        }
        s->p = cap_end;
    }
    return 0;
}

int
ohos_cliprdr_process_pdu(struct ohos_cliprdr *cliprdr, struct stream *s)
{
    int type;
    int msg_flags;
    int data_len;
    char *payload_end;
    int rv = 0;

    if (!s_check_rem_and_log(s, 8, "OHOS cliprdr PDU header"))
    {
        return 1;
    }
    in_uint16_le(s, type);
    in_uint16_le(s, msg_flags);
    in_uint32_le(s, data_len);
    if (data_len < 0 || data_len > s_rem(s))
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: invalid PDU type=%d data_len=%d remaining=%d",
            type, data_len, s_rem(s));
        cliprdr->errors++;
        return 1;
    }
    payload_end = s->p + data_len;

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: received PDU type=%s flags=0x%4.4x len=%d",
        CB_PDUTYPE_TO_STR(type), msg_flags, data_len);
    switch (type)
    {
        case CB_CLIP_CAPS:
            rv = ohos_cliprdr_process_caps(cliprdr, s);
            break;
        case CB_FORMAT_LIST:
            rv = ohos_cliprdr_process_format_list(cliprdr, msg_flags, s);
            break;
        case CB_FORMAT_LIST_RESPONSE:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cliprdr: format list response flags=0x%4.4x",
                msg_flags);
            break;
        case CB_FORMAT_DATA_REQUEST:
            rv = ohos_cliprdr_process_format_data_request(cliprdr, s);
            break;
        case CB_FORMAT_DATA_RESPONSE:
            rv = ohos_cliprdr_process_format_data_response(cliprdr,
                                                           msg_flags, s,
                                                           data_len);
            break;
        case CB_TEMP_DIRECTORY:
        case CB_LOCK_CLIPDATA:
        case CB_UNLOCK_CLIPDATA:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cliprdr: ignored optional PDU type=%s",
                CB_PDUTYPE_TO_STR(type));
            break;
        default:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cliprdr: ignored unsupported PDU type=%d", type);
            break;
    }

    s->p = payload_end;
    return rv;
}
