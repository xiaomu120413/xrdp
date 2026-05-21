/*
 * Format names and lightweight data classification for OHOS cliprdr.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "ms-rdpeclip.h"
#include "os_calls.h"
#include "parse.h"
#include "string_calls.h"

#include <ctype.h>

static const char g_html_format_name[] = "HTML Format";
static const char g_uriw_format_name[] = "UniformResourceLocatorW";
static const char g_uri_list_format_name[] = "text/uri-list";
static const char g_image_bmp_format_name[] = "image/bmp";
static const char g_image_png_format_name[] = "image/png";
static const char g_image_jpeg_format_name[] = "image/jpeg";
static const char g_image_webp_format_name[] = "image/webp";

int
OH_FileUri_GetPathFromUri(const char *uri, unsigned int length, char **result);

int
ohos_cliprdr_strcasecmp(const char *left, const char *right)
{
    if (left == 0 || right == 0)
    {
        return 1;
    }
    while (*left != '\0' && *right != '\0')
    {
        int lc = tolower((unsigned char)*left);
        int rc = tolower((unsigned char)*right);
        if (lc != rc)
        {
            return lc - rc;
        }
        left++;
        right++;
    }
    return (int)(unsigned char)*left - (int)(unsigned char)*right;
}

int
ohos_cliprdr_starts_with(const char *value, const char *prefix)
{
    if (value == 0 || prefix == 0)
    {
        return 0;
    }
    while (*prefix != '\0')
    {
        if (*value == '\0' ||
                tolower((unsigned char)*value) !=
                tolower((unsigned char)*prefix))
        {
            return 0;
        }
        value++;
        prefix++;
    }
    return 1;
}

int
ohos_cliprdr_is_uri_text(const char *value)
{
    return value != 0 &&
           (ohos_cliprdr_starts_with(value, "http://") ||
            ohos_cliprdr_starts_with(value, "https://") ||
            ohos_cliprdr_starts_with(value, "file://") ||
            ohos_cliprdr_starts_with(value, "content://") ||
            ohos_cliprdr_starts_with(value, "datashare://") ||
            value[0] == '/');
}

const char *
ohos_cliprdr_format_name(int format_id)
{
    switch (format_id)
    {
        case OHOS_CLIPRDR_FORMAT_HTML:
            return g_html_format_name;
        case OHOS_CLIPRDR_FORMAT_URIW:
            return g_uriw_format_name;
        case OHOS_CLIPRDR_FORMAT_URI_LIST:
            return g_uri_list_format_name;
        case OHOS_CLIPRDR_FORMAT_IMAGE_BMP:
            return g_image_bmp_format_name;
        case OHOS_CLIPRDR_FORMAT_IMAGE_PNG:
            return g_image_png_format_name;
        case OHOS_CLIPRDR_FORMAT_IMAGE_JPEG:
            return g_image_jpeg_format_name;
        case OHOS_CLIPRDR_FORMAT_IMAGE_WEBP:
            return g_image_webp_format_name;
        default:
            return 0;
    }
}

static int
ohos_cliprdr_hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

static char *
ohos_cliprdr_percent_decode(const char *value)
{
    int in;
    int out = 0;
    int len;
    char *decoded;

    if (value == 0)
    {
        return 0;
    }
    len = g_strlen(value);
    decoded = (char *)g_malloc(len + 1, 1);
    if (decoded == 0)
    {
        return 0;
    }
    for (in = 0; in < len; in++)
    {
        if (value[in] == '%' && in + 2 < len)
        {
            int hi = ohos_cliprdr_hex_value(value[in + 1]);
            int lo = ohos_cliprdr_hex_value(value[in + 2]);
            if (hi >= 0 && lo >= 0)
            {
                decoded[out++] = (char)((hi << 4) | lo);
                in += 2;
                continue;
            }
        }
        decoded[out++] = value[in];
    }
    decoded[out] = '\0';
    return decoded;
}

char *
ohos_cliprdr_uri_to_local_path(const char *uri)
{
    const char *path;
    char *converted = 0;
    char *decoded;
    int prefix_slash = 0;
    int len;

    if (uri == 0 || uri[0] == '\0')
    {
        return 0;
    }
    if (ohos_cliprdr_starts_with(uri, "file://"))
    {
        len = g_strlen(uri);
        if (OH_FileUri_GetPathFromUri(uri, (unsigned int)len,
                                      &converted) == 0 &&
                converted != 0 && converted[0] != '\0')
        {
            return converted;
        }
        g_free(converted);
        path = uri + 7;
        if (ohos_cliprdr_starts_with(path, "localhost/"))
        {
            path += 9;
        }
        else if (path[0] != '/' && path[0] != '\\')
        {
            prefix_slash = 1;
        }
        if (path[0] == '\0')
        {
            return 0;
        }
    }
    else if (uri[0] == '/')
    {
        path = uri;
    }
    else
    {
        return 0;
    }

    decoded = ohos_cliprdr_percent_decode(path);
    if (!prefix_slash || decoded == 0)
    {
        return decoded;
    }
    len = g_strlen(decoded);
    converted = (char *)g_malloc(len + 2, 1);
    if (converted != 0)
    {
        converted[0] = '/';
        g_memcpy(converted + 1, decoded, len + 1);
    }
    g_free(decoded);
    return converted;
}

int
ohos_cliprdr_image_format_from_signature(const char *data, int bytes)
{
    const unsigned char *p = (const unsigned char *)data;

    if (p == 0 || bytes < 3)
    {
        return 0;
    }
    if (bytes >= 2 && p[0] == 'B' && p[1] == 'M')
    {
        return OHOS_CLIPRDR_FORMAT_IMAGE_BMP;
    }
    if (bytes >= 8 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' &&
            p[3] == 'G' && p[4] == '\r' && p[5] == '\n' &&
            p[6] == 0x1a && p[7] == '\n')
    {
        return OHOS_CLIPRDR_FORMAT_IMAGE_PNG;
    }
    if (p[0] == 0xff && p[1] == 0xd8 && p[2] == 0xff)
    {
        return OHOS_CLIPRDR_FORMAT_IMAGE_JPEG;
    }
    if (bytes >= 12 && p[0] == 'R' && p[1] == 'I' && p[2] == 'F' &&
            p[3] == 'F' && p[8] == 'W' && p[9] == 'E' &&
            p[10] == 'B' && p[11] == 'P')
    {
        return OHOS_CLIPRDR_FORMAT_IMAGE_WEBP;
    }
    return 0;
}

const char *
ohos_cliprdr_image_extension(int format_id)
{
    switch (format_id)
    {
        case OHOS_CLIPRDR_FORMAT_IMAGE_BMP:
            return ".bmp";
        case OHOS_CLIPRDR_FORMAT_IMAGE_PNG:
            return ".png";
        case OHOS_CLIPRDR_FORMAT_IMAGE_JPEG:
            return ".jpg";
        case OHOS_CLIPRDR_FORMAT_IMAGE_WEBP:
            return ".webp";
        default:
            return ".img";
    }
}

char *
ohos_cliprdr_read_format_name(struct stream *s, int msg_flags)
{
    char *start;
    char *name;
    int wchar;
    int bytes;
    int skip;

    if ((msg_flags & CB_ASCII_NAMES) == 0 &&
            s_check_rem(s, 2))
    {
        start = s->p;
        wchar = 1;
        while (s_check_rem(s, 2) && wchar != 0)
        {
            in_uint16_le(s, wchar);
        }
        bytes = (int)(s->p - start);
        if (bytes > 2)
        {
            return ohos_cliprdr_utf16le_to_utf8(start, bytes);
        }
        return 0;
    }

    skip = s_rem(s) < 32 ? s_rem(s) : 32;
    start = s->p;
    in_uint8s(s, skip);
    name = ohos_cliprdr_bytes_to_text(start, skip);
    return name;
}

int
ohos_cliprdr_name_is(const char *name, int local_format)
{
    const char *format_name = ohos_cliprdr_format_name(local_format);

    return format_name != 0 && ohos_cliprdr_strcasecmp(name, format_name) == 0;
}

int
ohos_cliprdr_image_kind_from_format(int format_id)
{
    switch (format_id)
    {
        case OHOS_CLIPRDR_FORMAT_IMAGE_BMP:
            return OHOS_CLIPRDR_REQUEST_IMAGE_BMP;
        case OHOS_CLIPRDR_FORMAT_IMAGE_PNG:
            return OHOS_CLIPRDR_REQUEST_IMAGE_PNG;
        case OHOS_CLIPRDR_FORMAT_IMAGE_JPEG:
            return OHOS_CLIPRDR_REQUEST_IMAGE_JPEG;
        case OHOS_CLIPRDR_FORMAT_IMAGE_WEBP:
            return OHOS_CLIPRDR_REQUEST_IMAGE_WEBP;
        default:
            return OHOS_CLIPRDR_REQUEST_NONE;
    }
}
