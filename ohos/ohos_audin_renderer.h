#ifndef XRDP_OHOS_AUDIN_RENDERER_H
#define XRDP_OHOS_AUDIN_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ohos_audin_renderer;

struct ohos_audin_renderer *
ohos_audin_renderer_create(void);

void
ohos_audin_renderer_destroy(struct ohos_audin_renderer *renderer);

int
ohos_audin_renderer_open(struct ohos_audin_renderer *renderer,
                         uint32_t rate, uint16_t channels,
                         uint16_t bits_per_sample);

void
ohos_audin_renderer_close(struct ohos_audin_renderer *renderer);

int
ohos_audin_renderer_push(struct ohos_audin_renderer *renderer,
                         const void *data, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
