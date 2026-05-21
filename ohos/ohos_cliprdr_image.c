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

typedef struct OH_ImageSourceNative OH_ImageSourceNative;
typedef struct OH_DecodingOptions OH_DecodingOptions;

Image_ErrorCode
OH_DecodingOptions_Create(OH_DecodingOptions **options);

Image_ErrorCode
OH_DecodingOptions_SetPixelFormat(OH_DecodingOptions *options,
                                  int32_t pixel_format);

Image_ErrorCode
OH_DecodingOptions_Release(OH_DecodingOptions *options);

Image_ErrorCode
OH_ImageSourceNative_CreateFromData(uint8_t *data, size_t data_size,
                                    OH_ImageSourceNative **source);

Image_ErrorCode
OH_ImageSourceNative_CreatePixelmap(OH_ImageSourceNative *source,
                                    OH_DecodingOptions *options,
                                    OH_PixelmapNative **pixelmap);

Image_ErrorCode
OH_ImageSourceNative_Release(OH_ImageSourceNative *source);

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
ohos_cliprdr_decode_to_pixelmap(const char *data, int bytes,
                                OH_PixelmapNative **pixelmap,
                                unsigned int *width, unsigned int *height)
{
    Image_ErrorCode rc;
    OH_ImageSourceNative *source = 0;
    OH_DecodingOptions *options = 0;
    OH_Pixelmap_ImageInfo *info = 0;

    if (pixelmap == 0 || data == 0 || bytes <= 0)
    {
        return 1;
    }
    *pixelmap = 0;
    rc = OH_ImageSourceNative_CreateFromData((uint8_t *)data, (size_t)bytes,
                                             &source);
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_DecodingOptions_Create(&options);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_DecodingOptions_SetPixelFormat(options, PIXEL_FORMAT_BGRA_8888);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_ImageSourceNative_CreatePixelmap(source, options, pixelmap);
    }
    if (rc == IMAGE_SUCCESS && width != 0 && height != 0)
    {
        rc = OH_PixelmapImageInfo_Create(&info);
        if (rc == IMAGE_SUCCESS)
        {
            (void)OH_PixelmapNative_GetImageInfo(*pixelmap, info);
            (void)OH_PixelmapImageInfo_GetWidth(info, width);
            (void)OH_PixelmapImageInfo_GetHeight(info, height);
        }
    }
    if (info != 0)
    {
        OH_PixelmapImageInfo_Release(info);
    }
    if (options != 0)
    {
        OH_DecodingOptions_Release(options);
    }
    if (source != 0)
    {
        OH_ImageSourceNative_Release(source);
    }
    if (rc != IMAGE_SUCCESS || *pixelmap == 0)
    {
        if (*pixelmap != 0)
        {
            OH_PixelmapNative_Release(*pixelmap);
            *pixelmap = 0;
        }
        return 1;
    }
    return 0;
}

static int
ohos_cliprdr_decode_to_bgra(const char *data, int bytes, char **bgra,
                            unsigned int *width, unsigned int *height)
{
    OH_PixelmapNative *pixelmap = 0;
    OH_Pixelmap_ImageInfo *info = 0;
    Image_ErrorCode rc;
    unsigned int row_stride = 0;
    unsigned int compact_stride;
    size_t buffer_size;

    if (bgra == 0 || width == 0 || height == 0)
    {
        return 1;
    }
    *bgra = 0;
    *width = 0;
    *height = 0;
    if (ohos_cliprdr_decode_to_pixelmap(data, bytes, &pixelmap, width,
                                        height) != 0)
    {
        return 1;
    }
    rc = OH_PixelmapImageInfo_Create(&info);
    if (rc == IMAGE_SUCCESS)
    {
        (void)OH_PixelmapNative_GetImageInfo(pixelmap, info);
        (void)OH_PixelmapImageInfo_GetRowStride(info, &row_stride);
    }
    if (info != 0)
    {
        OH_PixelmapImageInfo_Release(info);
    }
    if (*width == 0 || *height == 0 || *width > 8192 || *height > 8192)
    {
        OH_PixelmapNative_Release(pixelmap);
        return 1;
    }
    compact_stride = *width * 4;
    if (row_stride < compact_stride)
    {
        row_stride = compact_stride;
    }
    buffer_size = (size_t)row_stride * (size_t)*height;
    if (buffer_size > OHOS_CLIPRDR_MAX_IMAGE_BYTES)
    {
        OH_PixelmapNative_Release(pixelmap);
        return 1;
    }
    *bgra = (char *)g_malloc((int)buffer_size, 0);
    if (*bgra == 0)
    {
        OH_PixelmapNative_Release(pixelmap);
        return 1;
    }
    rc = OH_PixelmapNative_ReadPixels(pixelmap, (uint8_t *)*bgra,
                                      &buffer_size);
    OH_PixelmapNative_Release(pixelmap);
    if (rc != IMAGE_SUCCESS)
    {
        g_free(*bgra);
        *bgra = 0;
        return 1;
    }
    if (row_stride > compact_stride)
    {
        char *compact;
        unsigned int y;

        compact = (char *)g_malloc((int)((size_t)compact_stride *
                                         (size_t)*height), 0);
        if (compact == 0)
        {
            g_free(*bgra);
            *bgra = 0;
            return 1;
        }
        for (y = 0; y < *height; y++)
        {
            g_memcpy(compact + (y * compact_stride),
                     *bgra + (y * row_stride), compact_stride);
        }
        g_free(*bgra);
        *bgra = compact;
    }
    return 0;
}

int
ohos_cliprdr_has_local_image(struct ohos_cliprdr *cliprdr, int *format_id)
{
    char *uri = 0;
    char *path = 0;
    char sig[16];
    int fd;
    int bytes;

    if (format_id != 0)
    {
        *format_id = 0;
    }
    if (ohos_cliprdr_pasteboard_read_uri(cliprdr, &uri) != 0)
    {
        return 0;
    }
    path = ohos_cliprdr_uri_to_local_path(uri);
    g_free(uri);
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
    if (format_id != 0)
    {
        *format_id = ohos_cliprdr_image_format_from_signature(sig, bytes);
    }
    return bytes > 0 && format_id != 0 && *format_id != 0;
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
        return 1;
    }
    path = ohos_cliprdr_uri_to_local_path(uri);
    g_free(uri);
    if (path == 0 || ohos_cliprdr_read_file_bytes(path, &file_data,
            &file_bytes) != 0)
    {
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
        return 0;
    }
    if (format_id != CF_DIB ||
            ohos_cliprdr_decode_to_bgra(file_data, file_bytes, &bgra,
                                        &width, &height) != 0)
    {
        g_free(file_data);
        return 1;
    }
    g_free(file_data);
    *data = ohos_cliprdr_bgra_to_dib(bgra, width, height, bytes);
    g_free(bgra);
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
    if (request_kind == OHOS_CLIPRDR_REQUEST_DIB)
    {
        owned_source = ohos_cliprdr_dib_to_bmp(data, bytes, &source_bytes);
        source_data = owned_source;
        source_format = OHOS_CLIPRDR_FORMAT_IMAGE_BMP;
    }
    else
    {
        source_format = ohos_cliprdr_image_format_from_signature(data, bytes);
    }
    if (source_data == 0 || source_format == 0 ||
            ohos_cliprdr_decode_to_pixelmap(source_data, source_bytes,
                                           &pixelmap, &width, &height) != 0)
    {
        g_free(owned_source);
        return 1;
    }
    file_uri = ohos_cliprdr_cache_remote_image(source_format, source_data,
                                               source_bytes);
    pixelmap_data = OH_UdsPixelMap_Create();
    file_uri_data = file_uri == 0 ? 0 : OH_UdsFileUri_Create();
    record = OH_UdmfRecord_Create();
    udmf = OH_UdmfData_Create();
    if (pixelmap_data == 0 || record == 0 || udmf == 0 ||
            (file_uri != 0 && file_uri_data == 0))
    {
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
        "xrdp.ohos.cliprdr: Pasteboard SetData image %ux%u bytes=%d uri=%s status=%d(%s)",
        width, height, source_bytes, file_uri == 0 ? "" : file_uri,
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
