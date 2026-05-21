/*
 * Sandbox file cache for image clipboard payloads.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "os_calls.h"
#include "string_calls.h"

char *
ohos_cliprdr_cache_remote_image(int format_id, const char *data, int bytes)
{
    char dir[256];
    char path[384];
    char *uri;
    int fd;
    int written;

    if (data == 0 || bytes <= 0)
    {
        return 0;
    }
    (void)g_create_dir(OHOS_CLIPRDR_SANDBOX_FILES_DIR);
    g_snprintf(dir, sizeof(dir), "%s/%s", OHOS_CLIPRDR_SANDBOX_FILES_DIR,
               OHOS_CLIPRDR_CACHE_DIR_NAME);
    (void)g_create_dir(dir);
    g_snprintf(path, sizeof(path), "%s/xrdp-clip-%u%s", dir,
               g_get_elapsed_ms(), ohos_cliprdr_image_extension(format_id));
    fd = g_file_open_ex(path, 0, 1, 1, 1);
    if (fd < 0)
    {
        return 0;
    }
    written = g_file_write(fd, data, bytes);
    g_file_close(fd);
    if (written != bytes)
    {
        return 0;
    }
    uri = (char *)g_malloc(g_strlen(path) + 8, 1);
    if (uri != 0)
    {
        g_sprintf(uri, "file://%s", path);
    }
    return uri;
}
