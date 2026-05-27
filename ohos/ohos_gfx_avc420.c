/**
 * HarmonyOS xrdp backend RDPGFX AVC420 frame path.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdint.h>
#include <sys/mman.h>
#include <time.h>

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

static uint64_t
ohos_now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

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
ohos_build_avc420_commands_ex(int dst_left, int dst_top, int width,
                              int height, int frame_id,
                              int already_compressed, int *bytes)
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
    ohos_put_u32(&p, already_compressed ? 1U : 0U);
    ohos_put_u16(&p, 1);
    ohos_put_rect_wh(&p, dst_left, dst_top, width, height);
    ohos_put_u16(&p, 1);
    ohos_put_rect_wh(&p, 0, 0, width, height);
    ohos_put_rect_wh(&p, dst_left, dst_top, width, height);

    ohos_put_gfx_header(&p, XR_RDPGFX_CMDID_ENDFRAME, end_bytes);
    ohos_put_u32(&p, (unsigned int)frame_id);

    *bytes = total;
    return cmd;
}

static int
ohos_copy_nv12(const char *nv12, int frame_width, int frame_height,
               int stride, int width, int height, unsigned char *target)
{
    int y;
    unsigned char *target_y;
    unsigned char *target_uv;
    const unsigned char *source_y;
    const unsigned char *source_uv;

    if (nv12 == 0 || target == 0 || frame_width < width ||
            frame_height < height || stride < frame_width ||
            width <= 0 || height <= 0 || (width & 1) != 0 ||
            (height & 1) != 0)
    {
        return 1;
    }

    target_y = target;
    target_uv = target + ((size_t)width * (size_t)height);
    source_y = (const unsigned char *)nv12;
    source_uv = source_y + ((size_t)stride * (size_t)frame_height);

    for (y = 0; y < height; ++y)
    {
        g_memcpy(target_y + ((size_t)y * (size_t)width),
                 source_y + ((size_t)y * (size_t)stride),
                 width);
    }

    for (y = 0; y < height / 2; ++y)
    {
        g_memcpy(target_uv + ((size_t)y * (size_t)width),
                 source_uv + ((size_t)y * (size_t)stride),
                 width);
    }

    return 0;
}

int
ohos_gfx_send_avc420_nv12_frame(struct mod *mod,
                                const char *nv12,
                                int frame_width,
                                int frame_height,
                                int stride,
                                int dst_left,
                                int dst_top,
                                int paint_width,
                                int paint_height,
                                int frame_id,
                                uint64_t source_sequence,
                                struct ohos_gfx_avc420_trace *trace)
{
    static int log_count = 0;
    char *cmd;
    int cmd_bytes = 0;
    size_t y_bytes;
    size_t data_bytes;
    void *mapped;
    uint64_t enter_us;
    uint64_t copy_start_us;
    uint64_t copy_done_us;
    uint64_t enqueue_start_us;
    uint64_t enqueue_done_us;
    int rv;

    enter_us = ohos_now_us();
    if (trace != 0)
    {
        trace->enter_us = enter_us;
        trace->convert_done_us = 0;
        trace->enqueue_done_us = 0;
        trace->convert_us = 0;
        trace->enqueue_us = 0;
    }

    if (mod == 0 || mod->server_egfx_cmd == 0 || nv12 == 0 ||
            paint_width <= 0 || paint_height <= 0 ||
            frame_width < paint_width || frame_height < paint_height ||
            stride < frame_width ||
            dst_left < 0 || dst_top < 0 ||
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

    copy_start_us = ohos_now_us();
    if (ohos_copy_nv12(nv12, frame_width, frame_height, stride,
                       paint_width, paint_height,
                       (unsigned char *)mapped) != 0)
    {
        munmap(mapped, data_bytes);
        return 1;
    }
    copy_done_us = ohos_now_us();
    if (trace != 0)
    {
        trace->convert_done_us = copy_done_us;
        if (copy_done_us >= copy_start_us)
        {
            trace->convert_us = (uint32_t)(copy_done_us - copy_start_us);
        }
    }

    cmd = ohos_build_avc420_commands_ex(dst_left, dst_top, paint_width,
                                        paint_height, frame_id, 0,
                                        &cmd_bytes);
    if (cmd == 0)
    {
        munmap(mapped, data_bytes);
        return 1;
    }

    enqueue_start_us = ohos_now_us();
    rv = mod->server_egfx_cmd(mod, cmd, cmd_bytes, (char *)mapped,
                              (int)data_bytes);
    enqueue_done_us = ohos_now_us();
    if (trace != 0)
    {
        trace->enqueue_done_us = enqueue_done_us;
        if (enqueue_done_us >= enqueue_start_us)
        {
            trace->enqueue_us = (uint32_t)(enqueue_done_us - enqueue_start_us);
        }
    }
    g_free(cmd);

    if (rv == 0)
    {
        log_count++;
        if (log_count <= 3 || (log_count % 60) == 0)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.avc420: queued NV12 frame id=%d source_seq=%llu size=%dx%d bytes=%d copy=%.3fms enqueue=%.3fms count=%d",
                frame_id, (unsigned long long)source_sequence,
                paint_width, paint_height, (int)data_bytes,
                trace == 0 ? 0.0 : trace->convert_us / 1000.0,
                trace == 0 ? 0.0 : trace->enqueue_us / 1000.0,
                log_count);
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

int
ohos_gfx_send_avc420_h264_frame(struct mod *mod,
                                const char *h264,
                                int h264_bytes,
                                int dst_left,
                                int dst_top,
                                int paint_width,
                                int paint_height,
                                int frame_id,
                                uint64_t source_sequence,
                                struct ohos_gfx_avc420_trace *trace)
{
    static int log_count = 0;
    char *cmd;
    int cmd_bytes = 0;
    void *mapped;
    uint64_t enter_us;
    uint64_t copy_start_us;
    uint64_t copy_done_us;
    uint64_t enqueue_start_us;
    uint64_t enqueue_done_us;
    int rv;

    enter_us = ohos_now_us();
    if (trace != 0)
    {
        trace->enter_us = enter_us;
        trace->convert_done_us = 0;
        trace->enqueue_done_us = 0;
        trace->convert_us = 0;
        trace->enqueue_us = 0;
    }

    if (mod == 0 || mod->server_egfx_cmd == 0 || h264 == 0 ||
            h264_bytes <= 0 || paint_width <= 0 || paint_height <= 0 ||
            dst_left < 0 || dst_top < 0 ||
            (paint_width & 1) != 0 || (paint_height & 1) != 0)
    {
        return 1;
    }

    mapped = mmap(0, (size_t)h264_bytes, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapped == MAP_FAILED)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.avc420: h264 mmap failed size=%d",
            h264_bytes);
        return 1;
    }

    copy_start_us = ohos_now_us();
    g_memcpy(mapped, h264, h264_bytes);
    copy_done_us = ohos_now_us();
    if (trace != 0)
    {
        trace->convert_done_us = copy_done_us;
        if (copy_done_us >= copy_start_us)
        {
            trace->convert_us = (uint32_t)(copy_done_us - copy_start_us);
        }
    }

    cmd = ohos_build_avc420_commands_ex(dst_left, dst_top, paint_width,
                                        paint_height, frame_id, 1,
                                        &cmd_bytes);
    if (cmd == 0)
    {
        munmap(mapped, (size_t)h264_bytes);
        return 1;
    }

    enqueue_start_us = ohos_now_us();
    rv = mod->server_egfx_cmd(mod, cmd, cmd_bytes, (char *)mapped,
                              h264_bytes);
    enqueue_done_us = ohos_now_us();
    if (trace != 0)
    {
        trace->enqueue_done_us = enqueue_done_us;
        if (enqueue_done_us >= enqueue_start_us)
        {
            trace->enqueue_us = (uint32_t)(enqueue_done_us - enqueue_start_us);
        }
    }
    g_free(cmd);

    log_count++;
    if (rv == 0 && (log_count <= 5 || (log_count % 60) == 0))
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.avc420: queued pre-encoded H264 frame id=%d source_seq=%llu size=%dx%d bytes=%d copy=%.3fms enqueue=%.3fms count=%d",
            frame_id, (unsigned long long)source_sequence,
            paint_width, paint_height, h264_bytes,
            trace != 0 ? trace->convert_us / 1000.0 : 0.0,
            trace != 0 ? trace->enqueue_us / 1000.0 : 0.0,
            log_count);
    }
    return rv;
}

int
ohos_gfx_send_avc420_frame(struct mod *mod,
                           const char *bgra,
                           int frame_width,
                           int frame_height,
                           int dst_left,
                           int dst_top,
                           int paint_width,
                           int paint_height,
                           int frame_id,
                           uint64_t source_sequence,
                           struct ohos_gfx_avc420_trace *trace)
{
    static int log_count = 0;
    char *cmd;
    int cmd_bytes = 0;
    size_t y_bytes;
    size_t data_bytes;
    void *mapped;
    uint64_t enter_us;
    uint64_t convert_start_us;
    uint64_t convert_done_us;
    uint64_t enqueue_start_us;
    uint64_t enqueue_done_us;
    int rv;

    enter_us = ohos_now_us();
    if (trace != 0)
    {
        trace->enter_us = enter_us;
        trace->convert_done_us = 0;
        trace->enqueue_done_us = 0;
        trace->convert_us = 0;
        trace->enqueue_us = 0;
    }

    if (mod == 0 || mod->server_egfx_cmd == 0 || bgra == 0 ||
            paint_width <= 0 || paint_height <= 0 ||
            frame_width < paint_width || frame_height < paint_height ||
            dst_left < 0 || dst_top < 0 ||
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

    convert_start_us = ohos_now_us();
    if (ohos_bgra_to_nv12(bgra, frame_width, frame_height,
                          paint_width, paint_height,
                          (unsigned char *)mapped) != 0)
    {
        munmap(mapped, data_bytes);
        return 1;
    }
    convert_done_us = ohos_now_us();
    if (trace != 0)
    {
        trace->convert_done_us = convert_done_us;
        if (convert_done_us >= convert_start_us)
        {
            trace->convert_us = (uint32_t)(convert_done_us - convert_start_us);
        }
    }

    cmd = ohos_build_avc420_commands_ex(dst_left, dst_top, paint_width,
                                        paint_height, frame_id, 0,
                                        &cmd_bytes);
    if (cmd == 0)
    {
        munmap(mapped, data_bytes);
        return 1;
    }

    enqueue_start_us = ohos_now_us();
    rv = mod->server_egfx_cmd(mod, cmd, cmd_bytes, (char *)mapped,
                              (int)data_bytes);
    enqueue_done_us = ohos_now_us();
    if (trace != 0)
    {
        trace->enqueue_done_us = enqueue_done_us;
        if (enqueue_done_us >= enqueue_start_us)
        {
            trace->enqueue_us = (uint32_t)(enqueue_done_us - enqueue_start_us);
        }
    }
    g_free(cmd);

    if (rv == 0)
    {
        log_count++;
        if (log_count <= 3 || (log_count % 60) == 0)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.avc420: queued frame id=%d source_seq=%llu size=%dx%d bytes=%d convert=%.3fms enqueue=%.3fms count=%d",
                frame_id, (unsigned long long)source_sequence,
                paint_width, paint_height, (int)data_bytes,
                trace == 0 ? 0.0 : trace->convert_us / 1000.0,
                trace == 0 ? 0.0 : trace->enqueue_us / 1000.0,
                log_count);
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
