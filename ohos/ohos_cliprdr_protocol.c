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

static int
ohos_cliprdr_name_is_bmp(const char *name)
{
    return name != 0 &&
           (ohos_cliprdr_name_is(name, OHOS_CLIPRDR_FORMAT_IMAGE_BMP) ||
            ohos_cliprdr_strcasecmp(name, "Bitmap") == 0 ||
            ohos_cliprdr_strcasecmp(name, "DIB") == 0 ||
            ohos_cliprdr_strcasecmp(name, "DeviceIndependentBitmap") == 0);
}

static int
ohos_cliprdr_name_is_png(const char *name)
{
    return name != 0 &&
           (ohos_cliprdr_name_is(name, OHOS_CLIPRDR_FORMAT_IMAGE_PNG) ||
            ohos_cliprdr_strcasecmp(name, "PNG") == 0 ||
            ohos_cliprdr_strcasecmp(name, "image/x-png") == 0);
}

static int
ohos_cliprdr_name_is_jpeg(const char *name)
{
    return name != 0 &&
           (ohos_cliprdr_name_is(name, OHOS_CLIPRDR_FORMAT_IMAGE_JPEG) ||
            ohos_cliprdr_strcasecmp(name, "JPEG") == 0 ||
            ohos_cliprdr_strcasecmp(name, "JPG") == 0 ||
            ohos_cliprdr_strcasecmp(name, "JFIF") == 0 ||
            ohos_cliprdr_strcasecmp(name, "image/jpg") == 0);
}

static int
ohos_cliprdr_name_is_webp(const char *name)
{
    return name != 0 &&
           (ohos_cliprdr_name_is(name, OHOS_CLIPRDR_FORMAT_IMAGE_WEBP) ||
            ohos_cliprdr_strcasecmp(name, "WEBP") == 0);
}

static int
ohos_cliprdr_request_kind_is_image(int request_kind)
{
    return request_kind == OHOS_CLIPRDR_REQUEST_DIB ||
           request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_BMP ||
           request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_PNG ||
           request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_JPEG ||
           request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_WEBP;
}

static int
ohos_cliprdr_set_fallback(int candidate_format, int candidate_kind,
                          int failed_format, int *format_id,
                          int *request_kind)
{
    if (candidate_format != 0 &&
            candidate_kind != OHOS_CLIPRDR_REQUEST_NONE &&
            candidate_format != failed_format)
    {
        *format_id = candidate_format;
        *request_kind = candidate_kind;
        return 1;
    }
    return 0;
}

static int
ohos_cliprdr_choose_image_fallback(struct ohos_cliprdr *cliprdr,
                                   int failed_format, int *format_id,
                                   int *request_kind)
{
    return ohos_cliprdr_set_fallback(cliprdr->remote_image_png_format,
                                     OHOS_CLIPRDR_REQUEST_IMAGE_PNG,
                                     failed_format, format_id,
                                     request_kind) ||
           ohos_cliprdr_set_fallback(cliprdr->remote_image_jpeg_format,
                                     OHOS_CLIPRDR_REQUEST_IMAGE_JPEG,
                                     failed_format, format_id,
                                     request_kind) ||
           ohos_cliprdr_set_fallback(cliprdr->remote_image_webp_format,
                                     OHOS_CLIPRDR_REQUEST_IMAGE_WEBP,
                                     failed_format, format_id,
                                     request_kind) ||
           ohos_cliprdr_set_fallback(cliprdr->remote_dibv5_format,
                                     OHOS_CLIPRDR_REQUEST_DIB,
                                     failed_format, format_id,
                                     request_kind) ||
           ohos_cliprdr_set_fallback(cliprdr->remote_dib_format,
                                     OHOS_CLIPRDR_REQUEST_DIB,
                                     failed_format, format_id,
                                     request_kind) ||
           ohos_cliprdr_set_fallback(cliprdr->remote_image_bmp_format,
                                     OHOS_CLIPRDR_REQUEST_IMAGE_BMP,
                                     failed_format, format_id,
                                     request_kind);
}

static int
ohos_cliprdr_queue_or_send_remote_request(struct ohos_cliprdr *cliprdr,
                                          int format_id, int request_kind,
                                          const char *reason)
{
    if (format_id == 0 || request_kind == OHOS_CLIPRDR_REQUEST_NONE)
    {
        return 0;
    }

    if (cliprdr->requested_kind != OHOS_CLIPRDR_REQUEST_NONE)
    {
        cliprdr->pending_remote_format = format_id;
        cliprdr->pending_remote_kind = request_kind;
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: deferred remote data request format=%d(%s) kind=%s(%d) while in-flight=%d(%s) kind=%s(%d) reason=%s",
            format_id, ohos_cliprdr_format_display_name(format_id),
            ohos_cliprdr_request_kind_name(request_kind), request_kind,
            cliprdr->requested_format,
            ohos_cliprdr_format_display_name(cliprdr->requested_format),
            ohos_cliprdr_request_kind_name(cliprdr->requested_kind),
            cliprdr->requested_kind, reason == 0 ? "" : reason);
        return 0;
    }

    return ohos_cliprdr_send_format_data_request(cliprdr, format_id,
                                                 request_kind);
}

static int
ohos_cliprdr_request_pending_remote_data(struct ohos_cliprdr *cliprdr)
{
    int format_id = cliprdr->pending_remote_format;
    int request_kind = cliprdr->pending_remote_kind;

    if (format_id == 0 || request_kind == OHOS_CLIPRDR_REQUEST_NONE)
    {
        return 0;
    }

    cliprdr->pending_remote_format = 0;
    cliprdr->pending_remote_kind = OHOS_CLIPRDR_REQUEST_NONE;
    return ohos_cliprdr_queue_or_send_remote_request(cliprdr, format_id,
                                                     request_kind,
                                                     "pending format list");
}

static int
ohos_cliprdr_request_remote_fallback(struct ohos_cliprdr *cliprdr,
                                     int failed_format, int failed_kind,
                                     int *requested)
{
    int format_id = 0;
    int request_kind = OHOS_CLIPRDR_REQUEST_NONE;

    *requested = 0;
    if (failed_kind == OHOS_CLIPRDR_REQUEST_HTML)
    {
        if (!ohos_cliprdr_set_fallback(cliprdr->remote_text_format,
                                       OHOS_CLIPRDR_REQUEST_TEXT,
                                       failed_format, &format_id,
                                       &request_kind))
        {
            if (!ohos_cliprdr_set_fallback(cliprdr->remote_uriw_format,
                                           OHOS_CLIPRDR_REQUEST_URIW,
                                           failed_format, &format_id,
                                           &request_kind))
            {
                (void)ohos_cliprdr_set_fallback(cliprdr->remote_uri_list_format,
                                                OHOS_CLIPRDR_REQUEST_URI_LIST,
                                                failed_format, &format_id,
                                                &request_kind);
            }
        }
    }
    else if (failed_kind == OHOS_CLIPRDR_REQUEST_TEXT)
    {
        if (!ohos_cliprdr_set_fallback(cliprdr->remote_html_format,
                                       OHOS_CLIPRDR_REQUEST_HTML,
                                       failed_format, &format_id,
                                       &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_uriw_format,
                                           OHOS_CLIPRDR_REQUEST_URIW,
                                           failed_format, &format_id,
                                           &request_kind))
        {
            (void)ohos_cliprdr_set_fallback(cliprdr->remote_uri_list_format,
                                            OHOS_CLIPRDR_REQUEST_URI_LIST,
                                            failed_format, &format_id,
                                            &request_kind);
        }
    }
    else if (failed_kind == OHOS_CLIPRDR_REQUEST_URIW)
    {
        if (!ohos_cliprdr_set_fallback(cliprdr->remote_uri_list_format,
                                       OHOS_CLIPRDR_REQUEST_URI_LIST,
                                       failed_format, &format_id,
                                       &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_text_format,
                                           OHOS_CLIPRDR_REQUEST_TEXT,
                                           failed_format, &format_id,
                                           &request_kind))
        {
            (void)ohos_cliprdr_set_fallback(cliprdr->remote_html_format,
                                            OHOS_CLIPRDR_REQUEST_HTML,
                                            failed_format, &format_id,
                                            &request_kind);
        }
    }
    else if (failed_kind == OHOS_CLIPRDR_REQUEST_URI_LIST)
    {
        if (!ohos_cliprdr_set_fallback(cliprdr->remote_uriw_format,
                                       OHOS_CLIPRDR_REQUEST_URIW,
                                       failed_format, &format_id,
                                       &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_text_format,
                                           OHOS_CLIPRDR_REQUEST_TEXT,
                                           failed_format, &format_id,
                                           &request_kind))
        {
            (void)ohos_cliprdr_set_fallback(cliprdr->remote_html_format,
                                            OHOS_CLIPRDR_REQUEST_HTML,
                                            failed_format, &format_id,
                                            &request_kind);
        }
    }
    else if (failed_kind == OHOS_CLIPRDR_REQUEST_FILE_GROUP_DESCRIPTOR)
    {
        if (!ohos_cliprdr_choose_image_fallback(cliprdr, failed_format,
                                                &format_id, &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_text_format,
                                           OHOS_CLIPRDR_REQUEST_TEXT,
                                           failed_format, &format_id,
                                           &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_html_format,
                                           OHOS_CLIPRDR_REQUEST_HTML,
                                           failed_format, &format_id,
                                           &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_uriw_format,
                                           OHOS_CLIPRDR_REQUEST_URIW,
                                           failed_format, &format_id,
                                           &request_kind))
        {
            (void)ohos_cliprdr_set_fallback(cliprdr->remote_uri_list_format,
                                            OHOS_CLIPRDR_REQUEST_URI_LIST,
                                            failed_format, &format_id,
                                            &request_kind);
        }
    }
    else if (ohos_cliprdr_request_kind_is_image(failed_kind))
    {
        if (!ohos_cliprdr_choose_image_fallback(cliprdr, failed_format,
                                                &format_id, &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_text_format,
                                           OHOS_CLIPRDR_REQUEST_TEXT,
                                           failed_format, &format_id,
                                           &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_html_format,
                                           OHOS_CLIPRDR_REQUEST_HTML,
                                           failed_format, &format_id,
                                           &request_kind) &&
                !ohos_cliprdr_set_fallback(cliprdr->remote_uriw_format,
                                           OHOS_CLIPRDR_REQUEST_URIW,
                                           failed_format, &format_id,
                                           &request_kind))
        {
            (void)ohos_cliprdr_set_fallback(cliprdr->remote_uri_list_format,
                                            OHOS_CLIPRDR_REQUEST_URI_LIST,
                                            failed_format, &format_id,
                                            &request_kind);
        }
    }
    if (format_id == 0 && !ohos_cliprdr_request_kind_is_image(failed_kind))
    {
        (void)ohos_cliprdr_choose_image_fallback(cliprdr, failed_format,
                                                 &format_id, &request_kind);
    }

    if (format_id == 0 || request_kind == OHOS_CLIPRDR_REQUEST_NONE)
    {
        return 0;
    }

    *requested = 1;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: fallback remote data request failed-format=%d(%s) failed-kind=%s(%d) next=%d(%s) next-kind=%s(%d)",
        failed_format, ohos_cliprdr_format_display_name(failed_format),
        ohos_cliprdr_request_kind_name(failed_kind), failed_kind,
        format_id, ohos_cliprdr_format_display_name(format_id),
        ohos_cliprdr_request_kind_name(request_kind), request_kind);
    return ohos_cliprdr_queue_or_send_remote_request(cliprdr, format_id,
                                                     request_kind,
                                                     "fallback");
}

static int
ohos_cliprdr_complete_remote_response(struct ohos_cliprdr *cliprdr,
                                      int format_id, int request_kind,
                                      int rv, int wrote)
{
    int fallback_requested = 0;
    int fallback_rv;
    int pending_rv;

    if ((rv != 0 || !wrote) &&
            request_kind != OHOS_CLIPRDR_REQUEST_NONE)
    {
        fallback_rv = ohos_cliprdr_request_remote_fallback(cliprdr,
                                                           format_id,
                                                           request_kind,
                                                           &fallback_requested);
        if (fallback_rv != 0 || fallback_requested)
        {
            return fallback_rv;
        }
    }

    pending_rv = ohos_cliprdr_request_pending_remote_data(cliprdr);
    return rv == 0 ? pending_rv : rv;
}

static int
ohos_cliprdr_process_format_list(struct ohos_cliprdr *cliprdr,
                                 int msg_flags, struct stream *s)
{
    int requested = 0;
    int requested_kind = OHOS_CLIPRDR_REQUEST_NONE;
    int text_format = 0;
    int image_format = 0;
    int image_kind = OHOS_CLIPRDR_REQUEST_NONE;
    int image_priority = 0;
    int format_count = 0;
    int format_id;

    cliprdr->remote_text_format = 0;
    cliprdr->remote_html_format = 0;
    cliprdr->remote_uriw_format = 0;
    cliprdr->remote_uri_list_format = 0;
    cliprdr->remote_dib_format = 0;
    cliprdr->remote_dibv5_format = 0;
    cliprdr->remote_image_bmp_format = 0;
    cliprdr->remote_image_png_format = 0;
    cliprdr->remote_image_jpeg_format = 0;
    cliprdr->remote_image_webp_format = 0;
    cliprdr->remote_file_group_descriptor_format = 0;
    cliprdr->remote_file_contents_format = 0;
    ohos_cliprdr_file_transfer_reset(cliprdr);

    while (s_check_rem(s, 4))
    {
        char *name;

        in_uint32_le(s, format_id);
        name = ohos_cliprdr_read_format_name(s, msg_flags);
        format_count++;

        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: remote format candidate index=%d id=%d canonical=%s name=%s",
            format_count, format_id,
            ohos_cliprdr_format_display_name(format_id),
            name == 0 ? "" : name);

        if (name != 0 &&
                (ohos_cliprdr_name_is(name, OHOS_CLIPRDR_FORMAT_HTML) ||
                 ohos_cliprdr_strcasecmp(name, "text/html") == 0))
        {
            cliprdr->remote_html_format = format_id;
        }
        else if (name != 0 &&
                 ohos_cliprdr_name_is(name, OHOS_CLIPRDR_FORMAT_URIW))
        {
            cliprdr->remote_uriw_format = format_id;
        }
        else if (name != 0 &&
                 (ohos_cliprdr_name_is(name, OHOS_CLIPRDR_FORMAT_URI_LIST) ||
                  ohos_cliprdr_strcasecmp(name, "UniformResourceLocator") == 0))
        {
            cliprdr->remote_uri_list_format = format_id;
        }
        else if (ohos_cliprdr_name_is_bmp(name))
        {
            cliprdr->remote_image_bmp_format = format_id;
            if (image_priority < 60)
            {
                image_format = format_id;
                image_kind = OHOS_CLIPRDR_REQUEST_IMAGE_BMP;
                image_priority = 60;
            }
        }
        else if (ohos_cliprdr_name_is_png(name))
        {
            cliprdr->remote_image_png_format = format_id;
            if (image_priority < 100)
            {
                image_format = format_id;
                image_kind = OHOS_CLIPRDR_REQUEST_IMAGE_PNG;
                image_priority = 100;
            }
        }
        else if (ohos_cliprdr_name_is_jpeg(name))
        {
            cliprdr->remote_image_jpeg_format = format_id;
            if (image_priority < 100)
            {
                image_format = format_id;
                image_kind = OHOS_CLIPRDR_REQUEST_IMAGE_JPEG;
                image_priority = 100;
            }
        }
        else if (ohos_cliprdr_name_is_webp(name))
        {
            cliprdr->remote_image_webp_format = format_id;
            if (image_priority < 100)
            {
                image_format = format_id;
                image_kind = OHOS_CLIPRDR_REQUEST_IMAGE_WEBP;
                image_priority = 100;
            }
        }
        else if (name != 0 &&
                 ohos_cliprdr_name_is(name,
                                      OHOS_CLIPRDR_FORMAT_FILE_GROUP_DESCRIPTOR))
        {
            cliprdr->remote_file_group_descriptor_format = format_id;
        }
        else if (name != 0 &&
                 ohos_cliprdr_name_is(name,
                                      OHOS_CLIPRDR_FORMAT_FILE_CONTENTS))
        {
            cliprdr->remote_file_contents_format = format_id;
        }
        else if (format_id == CF_DIB)
        {
            cliprdr->remote_dib_format = format_id;
            if (image_priority < 80)
            {
                image_format = format_id;
                image_kind = OHOS_CLIPRDR_REQUEST_DIB;
                image_priority = 80;
            }
        }
        else if (format_id == CF_DIBV5)
        {
            cliprdr->remote_dibv5_format = format_id;
            if (image_priority < 70)
            {
                image_format = format_id;
                image_kind = OHOS_CLIPRDR_REQUEST_DIB;
                image_priority = 70;
            }
        }
        else if (format_id == CF_UNICODETEXT)
        {
            text_format = CF_UNICODETEXT;
        }
        else if (text_format == 0 &&
                 (format_id == CF_TEXT || format_id == CF_OEMTEXT))
        {
            text_format = format_id;
        }
        g_free(name);
    }

    cliprdr->remote_format_lists_received++;
    cliprdr->remote_text_format = text_format;
    (void)ohos_cliprdr_send_format_list_ok(cliprdr);
    if (cliprdr->remote_file_group_descriptor_format != 0)
    {
        requested = cliprdr->remote_file_group_descriptor_format;
        requested_kind = OHOS_CLIPRDR_REQUEST_FILE_GROUP_DESCRIPTOR;
    }
    else if (image_format != 0)
    {
        requested = image_format;
        requested_kind = image_kind;
    }
    else if (text_format != 0)
    {
        requested = text_format;
        requested_kind = OHOS_CLIPRDR_REQUEST_TEXT;
    }
    else if (cliprdr->remote_html_format != 0)
    {
        requested = cliprdr->remote_html_format;
        requested_kind = OHOS_CLIPRDR_REQUEST_HTML;
    }
    else if (cliprdr->remote_uriw_format != 0)
    {
        requested = cliprdr->remote_uriw_format;
        requested_kind = OHOS_CLIPRDR_REQUEST_URIW;
    }
    else if (cliprdr->remote_uri_list_format != 0)
    {
        requested = cliprdr->remote_uri_list_format;
        requested_kind = OHOS_CLIPRDR_REQUEST_URI_LIST;
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote format summary count=%d text=%d html=%d uriw=%d uri-list=%d image-dib=%d image-bmp=%d image-png=%d image-jpeg=%d image-webp=%d file-desc=%d file-contents=%d selected=%d(%s) kind=%s(%d)",
        format_count, text_format, cliprdr->remote_html_format,
        cliprdr->remote_uriw_format, cliprdr->remote_uri_list_format,
        (image_format == CF_DIB || image_format == CF_DIBV5) ? image_format : 0,
        cliprdr->remote_image_bmp_format, cliprdr->remote_image_png_format,
        cliprdr->remote_image_jpeg_format, cliprdr->remote_image_webp_format,
        cliprdr->remote_file_group_descriptor_format,
        cliprdr->remote_file_contents_format,
        requested, ohos_cliprdr_format_display_name(requested),
        ohos_cliprdr_request_kind_name(requested_kind), requested_kind);
    if (requested != 0)
    {
        return ohos_cliprdr_queue_or_send_remote_request(cliprdr, requested,
                                                         requested_kind,
                                                         "format list");
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote format list has no supported format");
    return 0;
}

static int
ohos_cliprdr_process_format_data_request(struct ohos_cliprdr *cliprdr,
                                         struct stream *s)
{
    int format_id = 0;
    char *text = 0;
    char *html = 0;
    char *plain = 0;
    char *uri = 0;
    char *data = 0;
    int bytes = 0;
    int ok = 0;
    int rv;

    if (!s_check_rem_and_log(s, 4, "OHOS cliprdr data request"))
    {
        return 1;
    }
    in_uint32_le(s, format_id);

    cliprdr->local_requests_received++;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote requested local data format=%d(%s)",
        format_id, ohos_cliprdr_format_display_name(format_id));
    if (format_id == OHOS_CLIPRDR_FORMAT_HTML)
    {
        ok = ohos_cliprdr_pasteboard_read_html(cliprdr, &html, &plain) == 0;
        if (ok)
        {
            data = ohos_cliprdr_html_to_ms_html(html, &bytes);
        }
    }
    else if (format_id == OHOS_CLIPRDR_FORMAT_URIW)
    {
        ok = ohos_cliprdr_pasteboard_read_uri(cliprdr, &uri) == 0;
        if (ok)
        {
            data = ohos_cliprdr_utf8_to_utf16le(uri, &bytes);
        }
    }
    else if (format_id == OHOS_CLIPRDR_FORMAT_URI_LIST)
    {
        ok = ohos_cliprdr_pasteboard_read_uri(cliprdr, &uri) == 0;
        if (ok)
        {
            data = ohos_cliprdr_uri_to_uri_list(uri, &bytes);
        }
    }
    else if (format_id == CF_DIB ||
             ohos_cliprdr_image_kind_from_format(format_id) !=
             OHOS_CLIPRDR_REQUEST_NONE)
    {
        ok = ohos_cliprdr_read_local_image(cliprdr, format_id, &data,
                                           &bytes) == 0;
    }
    else if (format_id == OHOS_CLIPRDR_FORMAT_FILE_GROUP_DESCRIPTOR)
    {
        return ohos_cliprdr_send_local_file_descriptor(cliprdr);
    }
    else if (format_id == OHOS_CLIPRDR_FORMAT_DROP_EFFECT ||
             format_id == OHOS_CLIPRDR_FORMAT_PREFERRED_DROP_EFFECT)
    {
        bytes = 4;
        data = (char *)g_malloc(bytes, 1);
        if (data != 0)
        {
            data[0] = 1;
        }
    }
    else if (format_id == CF_UNICODETEXT ||
             format_id == CF_TEXT ||
             format_id == CF_OEMTEXT)
    {
        ok = ohos_cliprdr_pasteboard_read_plain_text(cliprdr, &text) == 0;
        if (!ok && ohos_cliprdr_pasteboard_read_uri(cliprdr, &uri) == 0)
        {
            text = g_strdup(uri);
            ok = text != 0;
        }
        if (!ok && ohos_cliprdr_pasteboard_read_html(cliprdr, &html,
                                                     &plain) == 0)
        {
            text = g_strdup(plain != 0 && plain[0] != '\0' ? plain : html);
            ok = text != 0;
        }
        if (ok && format_id == CF_UNICODETEXT)
        {
            data = ohos_cliprdr_utf8_to_utf16le(text, &bytes);
        }
        else if (ok)
        {
            bytes = g_strlen(text) + 1;
            data = (char *)g_malloc(bytes, 1);
            if (data != 0)
            {
                g_memcpy(data, text, bytes - 1);
            }
        }
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote requested local format=%d bytes=%d ok=%d",
        format_id, bytes, data != 0);
    rv = ohos_cliprdr_send_format_data_response(cliprdr, data, bytes);
    g_free(data);
    g_free(text);
    g_free(html);
    g_free(plain);
    g_free(uri);
    return rv;
}

static int
ohos_cliprdr_process_format_data_response(struct ohos_cliprdr *cliprdr,
                                          int msg_flags, struct stream *s,
                                          int data_len)
{
    char *text = 0;
    char *html = 0;
    char *uri = 0;
    int format_id = cliprdr->requested_format;
    int request_kind = cliprdr->requested_kind;
    int wrote = 0;
    int rv = 0;

    cliprdr->requested_format = 0;
    cliprdr->requested_kind = OHOS_CLIPRDR_REQUEST_NONE;
    if (request_kind == OHOS_CLIPRDR_REQUEST_NONE)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: remote data response had no matching request flags=0x%08x bytes=%d",
            msg_flags, data_len);
        return ohos_cliprdr_request_pending_remote_data(cliprdr);
    }
    if ((msg_flags & CB_RESPONSE_FAIL) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: remote data response failed format=%d(%s) kind=%s(%d)",
            format_id, ohos_cliprdr_format_display_name(format_id),
            ohos_cliprdr_request_kind_name(request_kind), request_kind);
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, 0, 0);
    }
    if (data_len <= 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: remote data response was empty format=%d(%s) kind=%s(%d)",
            format_id, ohos_cliprdr_format_display_name(format_id),
            ohos_cliprdr_request_kind_name(request_kind), request_kind);
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, 0, 0);
    }
    if (!s_check_rem_and_log(s, data_len, "OHOS cliprdr data response"))
    {
        return 1;
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote data response format=%d(%s) kind=%s(%d) bytes=%d flags=0x%08x",
        format_id, ohos_cliprdr_format_display_name(format_id),
        ohos_cliprdr_request_kind_name(request_kind), request_kind,
        data_len, msg_flags);

    if (request_kind == OHOS_CLIPRDR_REQUEST_HTML)
    {
        html = ohos_cliprdr_extract_ms_html(s->p, data_len);
        if (html != 0 && html[0] != '\0')
        {
            rv = ohos_cliprdr_pasteboard_write_html(cliprdr, html, 0);
            wrote = (rv == 0);
        }
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote html response html-bytes=%d write-rv=%d",
            html == 0 ? 0 : (int)g_strlen(html), rv);
        g_free(html);
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, rv,
                                                     wrote);
    }
    if (request_kind == OHOS_CLIPRDR_REQUEST_URIW)
    {
        uri = ohos_cliprdr_utf16le_to_utf8(s->p, data_len);
        if (uri != 0 && uri[0] != '\0')
        {
            rv = ohos_cliprdr_pasteboard_write_uri(cliprdr, uri);
            wrote = (rv == 0);
        }
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote uriw response uri-bytes=%d write-rv=%d",
            uri == 0 ? 0 : (int)g_strlen(uri), rv);
        g_free(uri);
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, rv,
                                                     wrote);
    }
    if (request_kind == OHOS_CLIPRDR_REQUEST_URI_LIST)
    {
        uri = ohos_cliprdr_extract_uri_list_first(s->p, data_len);
        if (uri == 0)
        {
            uri = ohos_cliprdr_bytes_to_text(s->p, data_len);
        }
        if (uri != 0 && uri[0] != '\0')
        {
            rv = ohos_cliprdr_pasteboard_write_uri(cliprdr, uri);
            wrote = (rv == 0);
        }
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote uri-list response uri-bytes=%d write-rv=%d",
            uri == 0 ? 0 : (int)g_strlen(uri), rv);
        g_free(uri);
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, rv,
                                                     wrote);
    }
    if (request_kind == OHOS_CLIPRDR_REQUEST_DIB ||
            request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_BMP ||
            request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_PNG ||
            request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_JPEG ||
            request_kind == OHOS_CLIPRDR_REQUEST_IMAGE_WEBP)
    {
        rv = ohos_cliprdr_write_remote_image(cliprdr, request_kind,
                                             s->p, data_len);
        wrote = (rv == 0);
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: remote image response kind=%s bytes=%d write-rv=%d",
            ohos_cliprdr_request_kind_name(request_kind), data_len, rv);
        if (wrote)
        {
            cliprdr->remote_responses_received++;
        }
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, rv,
                                                     wrote);
    }
    if (request_kind == OHOS_CLIPRDR_REQUEST_FILE_GROUP_DESCRIPTOR)
    {
        rv = ohos_cliprdr_process_remote_file_descriptor(cliprdr, s->p,
                                                         data_len);
        wrote = (rv == 0);
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote FileGroupDescriptorW response bytes=%d rv=%d",
            data_len, rv);
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, rv,
                                                     wrote);
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
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, 0, 0);
    }

    if (text == 0 || text[0] == '\0')
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote text response was empty");
        g_free(text);
        return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                     request_kind, 0, 0);
    }

    rv = ohos_cliprdr_pasteboard_write_plain_text(cliprdr, text);
    wrote = (rv == 0);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote text response text-bytes=%d write-rv=%d",
        (int)g_strlen(text), rv);
    if (wrote)
    {
        cliprdr->remote_responses_received++;
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.cliprdr: copied remote text to Pasteboard bytes=%d",
            (int)g_strlen(text));
    }
    g_free(text);
    return ohos_cliprdr_complete_remote_response(cliprdr, format_id,
                                                 request_kind, rv, wrote);
}

static int
ohos_cliprdr_process_caps(struct ohos_cliprdr *cliprdr, struct stream *s)
{
    int cap_count;
    int pad;
    int index;
    int saw_general = 0;
    int had_deferred_change = 0;
    int rv;

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
            cliprdr->remote_caps_received = 1;
            saw_general = 1;
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.cliprdr: remote caps version=%d flags=0x%8.8x",
                version, flags);
        }
        s->p = cap_end;
    }
    if (saw_general && cliprdr->local_format_lists_sent == 0)
    {
        if (ohos_cliprdr_lock(cliprdr) == 0)
        {
            had_deferred_change = cliprdr->local_change_pending;
            cliprdr->local_change_pending = 0;
            ohos_cliprdr_unlock(cliprdr);
        }
        rv = ohos_cliprdr_send_local_format_list(cliprdr,
                                                 "remote caps",
                                                 had_deferred_change);
        if (rv != 0)
        {
            return rv;
        }
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
        case CB_FILECONTENTS_REQUEST:
            rv = ohos_cliprdr_process_local_filecontents_request(cliprdr, s,
                                                                 data_len);
            break;
        case CB_FILECONTENTS_RESPONSE:
            rv = ohos_cliprdr_process_remote_filecontents_response(cliprdr,
                                                                   msg_flags,
                                                                   s,
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
