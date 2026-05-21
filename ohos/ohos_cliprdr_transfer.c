/*
 * Clipboard payload conversions used by OHOS cliprdr.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "os_calls.h"
#include "string_calls.h"

static void
ohos_cliprdr_append_utf8(char **text, int *count, int *capacity,
                         unsigned int cp)
{
    char encoded[4];
    int encoded_count = 0;
    char *grown;

    if (cp <= 0x7f)
    {
        encoded[encoded_count++] = (char)cp;
    }
    else if (cp <= 0x7ff)
    {
        encoded[encoded_count++] = (char)(0xc0 | (cp >> 6));
        encoded[encoded_count++] = (char)(0x80 | (cp & 0x3f));
    }
    else if (cp <= 0xffff)
    {
        encoded[encoded_count++] = (char)(0xe0 | (cp >> 12));
        encoded[encoded_count++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        encoded[encoded_count++] = (char)(0x80 | (cp & 0x3f));
    }
    else
    {
        encoded[encoded_count++] = (char)(0xf0 | (cp >> 18));
        encoded[encoded_count++] = (char)(0x80 | ((cp >> 12) & 0x3f));
        encoded[encoded_count++] = (char)(0x80 | ((cp >> 6) & 0x3f));
        encoded[encoded_count++] = (char)(0x80 | (cp & 0x3f));
    }

    if (*count + encoded_count + 1 > *capacity)
    {
        *capacity = (*capacity + encoded_count + 64) * 2;
        grown = (char *)g_malloc(*capacity, 1);
        if (grown == 0)
        {
            return;
        }
        if (*text != 0 && *count > 0)
        {
            g_memcpy(grown, *text, *count);
        }
        g_free(*text);
        *text = grown;
    }
    g_memcpy(*text + *count, encoded, encoded_count);
    *count += encoded_count;
    (*text)[*count] = '\0';
}

char *
ohos_cliprdr_utf16le_to_utf8(const char *data, int bytes)
{
    int index;
    int units;
    int count = 0;
    int capacity;
    char *text;

    if (data == 0 || bytes <= 0)
    {
        return g_strdup("");
    }
    units = bytes / 2;
    capacity = bytes + 4;
    text = (char *)g_malloc(capacity, 1);
    if (text == 0)
    {
        return 0;
    }

    for (index = 0; index < units; index++)
    {
        unsigned int cp = ((unsigned char)data[index * 2]) |
                          (((unsigned char)data[index * 2 + 1]) << 8);
        if (cp == 0)
        {
            break;
        }
        if (cp >= 0xd800 && cp <= 0xdbff && index + 1 < units)
        {
            unsigned int next = ((unsigned char)data[(index + 1) * 2]) |
                                (((unsigned char)data[(index + 1) * 2 + 1]) << 8);
            if (next >= 0xdc00 && next <= 0xdfff)
            {
                cp = 0x10000 + (((cp - 0xd800) << 10) | (next - 0xdc00));
                index++;
            }
        }
        ohos_cliprdr_append_utf8(&text, &count, &capacity, cp);
    }
    return text;
}

char *
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

char *
ohos_cliprdr_utf8_to_utf16le(const char *text, int *out_bytes)
{
    struct stream *s;
    int text_len;
    int bytes;
    char *data;

    if (out_bytes == 0)
    {
        return 0;
    }
    *out_bytes = 0;
    if (text == 0)
    {
        text = "";
    }
    text_len = g_strlen(text);
    bytes = (int)utf8_as_utf16_word_count(text, text_len) * 2 + 2;
    data = (char *)g_malloc(bytes, 1);
    if (data == 0)
    {
        return 0;
    }
    make_stream(s);
    if (s == 0)
    {
        g_free(data);
        return 0;
    }
    init_stream(s, bytes);
    if (s->data == 0)
    {
        free_stream(s);
        g_free(data);
        return 0;
    }
    out_utf8_as_utf16_le(s, text, text_len);
    out_uint16_le(s, 0);
    g_memcpy(data, s->data, bytes);
    free_stream(s);
    *out_bytes = bytes;
    return data;
}

static const char *
ohos_cliprdr_find_token(const char *text, const char *token)
{
    int token_len;
    const char *cursor;

    if (text == 0 || token == 0 || token[0] == '\0')
    {
        return 0;
    }
    token_len = g_strlen(token);
    for (cursor = text; *cursor != '\0'; cursor++)
    {
        if (g_strncmp(cursor, token, token_len) == 0)
        {
            return cursor;
        }
    }
    return 0;
}

static int
ohos_cliprdr_parse_html_offset(const char *text, const char *key)
{
    const char *cursor;
    int value = 0;
    int found = 0;

    cursor = ohos_cliprdr_find_token(text, key);
    if (cursor == 0)
    {
        return -1;
    }
    cursor += g_strlen(key);
    while (*cursor == ' ' || *cursor == '\t')
    {
        cursor++;
    }
    while (*cursor >= '0' && *cursor <= '9')
    {
        found = 1;
        value = (value * 10) + (*cursor - '0');
        cursor++;
    }
    return found ? value : -1;
}

char *
ohos_cliprdr_extract_ms_html(const char *data, int bytes)
{
    char *source;
    char *html;
    int start;
    int end;
    int len;

    source = ohos_cliprdr_bytes_to_text(data, bytes);
    if (source == 0)
    {
        return 0;
    }
    start = ohos_cliprdr_parse_html_offset(source, "StartFragment:");
    end = ohos_cliprdr_parse_html_offset(source, "EndFragment:");
    if (start < 0 || end < 0 || start >= end || end > bytes)
    {
        start = ohos_cliprdr_parse_html_offset(source, "StartHTML:");
        end = ohos_cliprdr_parse_html_offset(source, "EndHTML:");
    }
    if (start >= 0 && end > start && end <= bytes)
    {
        len = end - start;
        html = g_strndup(source + start, len);
        g_free(source);
        return html;
    }
    return source;
}

char *
ohos_cliprdr_html_to_ms_html(const char *html, int *out_bytes)
{
    static const char prefix[] =
        "Version:0.9\r\n"
        "StartHTML:%010u\r\n"
        "EndHTML:%010u\r\n"
        "StartFragment:%010u\r\n"
        "EndFragment:%010u\r\n";
    static const char start_fragment[] = "<html><body>\r\n<!--StartFragment-->";
    static const char end_fragment[] = "<!--EndFragment-->\r\n</body></html>";
    char *data;
    int header_len;
    int start_html;
    int start_fragment_offset;
    int end_fragment_offset;
    int end_html;
    int total;
    char dummy[1];

    if (out_bytes == 0)
    {
        return 0;
    }
    *out_bytes = 0;
    if (html == 0)
    {
        html = "";
    }
    header_len = g_snprintf(dummy, 1, prefix, 0, 0, 0, 0);
    if (header_len <= 0)
    {
        return 0;
    }
    start_html = header_len;
    start_fragment_offset = start_html + g_strlen(start_fragment);
    end_fragment_offset = start_fragment_offset + g_strlen(html);
    end_html = end_fragment_offset + g_strlen(end_fragment);
    total = end_html + 1;
    data = (char *)g_malloc(total, 1);
    if (data == 0)
    {
        return 0;
    }
    (void)g_snprintf(data, total, prefix, start_html, end_html,
                     start_fragment_offset, end_fragment_offset);
    g_memcpy(data + start_html, start_fragment, g_strlen(start_fragment));
    g_memcpy(data + start_fragment_offset, html, g_strlen(html));
    g_memcpy(data + end_fragment_offset, end_fragment, g_strlen(end_fragment));
    *out_bytes = total;
    return data;
}

char *
ohos_cliprdr_extract_uri_list_first(const char *data, int bytes)
{
    char *source;
    char *cursor;

    source = ohos_cliprdr_bytes_to_text(data, bytes);
    if (source == 0)
    {
        return 0;
    }
    cursor = source;
    while (*cursor != '\0')
    {
        char *line;
        while (*cursor == '\r' || *cursor == '\n')
        {
            cursor++;
        }
        line = cursor;
        while (*cursor != '\0' && *cursor != '\r' && *cursor != '\n')
        {
            cursor++;
        }
        if (*cursor != '\0')
        {
            *cursor++ = '\0';
        }
        if (line[0] != '#' && line[0] != '\0')
        {
            char *uri = g_strdup(line);
            g_free(source);
            return uri;
        }
    }
    g_free(source);
    return 0;
}

char *
ohos_cliprdr_uri_to_uri_list(const char *uri, int *out_bytes)
{
    int uri_len;
    char *data;

    if (uri == 0 || out_bytes == 0)
    {
        return 0;
    }
    uri_len = g_strlen(uri);
    data = (char *)g_malloc(uri_len + 3, 1);
    if (data == 0)
    {
        return 0;
    }
    g_memcpy(data, uri, uri_len);
    data[uri_len] = '\r';
    data[uri_len + 1] = '\n';
    data[uri_len + 2] = '\0';
    *out_bytes = uri_len + 3;
    return data;
}
