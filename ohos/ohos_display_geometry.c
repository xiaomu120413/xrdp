/*
 * HarmonyOS display geometry helpers for xrdp OHOS integration.
 */

#include "xrdp_ohos.h"

#include <stdbool.h>

#include <window_manager/oh_display_manager.h>

int
xrdp_ohos_query_display_geometry(
    struct xrdp_ohos_display_geometry *geometry)
{
    uint32_t caller_size;
    uint32_t write_size;
    uint64_t display_id = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t origin_x = 0;
    int32_t origin_y = 0;
    float virtual_pixel_ratio = 0.0F;
    uint32_t refresh_rate = 0;
    NativeDisplayManager_SourceMode source_mode = DISPLAY_SOURCE_MODE_NONE;
    NativeDisplayManager_ErrorCode id_rc;
    NativeDisplayManager_ErrorCode width_rc;
    NativeDisplayManager_ErrorCode height_rc;

    if (geometry == 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    caller_size = geometry->size;
    write_size = caller_size < sizeof(struct xrdp_ohos_display_geometry) ?
                 caller_size : sizeof(struct xrdp_ohos_display_geometry);
    if (write_size > 0)
    {
        char *bytes = (char *)geometry;
        uint32_t index;

        for (index = 0; index < write_size; index++)
        {
            bytes[index] = 0;
        }
    }
    if (caller_size >= sizeof(geometry->size))
    {
        geometry->size = sizeof(struct xrdp_ohos_display_geometry);
    }
    if (caller_size < sizeof(struct xrdp_ohos_display_geometry))
    {
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    id_rc = OH_NativeDisplayManager_GetDefaultDisplayId(&display_id);
    width_rc = OH_NativeDisplayManager_GetDefaultDisplayWidth(&width);
    height_rc = OH_NativeDisplayManager_GetDefaultDisplayHeight(&height);
    if (id_rc != DISPLAY_MANAGER_OK || width_rc != DISPLAY_MANAGER_OK ||
            height_rc != DISPLAY_MANAGER_OK || width <= 0 || height <= 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_OK;
    }

    geometry->valid = 1;
    geometry->display_id = display_id;
    geometry->width = width;
    geometry->height = height;

    if (OH_NativeDisplayManager_GetDisplayPosition(display_id, &origin_x,
            &origin_y) == DISPLAY_MANAGER_OK)
    {
        geometry->origin_x = origin_x;
        geometry->origin_y = origin_y;
    }
    if (OH_NativeDisplayManager_GetDefaultDisplayVirtualPixelRatio(
            &virtual_pixel_ratio) == DISPLAY_MANAGER_OK)
    {
        geometry->virtual_pixel_ratio_valid = 1;
        geometry->virtual_pixel_ratio = virtual_pixel_ratio;
    }
    if (OH_NativeDisplayManager_GetDefaultDisplayRefreshRate(
            &refresh_rate) == DISPLAY_MANAGER_OK)
    {
        geometry->refresh_rate_valid = 1;
        geometry->refresh_rate = refresh_rate;
    }
    if (OH_NativeDisplayManager_GetDisplaySourceMode(display_id,
            &source_mode) == DISPLAY_MANAGER_OK)
    {
        geometry->source_mode_valid = 1;
        geometry->source_mode = (int32_t)source_mode;
    }

    return XRDP_OHOS_BACKEND_STATUS_OK;
}
