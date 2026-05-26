/*
 * Image conversion and sandbox cache for OHOS cliprdr.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "log.h"
#include "os_calls.h"
#include "string_calls.h"
#include "xrdp_constants.h"

#include <database/pasteboard/oh_pasteboard.h>
#include <database/pasteboard/oh_pasteboard_err_code.h>
#include <database/udmf/udmf.h>
#include <database/udmf/udmf_err_code.h>
#include <database/udmf/uds.h>
#include <multimedia/image_framework/image/pixelmap_native.h>

static int
ohos_cliprdr_read_file_bytes(const char *path, char **data, int *bytes)
{
    int fd;
    int size;
    int read_bytes;

    if (data == 0 || bytes == 0)
    {
        return 1;
    }
    *data = 0;
    *bytes = 0;
    size = g_file_get_size(path);
    if (size <= 0 || size > OHOS_CLIPRDR_MAX_IMAGE_BYTES)
    {
        return 1;
    }
    fd = g_file_open_ro(path);
    if (fd < 0)
    {
        return 1;
    }
    *data = (char *)g_malloc(size, 0);
    if (*data == 0)
    {
        g_file_close(fd);
        return 1;
    }
    read_bytes = g_file_read(fd, *data, size);
    g_file_close(fd);
    if (read_bytes != size)
    {
        g_free(*data);
        *data = 0;
        return 1;
    }
    *bytes = size;
    return 0;
}

static int
ohos_cliprdr_read_uri_file_format(const char *uri, int *format_id)
{
    char *path;
    char sig[16];
    int fd;
    int bytes;
    int format;

    if (format_id != 0)
    {
        *format_id = 0;
    }
    path = ohos_cliprdr_uri_to_local_path(uri);
    if (path == 0)
    {
        return 0;
    }
    fd = g_file_open_ro(path);
    g_free(path);
    if (fd < 0)
    {
        return 0;
    }
    bytes = g_file_read(fd, sig, sizeof(sig));
    g_file_close(fd);
    format = ohos_cliprdr_image_format_from_signature(sig, bytes);
    if (format_id != 0)
    {
        *format_id = format;
    }
    return format != 0;
}

int
ohos_cliprdr_has_local_image(struct ohos_cliprdr *cliprdr, int *format_id)
{
    char *uri = 0;
    int image_format = 0;
    int has_image = 0;

    if (format_id != 0)
    {
        *format_id = 0;
    }
    if (ohos_cliprdr_pasteboard_read_uri(cliprdr, &uri) != 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local image probe found no URI data");
        return 0;
    }
    if (ohos_cliprdr_uri_decodes_as_image(uri))
    {
        has_image = 1;
    }
    else
    {
        has_image = ohos_cliprdr_read_uri_file_format(uri, &image_format);
    }
    if (format_id != 0)
    {
        *format_id = image_format;
    }
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: local image probe uri=%s decodable=%d file-format=%d(%s) has-image=%d",
        uri, has_image && image_format == 0, image_format,
        ohos_cliprdr_format_display_name(image_format), has_image);
    g_free(uri);
    return has_image;
}

int
ohos_cliprdr_read_local_image(struct ohos_cliprdr *cliprdr, int format_id,
                              char **data, int *bytes)
{
    char *uri = 0;
    char *path = 0;
    char *file_data = 0;
    char *bgra = 0;
    unsigned int width;
    unsigned int height;
    int file_bytes = 0;
    int file_format;

    if (data == 0 || bytes == 0)
    {
        return 1;
    }
    *data = 0;
    *bytes = 0;
    if (ohos_cliprdr_pasteboard_read_uri(cliprdr, &uri) != 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local image read failed: no URI data format=%d(%s)",
            format_id, ohos_cliprdr_format_display_name(format_id));
        return 1;
    }
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: local image read start format=%d(%s) uri=%s",
        format_id, ohos_cliprdr_format_display_name(format_id), uri);
    if (format_id == CF_DIB &&
            ohos_cliprdr_decode_image_uri_to_bgra(uri, &bgra,
                                                  &width, &height) == 0)
    {
        *data = ohos_cliprdr_bgra_to_dib(bgra, width, height, bytes);
        g_free(bgra);
        if (*data != 0)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cliprdr: copied local URI image directly uri=%s bytes=%d",
                uri, *bytes);
            g_free(uri);
            return 0;
        }
    }

    path = ohos_cliprdr_uri_to_local_path(uri);
    g_free(uri);
    if (path == 0 || ohos_cliprdr_read_file_bytes(path, &file_data,
            &file_bytes) != 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local image read failed: cannot read path=%s",
            path == 0 ? "" : path);
        g_free(path);
        return 1;
    }
    g_free(path);
    file_format = ohos_cliprdr_image_format_from_signature(file_data,
                                                           file_bytes);
    if (format_id != CF_DIB && file_format == format_id)
    {
        *data = file_data;
        *bytes = file_bytes;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local image read raw format=%d(%s) bytes=%d",
            file_format, ohos_cliprdr_format_display_name(file_format),
            file_bytes);
        return 0;
    }
    if (format_id != CF_DIB ||
            ohos_cliprdr_decode_image_data_to_bgra(file_data, file_bytes,
                                                   &bgra, &width,
                                                   &height) != 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: local image read failed: decode file-format=%d(%s) requested=%d(%s)",
            file_format, ohos_cliprdr_format_display_name(file_format),
            format_id, ohos_cliprdr_format_display_name(format_id));
        g_free(file_data);
        return 1;
    }
    g_free(file_data);
    *data = ohos_cliprdr_bgra_to_dib(bgra, width, height, bytes);
    g_free(bgra);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cliprdr: local image read converted to DIB %ux%u bytes=%d ok=%d",
        width, height, bytes == 0 ? 0 : *bytes, *data != 0);
    return *data == 0 ? 1 : 0;
}

int
ohos_cliprdr_write_remote_image(struct ohos_cliprdr *cliprdr,
                                int request_kind, const char *data,
                                int bytes)
{
    int rc = UDMF_E_OK;
    int source_format = OHOS_CLIPRDR_FORMAT_IMAGE_BMP;
    int source_bytes = bytes;
    char *source_data = (char *)data;
    char *owned_source = 0;
    char *cache_data = 0;
    int cache_bytes = 0;
    int cache_format = 0;
    char *bgra = 0;
    char *file_uri = 0;
    OH_PixelmapNative *pixelmap = 0;
    OH_UdsPixelMap *pixelmap_data = 0;
    OH_UdsFileUri *file_uri_data = 0;
    OH_UdmfRecord *record = 0;
    OH_UdmfData *udmf = 0;
    unsigned int width = 0;
    unsigned int height = 0;

    if (cliprdr == 0 || data == 0 || bytes <= 0)
    {
        return 1;
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: remote image write start kind=%s(%d) bytes=%d",
        ohos_cliprdr_request_kind_name(request_kind), request_kind, bytes);
    if (request_kind == OHOS_CLIPRDR_REQUEST_DIB)
    {
        source_format = CF_DIB;
        source_data = (char *)data;
        source_bytes = bytes;
        if (ohos_cliprdr_dib_to_bgra(data, bytes, &bgra, &width,
                                     &height) == 0 &&
                ohos_cliprdr_create_pixelmap_from_bgra(bgra, width, height,
                                                       &pixelmap) == 0)
        {
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.cliprdr: remote DIB parsed to PixelMap %ux%u bytes=%d",
                width, height, bytes);
            owned_source = ohos_cliprdr_dib_to_bmp(data, bytes, &cache_bytes);
            if (owned_source != 0)
            {
                cache_data = owned_source;
                cache_format = OHOS_CLIPRDR_FORMAT_IMAGE_BMP;
            }
        }
        g_free(bgra);
        bgra = 0;
    }
    else
    {
        source_format = ohos_cliprdr_image_format_from_signature(data, bytes);
        cache_data = source_data;
        cache_bytes = source_bytes;
        cache_format = source_format;
    }
    if (pixelmap == 0)
    {
        if (request_kind == OHOS_CLIPRDR_REQUEST_DIB)
        {
            owned_source = ohos_cliprdr_dib_to_bmp(data, bytes, &source_bytes);
            source_data = owned_source;
            source_format = OHOS_CLIPRDR_FORMAT_IMAGE_BMP;
            cache_data = owned_source;
            cache_bytes = source_bytes;
            cache_format = source_format;
        }
        if (source_data != 0 && source_format != 0)
        {
            (void)ohos_cliprdr_decode_image_data_to_pixelmap(source_data,
                                                             source_bytes,
                                                             &pixelmap,
                                                             &width,
                                                             &height);
        }
    }
    if (pixelmap == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cliprdr: remote image write decode failed kind=%s source-format=%d(%s) source-bytes=%d",
            ohos_cliprdr_request_kind_name(request_kind), source_format,
            ohos_cliprdr_format_display_name(source_format), source_bytes);
        g_free(owned_source);
        return 1;
    }
    if (cache_data != 0 && cache_format != 0 && cache_bytes > 0)
    {
        file_uri = ohos_cliprdr_cache_remote_image(cache_format, cache_data,
                                                   cache_bytes);
    }
    pixelmap_data = OH_UdsPixelMap_Create();
    file_uri_data = file_uri == 0 ? 0 : OH_UdsFileUri_Create();
    record = OH_UdmfRecord_Create();
    udmf = OH_UdmfData_Create();
    if (pixelmap_data == 0 || record == 0 || udmf == 0 ||
            (file_uri != 0 && file_uri_data == 0))
    {
        goto fail;
    }
    rc = ohos_cliprdr_udmf_make_cross_app(udmf);
    if (rc != UDMF_E_OK)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: UDMF image cross-app share setup failed rc=%d",
            rc);
        goto fail;
    }
    rc = OH_UdsPixelMap_SetPixelMap(pixelmap_data, pixelmap);
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdmfRecord_AddPixelMap(record, pixelmap_data);
    }
    if (rc == UDMF_E_OK && file_uri != 0)
    {
        rc = OH_UdsFileUri_SetFileUri(file_uri_data, file_uri);
    }
    if (rc == UDMF_E_OK && file_uri != 0)
    {
        rc = OH_UdmfRecord_AddFileUri(record, file_uri_data);
    }
    if (rc == UDMF_E_OK)
    {
        rc = OH_UdmfData_AddRecord(udmf, record);
    }
    if (rc != UDMF_E_OK)
    {
        goto fail;
    }
    ohos_cliprdr_pasteboard_begin_remote_write(cliprdr);
    rc = OH_Pasteboard_SetData(cliprdr->pasteboard, udmf);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.cliprdr: Pasteboard SetData image kind=%s source=%d(%s) record=pixelmap%s %ux%u bytes=%d uri=%s status=%d(%s)",
        ohos_cliprdr_request_kind_name(request_kind), source_format,
        ohos_cliprdr_format_display_name(source_format),
        file_uri == 0 ? "" : "+fileUri", width, height, source_bytes,
        file_uri == 0 ? "" : file_uri,
        rc, ohos_cliprdr_pasteboard_status_name(rc));
    if (rc != ERR_OK)
    {
        ohos_cliprdr_pasteboard_cancel_remote_write(cliprdr);
        goto fail;
    }
    cliprdr->pasteboard_writes++;
    OH_UdsPixelMap_Destroy(pixelmap_data);
    if (file_uri_data != 0)
    {
        OH_UdsFileUri_Destroy(file_uri_data);
    }
    OH_UdmfRecord_Destroy(record);
    OH_UdmfData_Destroy(udmf);
    OH_PixelmapNative_Release(pixelmap);
    g_free(file_uri);
    g_free(owned_source);
    return 0;

fail:
    cliprdr->errors++;
    if (pixelmap_data != 0)
    {
        OH_UdsPixelMap_Destroy(pixelmap_data);
    }
    if (file_uri_data != 0)
    {
        OH_UdsFileUri_Destroy(file_uri_data);
    }
    if (record != 0)
    {
        OH_UdmfRecord_Destroy(record);
    }
    if (udmf != 0)
    {
        OH_UdmfData_Destroy(udmf);
    }
    if (pixelmap != 0)
    {
        OH_PixelmapNative_Release(pixelmap);
    }
    g_free(file_uri);
    g_free(owned_source);
    return 1;
}
