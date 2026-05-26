/*
 * DIB/BMP helpers for OHOS cliprdr image transfer.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "os_calls.h"

#define BI_RGB 0
#define BI_BITFIELDS 3

static int
ohos_cliprdr_read_le16(const char *data)
{
    const unsigned char *p = (const unsigned char *)data;
    return p[0] | (p[1] << 8);
}

static int
ohos_cliprdr_read_le32(const char *data)
{
    const unsigned char *p = (const unsigned char *)data;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static unsigned int
ohos_cliprdr_read_u32(const char *data)
{
    const unsigned char *p = (const unsigned char *)data;
    return ((unsigned int)p[0]) | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int
ohos_cliprdr_read_s32(const char *data)
{
    unsigned int value = ohos_cliprdr_read_u32(data);

    if ((value & 0x80000000U) != 0U)
    {
        return -(int)(0x100000000ULL - (unsigned long long)value);
    }
    return (int)value;
}

static unsigned char
ohos_cliprdr_mask_to_u8(unsigned int value, unsigned int mask)
{
    unsigned int shift = 0;
    unsigned int bits = 0;
    unsigned int max;
    unsigned int component;

    if (mask == 0)
    {
        return 0;
    }
    while (((mask >> shift) & 1U) == 0U && shift < 32U)
    {
        shift++;
    }
    while (shift + bits < 32U &&
           ((mask >> (shift + bits)) & 1U) != 0U)
    {
        bits++;
    }
    if (bits == 0)
    {
        return 0;
    }
    component = (value & mask) >> shift;
    max = (1U << bits) - 1U;
    return (unsigned char)((component * 255U + (max / 2U)) / max);
}

static void
ohos_cliprdr_write_le16(char *data, int value)
{
    data[0] = (char)(value & 0xff);
    data[1] = (char)((value >> 8) & 0xff);
}

static void
ohos_cliprdr_write_le32(char *data, int value)
{
    data[0] = (char)(value & 0xff);
    data[1] = (char)((value >> 8) & 0xff);
    data[2] = (char)((value >> 16) & 0xff);
    data[3] = (char)((value >> 24) & 0xff);
}

char *
ohos_cliprdr_bgra_to_dib(const char *bgra, unsigned int width,
                         unsigned int height, int *out_bytes)
{
    int row_bytes;
    int image_bytes;
    int total;
    char *dib;
    char *pixels;
    unsigned int y;

    if (out_bytes == 0 || bgra == 0 || width == 0 || height == 0 ||
            width > 8192 || height > 8192)
    {
        return 0;
    }
    row_bytes = (int)width * 4;
    image_bytes = row_bytes * (int)height;
    if (image_bytes > OHOS_CLIPRDR_MAX_IMAGE_BYTES - 40)
    {
        return 0;
    }
    total = 40 + image_bytes;
    dib = (char *)g_malloc(total, 1);
    if (dib == 0)
    {
        return 0;
    }
    ohos_cliprdr_write_le32(dib, 40);
    ohos_cliprdr_write_le32(dib + 4, (int)width);
    ohos_cliprdr_write_le32(dib + 8, (int)height);
    ohos_cliprdr_write_le16(dib + 12, 1);
    ohos_cliprdr_write_le16(dib + 14, 32);
    ohos_cliprdr_write_le32(dib + 16, BI_RGB);
    ohos_cliprdr_write_le32(dib + 20, image_bytes);
    pixels = dib + 40;
    for (y = 0; y < height; y++)
    {
        const char *src = bgra + ((height - 1 - y) * row_bytes);
        char *dst = pixels + (y * row_bytes);
        g_memcpy(dst, src, row_bytes);
    }
    *out_bytes = total;
    return dib;
}

char *
ohos_cliprdr_dib_to_bmp(const char *dib, int dib_bytes, int *out_bytes)
{
    int header_size;
    int bit_count;
    int compression;
    int off_bits;
    int total;
    char *bmp;

    if (dib == 0 || out_bytes == 0 || dib_bytes < 40 ||
            dib_bytes > OHOS_CLIPRDR_MAX_IMAGE_BYTES)
    {
        return 0;
    }
    header_size = ohos_cliprdr_read_le32(dib);
    bit_count = ohos_cliprdr_read_le16(dib + 14);
    compression = ohos_cliprdr_read_le32(dib + 16);
    if (header_size < 40 || header_size > dib_bytes ||
            (bit_count != 24 && bit_count != 32))
    {
        return 0;
    }
    off_bits = 14 + header_size;
    if (compression == BI_BITFIELDS && header_size == 40 && dib_bytes >= 52)
    {
        off_bits += 12;
    }
    total = 14 + dib_bytes;
    bmp = (char *)g_malloc(total, 1);
    if (bmp == 0)
    {
        return 0;
    }
    bmp[0] = 'B';
    bmp[1] = 'M';
    ohos_cliprdr_write_le32(bmp + 2, total);
    ohos_cliprdr_write_le32(bmp + 10, off_bits);
    g_memcpy(bmp + 14, dib, dib_bytes);
    *out_bytes = total;
    return bmp;
}

int
ohos_cliprdr_dib_to_bgra(const char *dib, int dib_bytes, char **bgra,
                         unsigned int *width, unsigned int *height)
{
    int header_size;
    int bit_count;
    int compression;
    int planes;
    int signed_width;
    int signed_height;
    int top_down;
    int color_count = 0;
    int pixel_offset;
    unsigned int abs_height;
    unsigned int row_bytes;
    unsigned int image_bytes;
    unsigned int red_mask = 0;
    unsigned int green_mask = 0;
    unsigned int blue_mask = 0;
    unsigned int alpha_mask = 0;
    unsigned int y;
    int has_alpha = 0;

    if (bgra == 0 || width == 0 || height == 0)
    {
        return 1;
    }
    *bgra = 0;
    *width = 0;
    *height = 0;
    if (dib == 0 || dib_bytes < 40 || dib_bytes > OHOS_CLIPRDR_MAX_IMAGE_BYTES)
    {
        return 1;
    }

    header_size = ohos_cliprdr_read_le32(dib);
    signed_width = ohos_cliprdr_read_s32(dib + 4);
    signed_height = ohos_cliprdr_read_s32(dib + 8);
    planes = ohos_cliprdr_read_le16(dib + 12);
    bit_count = ohos_cliprdr_read_le16(dib + 14);
    compression = ohos_cliprdr_read_le32(dib + 16);
    if (header_size < 40 || header_size > dib_bytes ||
            signed_width <= 0 || signed_height == 0 ||
            planes != 1 || (bit_count != 24 && bit_count != 32) ||
            (compression != BI_RGB && compression != BI_BITFIELDS))
    {
        return 1;
    }
    top_down = signed_height < 0;
    abs_height = top_down ? (unsigned int)(-signed_height) :
                 (unsigned int)signed_height;
    if ((unsigned int)signed_width > 8192U || abs_height > 8192U)
    {
        return 1;
    }

    if (header_size >= 40 && dib_bytes >= 36)
    {
        color_count = ohos_cliprdr_read_le32(dib + 32);
    }
    if (compression == BI_BITFIELDS)
    {
        if (header_size >= 56)
        {
            red_mask = ohos_cliprdr_read_u32(dib + 40);
            green_mask = ohos_cliprdr_read_u32(dib + 44);
            blue_mask = ohos_cliprdr_read_u32(dib + 48);
            alpha_mask = ohos_cliprdr_read_u32(dib + 52);
            pixel_offset = header_size;
        }
        else if (header_size == 40 && dib_bytes >= 52)
        {
            red_mask = ohos_cliprdr_read_u32(dib + 40);
            green_mask = ohos_cliprdr_read_u32(dib + 44);
            blue_mask = ohos_cliprdr_read_u32(dib + 48);
            pixel_offset = 52;
        }
        else
        {
            return 1;
        }
    }
    else
    {
        red_mask = 0x00FF0000U;
        green_mask = 0x0000FF00U;
        blue_mask = 0x000000FFU;
        alpha_mask = bit_count == 32 ? 0xFF000000U : 0U;
        pixel_offset = header_size;
    }
    if (color_count > 0)
    {
        pixel_offset += color_count * 4;
    }

    row_bytes = (((unsigned int)signed_width * (unsigned int)bit_count + 31U) /
                 32U) * 4U;
    image_bytes = row_bytes * abs_height;
    if (row_bytes == 0 || image_bytes / row_bytes != abs_height ||
            pixel_offset < 0 || pixel_offset > dib_bytes ||
            image_bytes > (unsigned int)(dib_bytes - pixel_offset) ||
            image_bytes > OHOS_CLIPRDR_MAX_IMAGE_BYTES)
    {
        return 1;
    }

    *bgra = (char *)g_malloc((int)((unsigned int)signed_width *
                                   abs_height * 4U), 0);
    if (*bgra == 0)
    {
        return 1;
    }

    for (y = 0; y < abs_height; y++)
    {
        unsigned int src_y = top_down ? y : (abs_height - 1U - y);
        const unsigned char *src =
            (const unsigned char *)dib + pixel_offset + (src_y * row_bytes);
        unsigned char *dst =
            (unsigned char *)(*bgra) + (y * (unsigned int)signed_width * 4U);
        unsigned int x;

        for (x = 0; x < (unsigned int)signed_width; x++)
        {
            if (bit_count == 24)
            {
                if (compression == BI_BITFIELDS)
                {
                    unsigned int value = ((unsigned int)src[0]) |
                                         ((unsigned int)src[1] << 8) |
                                         ((unsigned int)src[2] << 16);
                    dst[0] = ohos_cliprdr_mask_to_u8(value, blue_mask);
                    dst[1] = ohos_cliprdr_mask_to_u8(value, green_mask);
                    dst[2] = ohos_cliprdr_mask_to_u8(value, red_mask);
                }
                else
                {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                }
                dst[3] = 0xFFU;
                src += 3;
            }
            else
            {
                unsigned int value = ohos_cliprdr_read_u32((const char *)src);
                dst[0] = ohos_cliprdr_mask_to_u8(value, blue_mask);
                dst[1] = ohos_cliprdr_mask_to_u8(value, green_mask);
                dst[2] = ohos_cliprdr_mask_to_u8(value, red_mask);
                dst[3] = alpha_mask == 0 ? src[3] :
                         ohos_cliprdr_mask_to_u8(value, alpha_mask);
                has_alpha = has_alpha || dst[3] != 0;
                src += 4;
            }
            dst += 4;
        }
    }
    if (bit_count == 32 && !has_alpha)
    {
        unsigned char *dst = (unsigned char *)(*bgra);
        unsigned int pixels = (unsigned int)signed_width * abs_height;
        unsigned int index;

        for (index = 0; index < pixels; index++)
        {
            dst[(index * 4U) + 3U] = 0xFFU;
        }
    }
    *width = (unsigned int)signed_width;
    *height = abs_height;
    return 0;
}
