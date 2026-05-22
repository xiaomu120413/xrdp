#ifndef XRDP_OHOS_CURSOR_SYSTEM_H
#define XRDP_OHOS_CURSOR_SYSTEM_H

#include "ohos_cursor_image.h"

int
ohos_cursor_system_style_is_null(int style);

int
ohos_cursor_system_get_image(int style,
                             const struct ohos_cursor_image **image);

#endif /* XRDP_OHOS_CURSOR_SYSTEM_H */
