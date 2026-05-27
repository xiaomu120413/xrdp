#ifndef XRDP_OHOS_DESKTOP_SIZE_H
#define XRDP_OHOS_DESKTOP_SIZE_H

#include "xrdp_ohos.h"

struct ohos_desktop_size
{
    int requested_width;
    int requested_height;
    int target_width;
    int target_height;
    int max_width;
    int max_height;
    int display_width;
    int display_height;
    int normalized;
    int limited_by_max;
    int valid_display;
};

int
ohos_select_desktop_size(int requested_width, int requested_height,
                         int max_width, int max_height,
                         struct ohos_desktop_size *desktop);

#endif
