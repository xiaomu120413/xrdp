/**
 * HarmonyOS xrdp backend RDPGFX AVC420 frame path.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdint.h>
#include <sys/mman.h>

#include "arch.h"
#include "log.h"
#include "os_calls.h"
#include "xrdp_egfx.h"
#include "xup.h"

#include "ohos_gfx_avc420.h"

#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

static int
ohos_clip_u8(int value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > 255)
    {
        return 255;
    }
    return value;
}

static void
ohos_bgr_to_yuv709_full(int b, int g, int r,
                        unsigned char *y, unsigned char *u, unsigned char *v)
{
    int yy;
    int uu;
    int vv;

    yy = (54 * r + 183 * g + 19 * b + 128) / 256;
    uu = (-29 * r - 99 * g + 128 * b) / 256 + 128;
    vv = (128 * r - 116 * g - 12 * b) / 256 + 128;

    *y = (unsigned char)ohos_clip_u8(yy);
    *u = (unsigned char)ohos_clip_u8(uu);
    *v = (unsigned char)ohos_clip_u8(vv);
}

static int
ohos_bgra_to_nv12(const char *bgra, int frame_width, int frame_height,
                  int width, int height, unsigned char *nv12)
{
    unsigned char *y_plane;
    unsigned char *uv_plane;
    int x;
    int y;

    if (bgra == 0 || nv12 == 0 || frame_width < width ||
            frame_height < height || width <= 0 || height <= 0 ||
            (width & 1) != 0 || (height & 1) != 0)
    {
        return 1;
    }

    y_plane = nv12;
    uv_plane = nv12 + ((size_t)width * (size_t)height);

    for (y = 0; y < height; ++y)
    {
        const unsigned char *src = (const unsigned char *)bgra +
                                   ((size_t)y * (size_t)frame_width * 4U);
        unsigned char *dst_y = y_plane + ((size_t)y * (size_t)width);

        for (x = 0; x < width; ++x)
        {
            unsigned char yy;
            unsigned char uu;
            unsigned char vv;
            const unsigned char *px = src + ((size_t)x * 4U);

            ohos_bgr_to_yuv709_full(px[0], px[1], px[2], &yy, &uu, &vv);
            dst_y[x] = yy;
        }
    }

    for (y = 0; y < height; y += 2)
    {
        unsigned char *dst_uv = uv_plane + ((size_t)(y / 2) * (size_t)width);

        for (x = 0; x < width; x += 2)
        {
            int dx;
            int dy;
            int sum_u = 0;
            int sum_v = 0;

            for (dy = 0; dy < 2; ++dy)
            {
                const unsigned char *src = (const unsigned char *)bgra +
                    ((size_t)(y + dy) * (size_t)frame_width * 4U);
                for (dx = 0; dx < 2; ++dx)
                {
                    unsigned char yy;
                    unsigned char uu;
                    unsigned char vv;
                    const unsigned char *px = src + ((size_t)(x + dx) * 4U);

                    ohos_bgr_to_yuv709_full(px[0], px[1], px[2],
                                            &yy, &uu, &vv);
                    sum_u += uu;
                    sum_v += vv;
                }
            }

            dst_uv[x] = (unsigned char)((sum_u + 2) / 4);
            dst_uv[x + 1] = (unsigned char)((sum_v + 2) / 4);
        }
    }

    return 0;
}

static void
ohos_put_u8(char **p, int value)
{
    (*p)[0] = (char)(value & 0xff);
    *p += 1;
}

static void
ohos_put_u16(char **p, int value)
{
    (*p)[0] = (char)(value & 0xff);
    (*p)[1] = (char)((value >> 8) & 0xff);
    *p += 2;
}

static void
ohos_put_u32(char **p, unsigned int value)
{
    (*p)[0] = (char)(value & 0xff);
    (*p)[1] = (char)((value >> 8) & 0xff);
    (*p)[2] = (char)((value >> 16) & 0xff);
    (*p)[3] = (char)((value >> 24) & 0xff);
    *p += 4;
}

static void
ohos_put_gfx_header(char **p, int cmd_id, int cmd_bytes)
{
    ohos_put_u16(p, cmd_id);
    ohos_put_u16(p, 0);
    ohos_put_u32(p, (unsigned int)cmd_bytes);
}

static void
ohos_put_rect_wh(char **p, int left, int top, int width, int height)
{
    ohos_put_u16(p, left);
    ohos_put_u16(p, top);
    ohos_put_u16(p, width);
    ohos_put_u16(p, height);
}

static char *
ohos_build_avc420_commands(int width, int height, int frame_id, int *bytes)
{
    const int start_bytes = 16;
    const int wire_bytes = 45;
    const int end_bytes = 12;
    const int total = start_bytes + wire_bytes + end_bytes;
    char *cmd;
    char *p;

    cmd = (char *)g_malloc(total, 0);
    if (cmd == 0)
    {
        return 0;
    }

    p = cmd;
    ohos_put_gfx_header(&p, XR_RDPGFX_CMDID_STARTFRAME, start_bytes);
    ohos_put_u32(&p, (unsigned int)frame_id);
    ohos_put_u32(&p, 0);

    ohos_put_gfx_header(&p, XR_RDPGFX_CMDID_WIRETOSURFACE_1, wire_bytes);
    ohos_put_u16(&p, 0);
    ohos_put_u16(&p, XR_RDPGFX_CODECID_AVC420);
    ohos_put_u8(&p, XR_PIXEL_FORMAT_XRGB_8888);
    ohos_put_u32(&p, 0);
    ohos_put_u16(&p, 1);
    ohos_put_rect_wh(&p, 0, 0, width, height);
    ohos_put_u16(&p, 1);
    ohos_put_rect_wh(&p, 0, 0, width, height);
    ohos_put_rect_wh(&p, 0, 0, width, height);

    ohos_put_gfx_header(&p, XR_RDPGFX_CMDID_ENDFRAME, end_bytes);
    ohos_put_u32(&p, (unsigned int)frame_id);

    *bytes = total;
    return cmd;
}

int
ohos_gfx_send_avc420_frame(struct mod *mod,
                           const char *bgra,
                           int frame_width,
                           int frame_height,
                           int paint_width,
                           int paint_height,
                           int frame_id,
                           uint64_t source_sequence)
{
    static int log_count = 0;
    char *cmd;
    int cmd_bytes = 0;
    size_t y_bytes;
    size_t data_bytes;
    void *mapped;
    int rv;

    if (mod == 0 || mod->server_egfx_cmd == 0 || bgra == 0 ||
            paint_width <= 0 || paint_height <= 0 ||
            frame_width < paint_width || frame_height < paint_height ||
            (paint_width & 1) != 0 || (paint_height & 1) != 0)
    {
        return 1;
    }

    y_bytes = (size_t)paint_width * (size_t)paint_height;
    if (paint_height <= 0 || y_bytes / (size_t)paint_height != (size_t)paint_width ||
            y_bytes > ((size_t)-1 / 3U) * 2U)
    {
        return 1;
    }
    data_bytes = y_bytes + (y_bytes / 2U);

    mapped = mmap(0, data_bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.avc420: mmap failed size=%d",
            (int)data_bytes);
        return 1;
    }

    if (ohos_bgra_to_nv12(bgra, frame_width, frame_height,
                          paint_width, paint_height,
                          (unsigned char *)mapped) != 0)
    {
        munmap(mapped, data_bytes);
        return 1;
    }

    cmd = ohos_build_avc420_commands(paint_width, paint_height,
                                     frame_id, &cmd_bytes);
    if (cmd == 0)
    {
        munmap(mapped, data_bytes);
        return 1;
    }

    rv = mod->server_egfx_cmd(mod, cmd, cmd_bytes, (char *)mapped,
                              (int)data_bytes);
    g_free(cmd);

    if (rv == 0)
    {
        log_count++;
        if (log_count <= 3 || (log_count % 60) == 0)
        {
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.avc420: queued frame id=%d source_seq=%llu size=%dx%d bytes=%d count=%d",
                frame_id, (unsigned long long)source_sequence,
                paint_width, paint_height, (int)data_bytes, log_count);
        }
    }
    else
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.avc420: server_egfx_cmd failed rv=%d frame=%d",
            rv, frame_id);
    }

    return rv;
}
