/*
 * HarmonyOS desktop sizing helpers for xrdp OHOS integration.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_desktop_size.h"

static int
ohos_gcd_positive(int a, int b)
{
    int t;

    while (b != 0)
    {
        t = a % b;
        a = b;
        b = t;
    }
    return a <= 0 ? 1 : a;
}

int
ohos_select_desktop_size(int requested_width, int requested_height,
                         struct ohos_desktop_size *desktop)
{
    struct xrdp_ohos_display_geometry geometry;
    int divisor;
    int ratio_width;
    int ratio_height;
    int scale_width;
    int scale_height;
    int scale;
    int target_width;
    int target_height;

    if (desktop == 0)
    {
        return 1;
    }

    desktop->requested_width = requested_width;
    desktop->requested_height = requested_height;
    desktop->target_width = requested_width;
    desktop->target_height = requested_height;
    desktop->display_width = 0;
    desktop->display_height = 0;
    desktop->normalized = 0;
    desktop->valid_display = 0;

    if (requested_width <= 0 || requested_height <= 0)
    {
        return 1;
    }

    geometry.size = sizeof(geometry);
    geometry.valid = 0;
    if (xrdp_ohos_query_display_geometry(&geometry) != 0 ||
            !geometry.valid || geometry.width <= 0 || geometry.height <= 0)
    {
        return 0;
    }

    desktop->display_width = geometry.width;
    desktop->display_height = geometry.height;
    desktop->valid_display = 1;

    divisor = ohos_gcd_positive(geometry.width, geometry.height);
    ratio_width = geometry.width / divisor;
    ratio_height = geometry.height / divisor;
    if (ratio_width <= 0 || ratio_height <= 0)
    {
        return 0;
    }

    scale_width = requested_width / ratio_width;
    scale_height = requested_height / ratio_height;
    scale = scale_width < scale_height ? scale_width : scale_height;
    while (scale > 0 &&
            (((ratio_width * scale) & 1) != 0 ||
             ((ratio_height * scale) & 1) != 0))
    {
        --scale;
    }
    if (scale <= 0)
    {
        return 0;
    }

    target_width = ratio_width * scale;
    target_height = ratio_height * scale;
    desktop->target_width = target_width;
    desktop->target_height = target_height;
    desktop->normalized = target_width != requested_width ||
                          target_height != requested_height;
    return 0;
}
