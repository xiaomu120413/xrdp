/*
 * HarmonyOS desktop sizing helpers for xrdp OHOS integration.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_desktop_size.h"

#define OHOS_MIN_DESKTOP_DIMENSION 200

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

static int
ohos_sanitize_max_dimension(int value)
{
    if (value == 0)
    {
        return 0;
    }
    if (value < OHOS_MIN_DESKTOP_DIMENSION ||
            value > XRDP_OHOS_FRAME_MAX_DIMENSION)
    {
        return 0;
    }
    return value;
}

int
ohos_select_desktop_size(int requested_width, int requested_height,
                         int max_width, int max_height,
                         struct ohos_desktop_size *desktop)
{
    struct xrdp_ohos_display_geometry geometry;
    int divisor;
    int ratio_width;
    int ratio_height;
    int effective_width;
    int effective_height;
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
    desktop->desktop_width = requested_width;
    desktop->desktop_height = requested_height;
    desktop->target_left = 0;
    desktop->target_top = 0;
    desktop->target_width = requested_width;
    desktop->target_height = requested_height;
    desktop->max_width = ohos_sanitize_max_dimension(max_width);
    desktop->max_height = ohos_sanitize_max_dimension(max_height);
    desktop->display_width = 0;
    desktop->display_height = 0;
    desktop->normalized = 0;
    desktop->limited_by_max = 0;
    desktop->limited_by_aspect = 0;
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

    effective_width = requested_width;
    effective_height = requested_height;
    if (desktop->max_width > 0 && effective_width > desktop->max_width)
    {
        effective_width = desktop->max_width;
        desktop->limited_by_max = 1;
    }
    if (desktop->max_height > 0 && effective_height > desktop->max_height)
    {
        effective_height = desktop->max_height;
        desktop->limited_by_max = 1;
    }
    desktop->desktop_width = effective_width;
    desktop->desktop_height = effective_height;

    scale_width = effective_width / ratio_width;
    scale_height = effective_height / ratio_height;
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
    desktop->target_left = (effective_width - target_width) / 2;
    desktop->target_top = (effective_height - target_height) / 2;
    desktop->target_width = target_width;
    desktop->target_height = target_height;
    desktop->limited_by_aspect = target_width != effective_width ||
                                 target_height != effective_height;
    desktop->normalized = desktop->limited_by_max;
    return 0;
}
