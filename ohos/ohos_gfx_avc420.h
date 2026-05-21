#ifndef OHOS_GFX_AVC420_H
#define OHOS_GFX_AVC420_H

#include <stdint.h>

struct mod;

int
ohos_gfx_send_avc420_frame(struct mod *mod,
                           const char *bgra,
                           int frame_width,
                           int frame_height,
                           int paint_width,
                           int paint_height,
                           int frame_id,
                           uint64_t source_sequence);

#endif
