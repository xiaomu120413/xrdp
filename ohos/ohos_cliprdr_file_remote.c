/*
 * Remote FileGroupDescriptorW/FileContents handling for OHOS cliprdr.
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
#define OHOS_CLIPRDR_MAX_REMOTE_FILES 16
#define OHOS_CLIPRDR_REMOTE_FILE_NONE 0
#define OHOS_CLIPRDR_REMOTE_FILE_SIZE 1
#define OHOS_CLIPRDR_REMOTE_FILE_RANGE 2

static const char *
ohos_cliprdr_remote_basename(const char *path)
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
ohos_cliprdr_remote_safe_name(const char *name)
{
    char *safe;
    int in;
    int out = 0;
    int len;

    name = ohos_cliprdr_remote_basename(name);
    if (name == 0 || name[0] == '\0')
    {
        name = "remote-file";
    }
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
        g_strncpy(safe, "remote-file", len + 1);
    }
    else
    {
        safe[out] = '\0';
    }
    return safe;
}

static char *
ohos_cliprdr_make_cache_path(const char *name, int index)
{
    char dir[256];
    char path[512];
    char *safe;
    char *result;

    safe = ohos_cliprdr_remote_safe_name(name);
    if (safe == 0)
    {
        return 0;
    }
    (void)g_create_dir(OHOS_CLIPRDR_SANDBOX_FILES_DIR);
    g_snprintf(dir, sizeof(dir), "%s/%s", OHOS_CLIPRDR_SANDBOX_FILES_DIR,
               OHOS_CLIPRDR_CACHE_DIR_NAME);
    (void)g_create_dir(dir);
    g_snprintf(path, sizeof(path), "%s/xrdp-file-%u-%d-%s", dir,
               g_get_elapsed_ms(), index, safe);
    g_free(safe);
    result = g_strdup(path);
    return result;
}

static char *
ohos_cliprdr_path_to_file_uri(const char *path)
{
    char *uri;
    int len;

    if (path == 0)
    {
        return 0;
    }
    len = g_strlen(path);
    uri = (char *)g_malloc(len + 8, 1);
    if (uri != 0)
    {
        g_sprintf(uri, "file://%s", path);
    }
    return uri;
}

void
ohos_cliprdr_file_transfer_reset(struct ohos_cliprdr *cliprdr)
{
    int index;

    if (cliprdr == 0)
    {
        return;
    }
    if (cliprdr->remote_file_fd >= 0)
    {
        g_file_close(cliprdr->remote_file_fd);
    }
    cliprdr->remote_file_fd = -1;
    if (cliprdr->remote_files != 0)
    {
        for (index = 0; index < cliprdr->remote_file_count; index++)
        {
            g_free(cliprdr->remote_files[index].name);
            if (cliprdr->remote_files[index].path != 0 &&
                    cliprdr->remote_files[index].uri == 0)
            {
                (void)g_file_delete(cliprdr->remote_files[index].path);
            }
            g_free(cliprdr->remote_files[index].path);
            g_free(cliprdr->remote_files[index].uri);
        }
        g_free(cliprdr->remote_files);
    }
    cliprdr->remote_files = 0;
    cliprdr->remote_file_count = 0;
    cliprdr->remote_file_index = 0;
    cliprdr->remote_file_offset = 0;
    cliprdr->remote_file_pending = OHOS_CLIPRDR_REMOTE_FILE_NONE;
}

static int
ohos_cliprdr_send_remote_filecontents_request(struct ohos_cliprdr *cliprdr,
                                              int flags, int position,
                                              int requested)
{
    struct stream *s;
    int stream_id;
    int rv;

    cliprdr->remote_file_stream_id++;
    if (cliprdr->remote_file_stream_id <= 0)
    {
        cliprdr->remote_file_stream_id = 1;
    }
    stream_id = cliprdr->remote_file_stream_id;
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
    ohos_cliprdr_out_header(s, CB_FILECONTENTS_REQUEST, 0);
    out_uint32_le(s, stream_id);
    out_uint32_le(s, cliprdr->remote_file_index);
    out_uint32_le(s, flags);
    out_uint32_le(s, position);
    out_uint32_le(s, 0);
    out_uint32_le(s, requested);
    out_uint32_le(s, 0);
    s_mark_end(s);
    rv = ohos_cliprdr_send_stream(cliprdr, s);
    free_stream(s);
    cliprdr->remote_file_pending =
        (flags & CB_FILECONTENTS_SIZE) != 0 ?
        OHOS_CLIPRDR_REMOTE_FILE_SIZE : OHOS_CLIPRDR_REMOTE_FILE_RANGE;
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: requested remote filecontents stream=%d lindex=%d flags=0x%08x pos=%d requested=%d rv=%d",
        stream_id, cliprdr->remote_file_index, flags, position, requested,
        rv);
    return rv;
}

static int
ohos_cliprdr_request_next_remote_file(struct ohos_cliprdr *cliprdr);

static int
ohos_cliprdr_complete_remote_file(struct ohos_cliprdr *cliprdr)
{
    struct ohos_cliprdr_remote_file *file;
    int rv;

    file = cliprdr->remote_files + cliprdr->remote_file_index;
    if (cliprdr->remote_file_fd >= 0)
    {
        g_file_close(cliprdr->remote_file_fd);
        cliprdr->remote_file_fd = -1;
    }
    file->uri = ohos_cliprdr_path_to_file_uri(file->path);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: completed remote file index=%d name=%s size=%d uri=%s",
        cliprdr->remote_file_index, file->name, file->size,
        file->uri == 0 ? "" : file->uri);
    cliprdr->remote_file_index++;
    if (cliprdr->remote_file_index < cliprdr->remote_file_count)
    {
        return ohos_cliprdr_request_next_remote_file(cliprdr);
    }
    rv = ohos_cliprdr_write_remote_file_uris(cliprdr);
    if (rv == 0)
    {
        cliprdr->remote_responses_received++;
    }
    ohos_cliprdr_file_transfer_reset(cliprdr);
    return rv;
}

static int
ohos_cliprdr_request_next_remote_file(struct ohos_cliprdr *cliprdr)
{
    struct ohos_cliprdr_remote_file *file;

    cliprdr->remote_file_offset = 0;
    file = cliprdr->remote_files + cliprdr->remote_file_index;
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: start remote file index=%d/%d name=%s descriptor-size=%d",
        cliprdr->remote_file_index, cliprdr->remote_file_count,
        file->name == 0 ? "" : file->name, file->size);
    return ohos_cliprdr_send_remote_filecontents_request(
               cliprdr, CB_FILECONTENTS_SIZE, 0, 0);
}

static int
ohos_cliprdr_open_remote_file(struct ohos_cliprdr *cliprdr, int size)
{
    struct ohos_cliprdr_remote_file *file;

    file = cliprdr->remote_files + cliprdr->remote_file_index;
    file->size = size;
    file->path = ohos_cliprdr_make_cache_path(file->name,
                                              cliprdr->remote_file_index);
    if (file->path == 0)
    {
        return 1;
    }
    cliprdr->remote_file_fd = g_file_open_ex(file->path, 0, 1, 1, 1);
    if (cliprdr->remote_file_fd < 0)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: cannot create remote file cache index=%d",
            cliprdr->remote_file_index);
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote file cache create failed path=%s",
            file->path);
        return 1;
    }
    if (size == 0)
    {
        return ohos_cliprdr_complete_remote_file(cliprdr);
    }
    return ohos_cliprdr_send_remote_filecontents_request(
               cliprdr, CB_FILECONTENTS_RANGE, 0,
               size > OHOS_CLIPRDR_MAX_FILE_CHUNK_BYTES ?
               OHOS_CLIPRDR_MAX_FILE_CHUNK_BYTES : size);
}

int
ohos_cliprdr_process_remote_file_descriptor(struct ohos_cliprdr *cliprdr,
                                            const char *data, int bytes)
{
    struct stream ls;
    int citems;
    int available;
    int count;
    int index;

    ohos_cliprdr_file_transfer_reset(cliprdr);
    if (data == 0 || bytes < 4)
    {
        return 1;
    }
    g_memset(&ls, 0, sizeof(ls));
    ls.data = (char *)data;
    ls.p = (char *)data;
    ls.end = (char *)data + bytes;
    ls.size = bytes;
    in_uint32_le(&ls, citems);
    available = s_rem(&ls) / OHOS_CLIPRDR_FILEDESCRIPTORW_BYTES;
    count = citems < available ? citems : available;
    if (count > OHOS_CLIPRDR_MAX_REMOTE_FILES)
    {
        count = OHOS_CLIPRDR_MAX_REMOTE_FILES;
    }
    if (count <= 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote FileGroupDescriptorW empty citems=%d bytes=%d",
            citems, bytes);
        return 1;
    }
    cliprdr->remote_files = (struct ohos_cliprdr_remote_file *)
                            g_malloc(sizeof(struct ohos_cliprdr_remote_file) *
                                     count, 1);
    if (cliprdr->remote_files == 0)
    {
        return 1;
    }
    cliprdr->remote_file_count = count;
    for (index = 0; index < count; index++)
    {
        char name[260 * 4 + 1];
        int flags;
        int attrs;
        int size_high;
        int size_low;
        int time_low;
        int time_high;

        in_uint32_le(&ls, flags);
        in_uint8s(&ls, 32);
        in_uint32_le(&ls, attrs);
        in_uint8s(&ls, 16);
        in_uint32_le(&ls, time_low);
        in_uint32_le(&ls, time_high);
        in_uint32_le(&ls, size_high);
        in_uint32_le(&ls, size_low);
        (void)flags;
        (void)attrs;
        (void)time_low;
        (void)time_high;
        if (in_utf16_le_fixed_as_utf8(&ls, 260, name, sizeof(name)) >=
                sizeof(name))
        {
            g_snprintf(name, sizeof(name), "remote-file-%d", index);
        }
        cliprdr->remote_files[index].name =
            ohos_cliprdr_remote_safe_name(name);
        cliprdr->remote_files[index].size = size_high == 0 ? size_low : -1;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote file descriptor index=%d name=%s size=%d size-high=%d",
            index, cliprdr->remote_files[index].name == 0 ? "" :
            cliprdr->remote_files[index].name, size_low, size_high);
    }
    return ohos_cliprdr_request_next_remote_file(cliprdr);
}

int
ohos_cliprdr_process_remote_filecontents_response(struct ohos_cliprdr *cliprdr,
                                                  int msg_flags,
                                                  struct stream *s,
                                                  int data_len)
{
    int stream_id;
    int size_low;
    int size_high;
    int bytes;
    int requested;
    int written;

    if (cliprdr->remote_file_pending == OHOS_CLIPRDR_REMOTE_FILE_NONE)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: ignored remote filecontents response without pending request");
        return 0;
    }
    if ((msg_flags & CB_RESPONSE_FAIL) != 0 || data_len < 4 ||
            !s_check_rem_and_log(s, data_len, "OHOS cliprdr file response"))
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: remote filecontents response failed flags=0x%08x len=%d",
            msg_flags, data_len);
        ohos_cliprdr_file_transfer_reset(cliprdr);
        return 0;
    }
    in_uint32_le(s, stream_id);
    if (stream_id != cliprdr->remote_file_stream_id)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: remote filecontents stream mismatch got=%d expected=%d",
            stream_id, cliprdr->remote_file_stream_id);
        ohos_cliprdr_file_transfer_reset(cliprdr);
        return 0;
    }
    if (cliprdr->remote_file_pending == OHOS_CLIPRDR_REMOTE_FILE_SIZE)
    {
        if (data_len < 12 || !s_check_rem(s, 8))
        {
            ohos_cliprdr_file_transfer_reset(cliprdr);
            return 1;
        }
        in_uint32_le(s, size_low);
        in_uint32_le(s, size_high);
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: remote file size response stream=%d size=%d high=%d",
            stream_id, size_low, size_high);
        if (size_high != 0 || size_low < 0)
        {
            ohos_cliprdr_file_transfer_reset(cliprdr);
            return 1;
        }
        return ohos_cliprdr_open_remote_file(cliprdr, size_low);
    }

    bytes = data_len - 4;
    if (cliprdr->remote_file_fd < 0 || bytes < 0)
    {
        ohos_cliprdr_file_transfer_reset(cliprdr);
        return 1;
    }
    written = g_file_write(cliprdr->remote_file_fd, s->p, bytes);
    if (written != bytes)
    {
        ohos_cliprdr_file_transfer_reset(cliprdr);
        return 1;
    }
    cliprdr->remote_file_offset += bytes;
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: remote file range response stream=%d index=%d bytes=%d offset=%d/%d",
        stream_id, cliprdr->remote_file_index, bytes,
        cliprdr->remote_file_offset,
        cliprdr->remote_files[cliprdr->remote_file_index].size);
    if (cliprdr->remote_file_offset >=
            cliprdr->remote_files[cliprdr->remote_file_index].size)
    {
        return ohos_cliprdr_complete_remote_file(cliprdr);
    }
    requested = cliprdr->remote_files[cliprdr->remote_file_index].size -
                cliprdr->remote_file_offset;
    if (requested > OHOS_CLIPRDR_MAX_FILE_CHUNK_BYTES)
    {
        requested = OHOS_CLIPRDR_MAX_FILE_CHUNK_BYTES;
    }
    return ohos_cliprdr_send_remote_filecontents_request(
               cliprdr, CB_FILECONTENTS_RANGE,
               cliprdr->remote_file_offset, requested);
}
