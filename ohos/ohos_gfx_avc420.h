#ifndef OHOS_GFX_AVC420_H
#define OHOS_GFX_AVC420_H

#include <stdint.h>

struct mod;

struct ohos_gfx_avc420_trace
{
    uint64_t enter_us;
    uint64_t convert_done_us;
    uint64_t enqueue_done_us;
    uint32_t convert_us;
    uint32_t enqueue_us;
};

int
ohos_gfx_send_avc420_frame(struct mod *mod,
                           const char *bgra,
                           int frame_width,
                           int frame_height,
                           int dst_left,
                           int dst_top,
                           int paint_width,
                           int paint_height,
                           int desktop_width,
                           int desktop_height,
                           int frame_id,
                           uint64_t source_sequence,
                           struct ohos_gfx_avc420_trace *trace);

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
                                int desktop_width,
                                int desktop_height,
                                int frame_id,
                                uint64_t source_sequence,
                                struct ohos_gfx_avc420_trace *trace);

int
ohos_gfx_send_avc420_h264_frame(struct mod *mod,
                                const char *h264,
                                int h264_bytes,
                                int dst_left,
                                int dst_top,
                                int paint_width,
                                int paint_height,
                                int desktop_width,
                                int desktop_height,
                                int frame_id,
                                uint64_t source_sequence,
                                struct ohos_gfx_avc420_trace *trace);

#endif
