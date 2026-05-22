#ifndef XRDP_OHOS_CURSOR_IMAGE_H
#define XRDP_OHOS_CURSOR_IMAGE_H

#include "arch.h"

#include <stdint.h>
#include <multimedia/image_framework/image/pixelmap_native.h>

#define OHOS_CURSOR_IMAGE_MAX_SIZE 96

struct ohos_cursor_image
{
    char *data;
    char *mask;
    int width;
    int height;
    int hot_x;
    int hot_y;
    int bpp;
    uint64_t hash;
};

void
ohos_cursor_image_init(struct ohos_cursor_image *image);

void
ohos_cursor_image_deinit(struct ohos_cursor_image *image);

int
ohos_cursor_image_from_pixelmap(OH_PixelmapNative *pixelmap, int style,
                                struct ohos_cursor_image *image);

int
ohos_cursor_image_from_pixelmap_target(OH_PixelmapNative *pixelmap,
                                       int style, int target_width,
                                       int target_height,
                                       struct ohos_cursor_image *image);

#endif /* XRDP_OHOS_CURSOR_IMAGE_H */
