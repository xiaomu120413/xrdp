/*
 * File clipboard support for OHOS cliprdr.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "log.h"
#include "ms-rdpeclip.h"
#include "os_calls.h"
#include "string_calls.h"

#define OHOS_CLIPRDR_FILEDESCRIPTORW_BYTES 592

struct ohos_cliprdr_local_file
{
    char *uri;
    char *path;
    char *name;
    int size;
};

static void
ohos_cliprdr_free_local_file(struct ohos_cliprdr_local_file *file)
{
    if (file == 0)
    {
        return;
    }
    g_free(file->uri);
    g_free(file->path);
    g_free(file->name);
    g_memset(file, 0, sizeof(struct ohos_cliprdr_local_file));
}

static const char *
ohos_cliprdr_basename(const char *path)
{
    const char *name;

    if (path == 0)
    {
        return "";
    }
    name = path + g_strlen(path);
    while (name > path && name[-1] != '/' && name[-1] != '\\')
    {
        name--;
    }
    return name;
}

static char *
ohos_cliprdr_sanitize_filename(const char *name)
{
    char *safe;
    int in;
    int out = 0;
    int len;

    if (name == 0 || name[0] == '\0')
    {
        name = "clipboard-file";
    }
    name = ohos_cliprdr_basename(name);
    len = g_strlen(name);
    if (len > 160)
    {
        len = 160;
    }
    safe = (char *)g_malloc(len + 1, 1);
    if (safe == 0)
    {
        return 0;
    }
    for (in = 0; in < len; in++)
    {
        unsigned char c = (unsigned char)name[in];
        if (c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
        {
            safe[out++] = '_';
        }
        else
        {
            safe[out++] = (char)c;
        }
    }
    while (out > 0 && (safe[out - 1] == '.' || safe[out - 1] == ' '))
    {
        out--;
    }
    if (out == 0)
    {
        g_strncpy(safe, "clipboard-file", len + 1);
    }
    else
    {
        safe[out] = '\0';
    }
    return safe;
}

static int
ohos_cliprdr_get_local_file(struct ohos_cliprdr *cliprdr,
                            struct ohos_cliprdr_local_file *file)
{
    int size;

    if (file == 0)
    {
        return 1;
    }
    g_memset(file, 0, sizeof(struct ohos_cliprdr_local_file));
    if (ohos_cliprdr_pasteboard_read_uri(cliprdr, &file->uri) != 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local file probe found no URI data");
        return 1;
    }
    file->path = ohos_cliprdr_uri_to_local_path(file->uri);
    if (file->path == 0 || file->path[0] == '\0')
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local file probe cannot map uri=%s",
            file->uri);
        ohos_cliprdr_free_local_file(file);
        return 1;
    }
    if (!g_file_exist(file->path) || g_directory_exist(file->path) ||
            !g_file_readable(file->path))
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local file probe unreadable path=%s exists=%d dir=%d readable=%d",
            file->path, g_file_exist(file->path),
            g_directory_exist(file->path), g_file_readable(file->path));
        ohos_cliprdr_free_local_file(file);
        return 1;
    }
    size = g_file_get_size(file->path);
    if (size < 0)
    {
        ohos_cliprdr_free_local_file(file);
        return 1;
    }
    file->name = ohos_cliprdr_sanitize_filename(file->path);
    file->size = size;
    if (file->name == 0)
    {
        ohos_cliprdr_free_local_file(file);
        return 1;
    }
    return 0;
}

int
ohos_cliprdr_has_local_file(struct ohos_cliprdr *cliprdr)
{
    struct ohos_cliprdr_local_file file;
    int rv;

    rv = ohos_cliprdr_get_local_file(cliprdr, &file);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: local file probe ok=%d uri=%s path=%s size=%d",
        rv == 0, rv == 0 ? file.uri : "", rv == 0 ? file.path : "",
        rv == 0 ? file.size : 0);
    ohos_cliprdr_free_local_file(&file);
    return rv == 0;
}

static void
ohos_cliprdr_out_filedescriptorw(struct stream *s,
                                 struct ohos_cliprdr_local_file *file)
{
    const char *name;
    unsigned int utf8_count;
    unsigned int utf16_count;
    int flags;

    flags = CB_FD_ATTRIBUTES | CB_FD_FILESIZE;
    name = file->name;
    utf8_count = g_strlen(name) + 1;
    utf16_count = utf8_as_utf16_word_count(name, utf8_count);
    if (utf16_count > 260)
    {
        name = "clipboard-file";
        utf8_count = g_strlen(name) + 1;
        utf16_count = utf8_as_utf16_word_count(name, utf8_count);
    }

    out_uint32_le(s, flags);
    out_uint8s(s, 32);
    out_uint32_le(s, CB_FILE_ATTRIBUTE_NORMAL);
    out_uint8s(s, 24);
    out_uint32_le(s, 0);
    out_uint32_le(s, file->size);
    out_utf8_as_utf16_le(s, name, utf8_count);
    out_uint8s(s, (260 - utf16_count) * 2);
}

int
ohos_cliprdr_send_local_file_descriptor(struct ohos_cliprdr *cliprdr)
{
    struct ohos_cliprdr_local_file file;
    struct stream *s;
    int rv;

    if (ohos_cliprdr_get_local_file(cliprdr, &file) != 0)
    {
        return ohos_cliprdr_send_format_data_response(cliprdr, 0, 0);
    }
    make_stream(s);
    if (s == 0)
    {
        ohos_cliprdr_free_local_file(&file);
        return 1;
    }
    init_stream(s, 4 + OHOS_CLIPRDR_FILEDESCRIPTORW_BYTES);
    if (s->data == 0)
    {
        free_stream(s);
        ohos_cliprdr_free_local_file(&file);
        return 1;
    }
    out_uint32_le(s, 1);
    ohos_cliprdr_out_filedescriptorw(s, &file);
    s_mark_end(s);
    rv = ohos_cliprdr_send_format_data_response(cliprdr, s->data,
                                                (int)(s->end - s->data));
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: sent local FileGroupDescriptorW name=%s size=%d bytes=%d flags=0x%08x attrs=0x%08x rv=%d",
        file.name, file.size, (int)(s->end - s->data),
        CB_FD_ATTRIBUTES | CB_FD_FILESIZE, CB_FILE_ATTRIBUTE_NORMAL, rv);
    free_stream(s);
    ohos_cliprdr_free_local_file(&file);
    return rv;
}

static int
ohos_cliprdr_send_filecontents_fail(struct ohos_cliprdr *cliprdr,
                                    int stream_id)
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
    ohos_cliprdr_out_header(s, CB_FILECONTENTS_RESPONSE, CB_RESPONSE_FAIL);
    out_uint32_le(s, stream_id);
    s_mark_end(s);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    LOG(LOG_LEVEL_WARNING,
        "xrdp.ohos.cliprdr: sent local filecontents failure stream=%d rv=%d",
        stream_id, rv);
    free_stream(s);
    return rv;
}

static int
ohos_cliprdr_send_local_file_size(struct ohos_cliprdr *cliprdr,
                                  int stream_id, int lindex)
{
    struct ohos_cliprdr_local_file file;
    struct stream *s;
    int rv;

    if (lindex != 0 || ohos_cliprdr_get_local_file(cliprdr, &file) != 0)
    {
        return ohos_cliprdr_send_filecontents_fail(cliprdr, stream_id);
    }
    make_stream(s);
    if (s == 0)
    {
        ohos_cliprdr_free_local_file(&file);
        return 1;
    }
    init_stream(s, 64);
    if (s->data == 0)
    {
        free_stream(s);
        ohos_cliprdr_free_local_file(&file);
        return 1;
    }
    ohos_cliprdr_out_header(s, CB_FILECONTENTS_RESPONSE, CB_RESPONSE_OK);
    out_uint32_le(s, stream_id);
    out_uint32_le(s, file.size);
    out_uint32_le(s, 0);
    s_mark_end(s);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: sent local file size stream=%d lindex=%d size=%d rv=%d",
        stream_id, lindex, file.size, rv);
    free_stream(s);
    ohos_cliprdr_free_local_file(&file);
    return rv;
}

static int
ohos_cliprdr_send_local_file_range(struct ohos_cliprdr *cliprdr,
                                   int stream_id, int lindex,
                                   int position, int requested)
{
    struct ohos_cliprdr_local_file file;
    struct stream *s;
    int fd;
    int bytes;
    int rv;

    if (lindex != 0 || position < 0 || requested < 0 ||
            ohos_cliprdr_get_local_file(cliprdr, &file) != 0)
    {
        return ohos_cliprdr_send_filecontents_fail(cliprdr, stream_id);
    }
    if (position > file.size)
    {
        ohos_cliprdr_free_local_file(&file);
        return ohos_cliprdr_send_filecontents_fail(cliprdr, stream_id);
    }
    if (requested > file.size - position)
    {
        requested = file.size - position;
    }
    if (requested > OHOS_CLIPRDR_MAX_FILE_CHUNK_BYTES)
    {
        requested = OHOS_CLIPRDR_MAX_FILE_CHUNK_BYTES;
    }
    fd = g_file_open_ro(file.path);
    if (fd < 0 || g_file_seek(fd, position) < 0)
    {
        if (fd >= 0)
        {
            g_file_close(fd);
        }
        ohos_cliprdr_free_local_file(&file);
        return ohos_cliprdr_send_filecontents_fail(cliprdr, stream_id);
    }
    make_stream(s);
    if (s == 0)
    {
        g_file_close(fd);
        ohos_cliprdr_free_local_file(&file);
        return 1;
    }
    init_stream(s, requested + 64);
    if (s->data == 0)
    {
        free_stream(s);
        g_file_close(fd);
        ohos_cliprdr_free_local_file(&file);
        return 1;
    }
    ohos_cliprdr_out_header(s, CB_FILECONTENTS_RESPONSE, CB_RESPONSE_OK);
    out_uint32_le(s, stream_id);
    bytes = g_file_read(fd, s->p, requested);
    g_file_close(fd);
    if (bytes < 0)
    {
        free_stream(s);
        ohos_cliprdr_free_local_file(&file);
        return ohos_cliprdr_send_filecontents_fail(cliprdr, stream_id);
    }
    s->p += bytes;
    s_mark_end(s);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: sent local file range stream=%d lindex=%d pos=%d requested=%d bytes=%d name=%s rv=%d",
        stream_id, lindex, position, requested, bytes, file.name, rv);
    free_stream(s);
    ohos_cliprdr_free_local_file(&file);
    return rv;
}

int
ohos_cliprdr_process_local_filecontents_request(struct ohos_cliprdr *cliprdr,
                                                struct stream *s,
                                                int data_len)
{
    int stream_id;
    int lindex;
    int flags;
    int position_low;
    int position_high;
    int requested;
    int clip_data_id = 0;
    int have_clip_data_id = 0;

    if (data_len < 24 ||
            !s_check_rem_and_log(s, 24, "OHOS cliprdr file request"))
    {
        return 1;
    }
    in_uint32_le(s, stream_id);
    in_uint32_le(s, lindex);
    in_uint32_le(s, flags);
    in_uint32_le(s, position_low);
    in_uint32_le(s, position_high);
    in_uint32_le(s, requested);
    if (s_check_rem(s, 4))
    {
        in_uint32_le(s, clip_data_id);
        have_clip_data_id = 1;
    }
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: local filecontents request stream=%d lindex=%d flags=0x%08x pos=%d/%d requested=%d haveClipDataId=%d clipDataId=%d",
        stream_id, lindex, flags, position_high, position_low, requested,
        have_clip_data_id, clip_data_id);
    if (lindex != 0 || position_high != 0 ||
            (((flags & CB_FILECONTENTS_SIZE) != 0) ==
             ((flags & CB_FILECONTENTS_RANGE) != 0)))
    {
        return ohos_cliprdr_send_filecontents_fail(cliprdr, stream_id);
    }
    if ((flags & CB_FILECONTENTS_SIZE) != 0)
    {
        return ohos_cliprdr_send_local_file_size(cliprdr, stream_id, lindex);
    }
    if ((flags & CB_FILECONTENTS_RANGE) != 0)
    {
        return ohos_cliprdr_send_local_file_range(cliprdr, stream_id, lindex,
                                                  position_low, requested);
    }
    return ohos_cliprdr_send_filecontents_fail(cliprdr, stream_id);
}
