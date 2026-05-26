/*
 * OHOS ImageSource and PixelMap decoding helpers for cliprdr.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "os_calls.h"
#include "string_calls.h"

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
OH_PixelmapInitializationOptions_Create(
    OH_Pixelmap_InitializationOptions **options);

Image_ErrorCode
OH_PixelmapInitializationOptions_SetWidth(
    OH_Pixelmap_InitializationOptions *options, uint32_t width);

Image_ErrorCode
OH_PixelmapInitializationOptions_SetHeight(
    OH_Pixelmap_InitializationOptions *options, uint32_t height);

Image_ErrorCode
OH_PixelmapInitializationOptions_SetPixelFormat(
    OH_Pixelmap_InitializationOptions *options, int32_t pixel_format);

Image_ErrorCode
OH_PixelmapInitializationOptions_SetAlphaType(
    OH_Pixelmap_InitializationOptions *options, int32_t alpha_type);

Image_ErrorCode
OH_PixelmapInitializationOptions_Release(
    OH_Pixelmap_InitializationOptions *options);

Image_ErrorCode
OH_PixelmapNative_CreatePixelmap(uint8_t *data, size_t data_size,
                                 OH_Pixelmap_InitializationOptions *options,
                                 OH_PixelmapNative **pixelmap);

Image_ErrorCode
OH_ImageSourceNative_CreateFromUri(char *uri, size_t uri_size,
                                   OH_ImageSourceNative **source);

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
ohos_cliprdr_source_to_pixelmap(OH_ImageSourceNative *source,
                                OH_PixelmapNative **pixelmap,
                                unsigned int *width, unsigned int *height)
{
    Image_ErrorCode rc;
    OH_DecodingOptions *options = 0;
    OH_Pixelmap_ImageInfo *info = 0;

    if (source == 0 || pixelmap == 0)
    {
        return 1;
    }
    *pixelmap = 0;
    rc = OH_DecodingOptions_Create(&options);
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

int
ohos_cliprdr_create_pixelmap_from_bgra(const char *bgra, unsigned int width,
                                       unsigned int height,
                                       OH_PixelmapNative **pixelmap)
{
    Image_ErrorCode rc;
    OH_Pixelmap_InitializationOptions *options = 0;
    size_t data_size;

    if (bgra == 0 || width == 0 || height == 0 || pixelmap == 0 ||
            width > 8192 || height > 8192)
    {
        return 1;
    }
    data_size = (size_t)width * (size_t)height * 4U;
    if (data_size > OHOS_CLIPRDR_MAX_IMAGE_BYTES)
    {
        return 1;
    }
    *pixelmap = 0;
    rc = OH_PixelmapInitializationOptions_Create(&options);
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapInitializationOptions_SetWidth(options, width);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapInitializationOptions_SetHeight(options, height);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapInitializationOptions_SetPixelFormat(
            options, PIXEL_FORMAT_BGRA_8888);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapInitializationOptions_SetAlphaType(
            options, PIXELMAP_ALPHA_TYPE_UNPREMULTIPLIED);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapNative_CreatePixelmap((uint8_t *)bgra, data_size,
                                              options, pixelmap);
    }
    if (options != 0)
    {
        OH_PixelmapInitializationOptions_Release(options);
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

int
ohos_cliprdr_decode_image_data_to_pixelmap(const char *data, int bytes,
                                           OH_PixelmapNative **pixelmap,
                                           unsigned int *width,
                                           unsigned int *height)
{
    Image_ErrorCode rc;
    OH_ImageSourceNative *source = 0;
    int rv;

    if (pixelmap == 0 || data == 0 || bytes <= 0)
    {
        return 1;
    }
    rc = OH_ImageSourceNative_CreateFromData((uint8_t *)data, (size_t)bytes,
                                             &source);
    if (rc != IMAGE_SUCCESS || source == 0)
    {
        return 1;
    }
    rv = ohos_cliprdr_source_to_pixelmap(source, pixelmap, width, height);
    OH_ImageSourceNative_Release(source);
    return rv;
}

static int
ohos_cliprdr_decode_uri_to_pixelmap(const char *uri,
                                    OH_PixelmapNative **pixelmap,
                                    unsigned int *width,
                                    unsigned int *height)
{
    Image_ErrorCode rc;
    OH_ImageSourceNative *source = 0;
    int rv;

    if (pixelmap == 0 || uri == 0 || uri[0] == '\0')
    {
        return 1;
    }
    rc = OH_ImageSourceNative_CreateFromUri((char *)uri, (size_t)g_strlen(uri),
                                            &source);
    if (rc != IMAGE_SUCCESS || source == 0)
    {
        return 1;
    }
    rv = ohos_cliprdr_source_to_pixelmap(source, pixelmap, width, height);
    OH_ImageSourceNative_Release(source);
    return rv;
}

static int
ohos_cliprdr_pixelmap_to_bgra(OH_PixelmapNative *pixelmap, char **bgra,
                              unsigned int *width, unsigned int *height)
{
    OH_Pixelmap_ImageInfo *info = 0;
    Image_ErrorCode rc;
    unsigned int row_stride = 0;
    unsigned int compact_stride;
    size_t buffer_size;

    if (pixelmap == 0 || bgra == 0 || width == 0 || height == 0)
    {
        return 1;
    }
    *bgra = 0;
    *width = 0;
    *height = 0;
    rc = OH_PixelmapImageInfo_Create(&info);
    if (rc == IMAGE_SUCCESS)
    {
        (void)OH_PixelmapNative_GetImageInfo(pixelmap, info);
        (void)OH_PixelmapImageInfo_GetWidth(info, width);
        (void)OH_PixelmapImageInfo_GetHeight(info, height);
        (void)OH_PixelmapImageInfo_GetRowStride(info, &row_stride);
    }
    if (info != 0)
    {
        OH_PixelmapImageInfo_Release(info);
    }
    if (*width == 0 || *height == 0 || *width > 8192 || *height > 8192)
    {
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
        return 1;
    }
    *bgra = (char *)g_malloc((int)buffer_size, 0);
    if (*bgra == 0)
    {
        return 1;
    }
    rc = OH_PixelmapNative_ReadPixels(pixelmap, (uint8_t *)*bgra,
                                      &buffer_size);
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
ohos_cliprdr_decode_image_data_to_bgra(const char *data, int bytes,
                                       char **bgra, unsigned int *width,
                                       unsigned int *height)
{
    OH_PixelmapNative *pixelmap = 0;
    int rv;

    if (ohos_cliprdr_decode_image_data_to_pixelmap(data, bytes, &pixelmap,
                                                   width, height) != 0)
    {
        return 1;
    }
    rv = ohos_cliprdr_pixelmap_to_bgra(pixelmap, bgra, width, height);
    OH_PixelmapNative_Release(pixelmap);
    return rv;
}

int
ohos_cliprdr_decode_image_uri_to_bgra(const char *uri, char **bgra,
                                      unsigned int *width,
                                      unsigned int *height)
{
    OH_PixelmapNative *pixelmap = 0;
    int rv;

    if (ohos_cliprdr_decode_uri_to_pixelmap(uri, &pixelmap, width,
                                            height) != 0)
    {
        return 1;
    }
    rv = ohos_cliprdr_pixelmap_to_bgra(pixelmap, bgra, width, height);
    OH_PixelmapNative_Release(pixelmap);
    return rv;
}

int
ohos_cliprdr_uri_decodes_as_image(const char *uri)
{
    OH_PixelmapNative *pixelmap = 0;
    unsigned int width = 0;
    unsigned int height = 0;

    if (ohos_cliprdr_decode_uri_to_pixelmap(uri, &pixelmap, &width,
                                            &height) != 0)
    {
        return 0;
    }
    OH_PixelmapNative_Release(pixelmap);
    return width > 0 && height > 0;
}
