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
