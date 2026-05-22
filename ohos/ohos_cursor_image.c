/**
 * HarmonyOS cursor image conversion.
 *
 * The OHOS cursor API can return a PixelMap for developer-defined cursors.
 * xrdp expects a compact 32bpp BGRA XOR bitmap and a 1bpp AND mask.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "log.h"
#include "os_calls.h"

#include <stdint.h>
#include <stddef.h>

#include "ohos_cursor_image.h"

#define OHOS_CURSOR_FNV_OFFSET 1469598103934665603ULL
#define OHOS_CURSOR_FNV_PRIME 1099511628211ULL

static int
ohos_cursor_pixel_format_bytes(int pixel_format)
{
    switch (pixel_format)
    {
        case PIXEL_FORMAT_BGRA_8888:
        case PIXEL_FORMAT_RGBA_8888:
            return 4;
        case PIXEL_FORMAT_RGB_888:
            return 3;
        case PIXEL_FORMAT_RGB_565:
            return 2;
        case PIXEL_FORMAT_ALPHA_8:
            return 1;
        default:
            return 0;
    }
}

static uint16_t
ohos_cursor_read_le16(const uint8_t *src)
{
    return (uint16_t)(src[0] | (src[1] << 8));
}

static void
ohos_cursor_set_mask_bit(char *mask, int bit_index)
{
    mask[bit_index / 8] = (char)(mask[bit_index / 8] |
                                 (0x80 >> (bit_index % 8)));
}

static uint64_t
ohos_cursor_hash_bytes(uint64_t hash, const void *data, size_t bytes)
{
    const uint8_t *cursor;
    size_t index;

    cursor = (const uint8_t *)data;
    for (index = 0; index < bytes; index++)
    {
        hash ^= cursor[index];
        hash *= OHOS_CURSOR_FNV_PRIME;
    }
    return hash;
}

static void
ohos_cursor_choose_hotspot(int style, int width, int height,
                           int *hot_x, int *hot_y)
{
    if (hot_x == 0 || hot_y == 0)
    {
        return;
    }

    *hot_x = 0;
    *hot_y = 0;

    switch (style)
    {
        case 13: /* CROSS */
        case 16: /* COLOR_SUCKER */
        case 21: /* MOVE */
        case 24: /* SCREENSHOT_CHOOSE */
        case 40: /* CURSOR_CROSS */
        case 41: /* CURSOR_CIRCLE */
        case 49: /* LASER_CURSOR */
        case 50: /* LASER_CURSOR_DOT */
        case 51: /* LASER_CURSOR_DOT_RED */
            *hot_x = width / 2;
            *hot_y = height / 2;
            break;

        case 22: /* RESIZE_LEFT_RIGHT */
        case 23: /* RESIZE_UP_DOWN */
        case 26: /* TEXT_CURSOR */
        case 39: /* HORIZONTAL_TEXT_CURSOR */
            *hot_x = width / 2;
            *hot_y = height / 2;
            break;

        case 17: /* HAND_GRABBING */
        case 18: /* HAND_OPEN */
        case 19: /* HAND_POINTING */
            *hot_x = width / 3;
            *hot_y = width / 8;
            break;

        default:
            break;
    }

    if (*hot_x < 0)
    {
        *hot_x = 0;
    }
    if (*hot_y < 0)
    {
        *hot_y = 0;
    }
    if (*hot_x >= width)
    {
        *hot_x = width - 1;
    }
    if (*hot_y >= height)
    {
        *hot_y = height - 1;
    }
}

void
ohos_cursor_image_init(struct ohos_cursor_image *image)
{
    if (image == 0)
    {
        return;
    }

    g_memset(image, 0, sizeof(*image));
    image->bpp = 32;
}

void
ohos_cursor_image_deinit(struct ohos_cursor_image *image)
{
    if (image == 0)
    {
        return;
    }

    g_free(image->data);
    g_free(image->mask);
    ohos_cursor_image_init(image);
}

static int
ohos_cursor_image_read_info(OH_PixelmapNative *pixelmap, uint32_t *width,
                            uint32_t *height, uint32_t *row_stride,
                            int32_t *pixel_format)
{
    OH_Pixelmap_ImageInfo *info = 0;
    Image_ErrorCode rc;

    if (pixelmap == 0 || width == 0 || height == 0 ||
        row_stride == 0 || pixel_format == 0)
    {
        return 1;
    }

    *width = 0;
    *height = 0;
    *row_stride = 0;
    *pixel_format = PIXEL_FORMAT_UNKNOWN;

    rc = OH_PixelmapImageInfo_Create(&info);
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapNative_GetImageInfo(pixelmap, info);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapImageInfo_GetWidth(info, width);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapImageInfo_GetHeight(info, height);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapImageInfo_GetRowStride(info, row_stride);
    }
    if (rc == IMAGE_SUCCESS)
    {
        rc = OH_PixelmapImageInfo_GetPixelFormat(info, pixel_format);
    }
    if (info != 0)
    {
        OH_PixelmapImageInfo_Release(info);
    }

    return rc == IMAGE_SUCCESS ? 0 : 1;
}

static int
ohos_cursor_image_from_pixelmap_impl(OH_PixelmapNative *pixelmap, int style,
                                     int target_width, int target_height,
                                     struct ohos_cursor_image *image)
{
    uint32_t width;
    uint32_t height;
    uint32_t out_width;
    uint32_t out_height;
    uint32_t row_stride;
    int32_t pixel_format;
    int bytes_per_pixel;
    size_t min_row_bytes;
    size_t pixel_bytes;
    size_t data_bytes;
    size_t mask_bytes;
    size_t buffer_size;
    size_t read_stride;
    uint8_t *pixels;
    uint32_t y;
    Image_ErrorCode rc;

    if (pixelmap == 0 || image == 0)
    {
        return 1;
    }

    ohos_cursor_image_init(image);
    if (ohos_cursor_image_read_info(pixelmap, &width, &height, &row_stride,
                                    &pixel_format) != 0)
    {
        return 1;
    }

    bytes_per_pixel = ohos_cursor_pixel_format_bytes(pixel_format);
    if (width == 0 || height == 0 || bytes_per_pixel == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cursor: unsupported cursor pixelmap format=%d size=%ux%u",
            pixel_format, width, height);
        return 1;
    }
    if (target_width > 0 && target_height > 0)
    {
        out_width = (uint32_t)target_width;
        out_height = (uint32_t)target_height;
    }
    else
    {
        out_width = width;
        out_height = height;
    }
    if (out_width == 0 || out_height == 0 ||
        out_width > OHOS_CURSOR_IMAGE_MAX_SIZE ||
        out_height > OHOS_CURSOR_IMAGE_MAX_SIZE)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cursor: unsupported cursor output size=%ux%u source=%ux%u",
            out_width, out_height, width, height);
        return 1;
    }

    min_row_bytes = (size_t)width * (size_t)bytes_per_pixel;
    if (row_stride < min_row_bytes)
    {
        row_stride = (uint32_t)min_row_bytes;
    }
    pixel_bytes = (size_t)row_stride * (size_t)height;
    data_bytes = (size_t)out_width * (size_t)out_height * 4U;
    mask_bytes = ((size_t)out_width * (size_t)out_height + 7U) / 8U;
    if (pixel_bytes == 0 || data_bytes == 0 ||
        pixel_bytes > 4U * 1024U * 1024U ||
        data_bytes > (size_t)OHOS_CURSOR_IMAGE_MAX_SIZE *
                     (size_t)OHOS_CURSOR_IMAGE_MAX_SIZE * 4U)
    {
        return 1;
    }

    pixels = (uint8_t *)g_malloc((int)pixel_bytes, 0);
    image->data = (char *)g_malloc((int)data_bytes, 0);
    image->mask = (char *)g_malloc((int)mask_bytes, 1);
    if (pixels == 0 || image->data == 0 || image->mask == 0)
    {
        g_free(pixels);
        ohos_cursor_image_deinit(image);
        return 1;
    }

    buffer_size = pixel_bytes;
    rc = OH_PixelmapNative_ReadPixels(pixelmap, pixels, &buffer_size);
    if (rc != IMAGE_SUCCESS)
    {
        g_free(pixels);
        ohos_cursor_image_deinit(image);
        return 1;
    }

    read_stride = row_stride;
    if (buffer_size < pixel_bytes)
    {
        size_t compact_bytes;

        compact_bytes = min_row_bytes * (size_t)height;
        if (buffer_size >= compact_bytes)
        {
            read_stride = min_row_bytes;
        }
        else
        {
            g_free(pixels);
            ohos_cursor_image_deinit(image);
            return 1;
        }
    }

    for (y = 0; y < out_height; y++)
    {
        const uint8_t *src;
        uint8_t *dst;
        uint32_t x;
        uint32_t dst_y;
        uint32_t src_y;

        src_y = (uint32_t)(((uint64_t)y * (uint64_t)height) /
                           (uint64_t)out_height);
        if (src_y >= height)
        {
            src_y = height - 1;
        }
        dst_y = out_height - y - 1;
        dst = (uint8_t *)image->data +
              ((size_t)dst_y * (size_t)out_width * 4U);
        for (x = 0; x < out_width; x++)
        {
            uint32_t src_x;
            uint8_t alpha;

            src_x = (uint32_t)(((uint64_t)x * (uint64_t)width) /
                               (uint64_t)out_width);
            if (src_x >= width)
            {
                src_x = width - 1;
            }
            src = pixels + ((size_t)src_y * read_stride) +
                  ((size_t)src_x * (size_t)bytes_per_pixel);
            alpha = 0xffU;
            switch (pixel_format)
            {
                case PIXEL_FORMAT_BGRA_8888:
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = src[3];
                    alpha = src[3];
                    src += 4;
                    break;

                case PIXEL_FORMAT_RGBA_8888:
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = src[3];
                    alpha = src[3];
                    src += 4;
                    break;

                case PIXEL_FORMAT_RGB_888:
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = 0xffU;
                    src += 3;
                    break;

                case PIXEL_FORMAT_RGB_565:
                {
                    uint16_t value;

                    value = ohos_cursor_read_le16(src);
                    dst[2] = (uint8_t)((((value >> 11U) & 0x1fU) * 255U) /
                                        31U);
                    dst[1] = (uint8_t)((((value >> 5U) & 0x3fU) * 255U) /
                                        63U);
                    dst[0] = (uint8_t)(((value & 0x1fU) * 255U) / 31U);
                    dst[3] = 0xffU;
                    src += 2;
                    break;
                }

                case PIXEL_FORMAT_ALPHA_8:
                    dst[0] = 0;
                    dst[1] = 0;
                    dst[2] = 0;
                    dst[3] = src[0];
                    alpha = src[0];
                    src += 1;
                    break;

                default:
                    break;
            }

            if (alpha == 0)
            {
                ohos_cursor_set_mask_bit(image->mask,
                                         (int)((dst_y * out_width) + x));
            }
            dst += 4;
        }
    }

    g_free(pixels);

    image->width = (int)out_width;
    image->height = (int)out_height;
    image->bpp = 32;
    ohos_cursor_choose_hotspot(style, image->width, image->height,
                               &image->hot_x, &image->hot_y);
    image->hash = OHOS_CURSOR_FNV_OFFSET;
    image->hash = ohos_cursor_hash_bytes(image->hash, image->data,
                                         data_bytes);
    image->hash = ohos_cursor_hash_bytes(image->hash, image->mask,
                                         mask_bytes);
    image->hash = ohos_cursor_hash_bytes(image->hash, &image->width,
                                         sizeof(image->width));
    image->hash = ohos_cursor_hash_bytes(image->hash, &image->height,
                                         sizeof(image->height));
    image->hash = ohos_cursor_hash_bytes(image->hash, &image->hot_x,
                                         sizeof(image->hot_x));
    image->hash = ohos_cursor_hash_bytes(image->hash, &image->hot_y,
                                         sizeof(image->hot_y));
    return 0;
}

int
ohos_cursor_image_from_pixelmap(OH_PixelmapNative *pixelmap, int style,
                                struct ohos_cursor_image *image)
{
    return ohos_cursor_image_from_pixelmap_impl(pixelmap, style, 0, 0, image);
}

int
ohos_cursor_image_from_pixelmap_target(OH_PixelmapNative *pixelmap,
                                       int style, int target_width,
                                       int target_height,
                                       struct ohos_cursor_image *image)
{
    return ohos_cursor_image_from_pixelmap_impl(pixelmap, style,
                                               target_width, target_height,
                                               image);
}
