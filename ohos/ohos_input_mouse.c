#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_input.h"

#include "log.h"

#include <limits.h>
#include <stdbool.h>
#include <multimodalinput/oh_input_manager.h>

#define OHOS_INPUT_GEOMETRY_CACHE_MS 1000ULL
#define OHOS_INPUT_WHEEL_STEP 120.0F

static int
ohos_input_clamp_long(long value, int min_value, int max_value)
{
    if (max_value < min_value)
    {
        return min_value;
    }
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return (int)value;
}

static int
ohos_input_round_div_i64(int64_t numerator, int64_t denominator)
{
    if (denominator <= 0)
    {
        return 0;
    }
    if (numerator < 0)
    {
        return (int)((numerator - denominator / 2) / denominator);
    }
    return (int)((numerator + denominator / 2) / denominator);
}

static int
ohos_input_scale_coordinate(long value, int source_size, int target_size)
{
    int source;
    int64_t numerator;

    if (target_size <= 1)
    {
        return 0;
    }
    if (source_size <= 1)
    {
        return ohos_input_clamp_long(value, 0, target_size - 1);
    }

    source = ohos_input_clamp_long(value, 0, source_size - 1);
    numerator = (int64_t)source * (int64_t)(target_size - 1);
    return ohos_input_clamp_long(
        ohos_input_round_div_i64(numerator, source_size - 1),
        0, target_size - 1);
}

static int
ohos_input_scale_coordinate_from_content(long value, int content_start,
                                         int content_size, int target_size)
{
    int source;
    int64_t numerator;

    if (target_size <= 1)
    {
        return 0;
    }
    if (content_size <= 1)
    {
        return ohos_input_clamp_long(value - content_start, 0,
                                     target_size - 1);
    }

    source = ohos_input_clamp_long(value - content_start, 0,
                                   content_size - 1);
    numerator = (int64_t)source * (int64_t)(target_size - 1);
    return ohos_input_clamp_long(
        ohos_input_round_div_i64(numerator, content_size - 1),
        0, target_size - 1);
}

static void
ohos_input_resolve_content_rect(int source_width, int source_height,
                                int target_width, int target_height,
                                struct ohos_input_mouse_coordinates *coords)
{
    int64_t lhs;
    int64_t rhs;

    coords->content_rect_valid = 1;
    coords->content_left = 0;
    coords->content_top = 0;
    coords->content_width = source_width;
    coords->content_height = source_height;

    if (source_width <= 0 || source_height <= 0 ||
        target_width <= 0 || target_height <= 0)
    {
        coords->content_rect_valid = 0;
        return;
    }

    lhs = (int64_t)source_width * (int64_t)target_height;
    rhs = (int64_t)source_height * (int64_t)target_width;
    if (lhs <= rhs)
    {
        coords->content_width = source_width;
        coords->content_height = ohos_input_clamp_long(
            ohos_input_round_div_i64((int64_t)target_height * source_width,
                                     target_width),
            1, source_height);
    }
    else
    {
        coords->content_width = ohos_input_clamp_long(
            ohos_input_round_div_i64((int64_t)target_width * source_height,
                                     target_height),
            1, source_width);
        coords->content_height = source_height;
    }
    coords->content_left = (source_width - coords->content_width) / 2;
    coords->content_top = (source_height - coords->content_height) / 2;
}

static int
ohos_input_geometry_changed(const struct xrdp_ohos_display_geometry *old_geometry,
                            const struct xrdp_ohos_display_geometry *new_geometry)
{
    if (old_geometry->valid != new_geometry->valid)
    {
        return 1;
    }
    if (!new_geometry->valid)
    {
        return 0;
    }
    return old_geometry->display_id != new_geometry->display_id ||
           old_geometry->width != new_geometry->width ||
           old_geometry->height != new_geometry->height ||
           old_geometry->origin_x != new_geometry->origin_x ||
           old_geometry->origin_y != new_geometry->origin_y ||
           old_geometry->virtual_pixel_ratio_valid !=
               new_geometry->virtual_pixel_ratio_valid ||
           old_geometry->virtual_pixel_ratio !=
               new_geometry->virtual_pixel_ratio ||
           old_geometry->source_mode_valid != new_geometry->source_mode_valid ||
           old_geometry->source_mode != new_geometry->source_mode;
}

static void
ohos_input_refresh_geometry(struct ohos_input_context *ctx)
{
    struct xrdp_ohos_display_geometry geometry;
    uint64_t now_ms;

    if (ctx == 0)
    {
        return;
    }

    now_ms = ohos_input_now_ms();
    if (ctx->geometry.valid &&
        now_ms >= ctx->geometry_query_ms &&
        now_ms - ctx->geometry_query_ms < OHOS_INPUT_GEOMETRY_CACHE_MS)
    {
        return;
    }

    geometry.size = sizeof(geometry);
    geometry.valid = 0;
    if (xrdp_ohos_query_display_geometry(&geometry) == 0)
    {
        if (ohos_input_geometry_changed(&ctx->geometry, &geometry))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: display id=%llu size=%dx%d origin=(%d,%d) vpr_valid=%u vpr=%.3f source_mode_valid=%u source_mode=%d",
                (unsigned long long)geometry.display_id,
                geometry.width, geometry.height,
                geometry.origin_x, geometry.origin_y,
                geometry.virtual_pixel_ratio_valid,
                geometry.virtual_pixel_ratio,
                geometry.source_mode_valid,
                geometry.source_mode);
        }
        ctx->geometry = geometry;
    }
    else
    {
        ctx->geometry.valid = 0;
    }
    ctx->geometry_query_ms = now_ms;
}

int
ohos_input_is_mouse_move(const struct xrdp_ohos_input_event *event)
{
    return event != 0 && event->msg == XRDP_OHOS_WM_MOUSEMOVE;
}

int
ohos_input_active_mouse_button_from_mask(uint32_t mask)
{
    if ((mask & OHOS_INPUT_MOUSE_LEFT_MASK) != 0U)
    {
        return MOUSE_BUTTON_LEFT;
    }
    if ((mask & OHOS_INPUT_MOUSE_MIDDLE_MASK) != 0U)
    {
        return MOUSE_BUTTON_MIDDLE;
    }
    if ((mask & OHOS_INPUT_MOUSE_RIGHT_MASK) != 0U)
    {
        return MOUSE_BUTTON_RIGHT;
    }
    if ((mask & OHOS_INPUT_MOUSE_FORWARD_MASK) != 0U)
    {
        return MOUSE_BUTTON_FORWARD;
    }
    if ((mask & OHOS_INPUT_MOUSE_BACK_MASK) != 0U)
    {
        return MOUSE_BUTTON_BACK;
    }
    return MOUSE_BUTTON_NONE;
}

int
ohos_input_mouse_button_up_message_from_mask(uint32_t mask)
{
    switch (mask)
    {
        case OHOS_INPUT_MOUSE_LEFT_MASK:
            return XRDP_OHOS_WM_LBUTTONUP;
        case OHOS_INPUT_MOUSE_MIDDLE_MASK:
            return XRDP_OHOS_WM_MBUTTONUP;
        case OHOS_INPUT_MOUSE_RIGHT_MASK:
            return XRDP_OHOS_WM_RBUTTONUP;
        case OHOS_INPUT_MOUSE_FORWARD_MASK:
            return XRDP_OHOS_WM_XBUTTON2UP;
        case OHOS_INPUT_MOUSE_BACK_MASK:
            return XRDP_OHOS_WM_XBUTTON1UP;
        default:
            return XRDP_OHOS_WM_LBUTTONUP;
    }
}

struct ohos_input_mouse_dispatch
ohos_input_map_mouse(const struct xrdp_ohos_input_event *event)
{
    struct ohos_input_mouse_dispatch dispatch;

    dispatch.supported = 1;
    dispatch.wheel = 0;
    dispatch.action = MOUSE_ACTION_MOVE;
    dispatch.button = MOUSE_BUTTON_NONE;
    dispatch.axis_type = MOUSE_AXIS_SCROLL_VERTICAL;
    dispatch.axis_value = 0.0F;
    dispatch.begin_axis_before_update = 0;
    dispatch.end_axis_after_update = 0;
    dispatch.button_mask = 0;
    dispatch.button_down = 0;

    if (event == 0)
    {
        dispatch.supported = 0;
        return dispatch;
    }

    switch (event->msg)
    {
        case XRDP_OHOS_WM_MOUSEMOVE:
            dispatch.action = MOUSE_ACTION_MOVE;
            break;
        case XRDP_OHOS_WM_LBUTTONDOWN:
            dispatch.action = MOUSE_ACTION_BUTTON_DOWN;
            dispatch.button = MOUSE_BUTTON_LEFT;
            dispatch.button_mask = OHOS_INPUT_MOUSE_LEFT_MASK;
            dispatch.button_down = 1;
            break;
        case XRDP_OHOS_WM_LBUTTONUP:
            dispatch.action = MOUSE_ACTION_BUTTON_UP;
            dispatch.button = MOUSE_BUTTON_LEFT;
            dispatch.button_mask = OHOS_INPUT_MOUSE_LEFT_MASK;
            break;
        case XRDP_OHOS_WM_RBUTTONDOWN:
            dispatch.action = MOUSE_ACTION_BUTTON_DOWN;
            dispatch.button = MOUSE_BUTTON_RIGHT;
            dispatch.button_mask = OHOS_INPUT_MOUSE_RIGHT_MASK;
            dispatch.button_down = 1;
            break;
        case XRDP_OHOS_WM_RBUTTONUP:
            dispatch.action = MOUSE_ACTION_BUTTON_UP;
            dispatch.button = MOUSE_BUTTON_RIGHT;
            dispatch.button_mask = OHOS_INPUT_MOUSE_RIGHT_MASK;
            break;
        case XRDP_OHOS_WM_MBUTTONDOWN:
            dispatch.action = MOUSE_ACTION_BUTTON_DOWN;
            dispatch.button = MOUSE_BUTTON_MIDDLE;
            dispatch.button_mask = OHOS_INPUT_MOUSE_MIDDLE_MASK;
            dispatch.button_down = 1;
            break;
        case XRDP_OHOS_WM_MBUTTONUP:
            dispatch.action = MOUSE_ACTION_BUTTON_UP;
            dispatch.button = MOUSE_BUTTON_MIDDLE;
            dispatch.button_mask = OHOS_INPUT_MOUSE_MIDDLE_MASK;
            break;
        case XRDP_OHOS_WM_XBUTTON1DOWN:
            dispatch.action = MOUSE_ACTION_BUTTON_DOWN;
            dispatch.button = MOUSE_BUTTON_BACK;
            dispatch.button_mask = OHOS_INPUT_MOUSE_BACK_MASK;
            dispatch.button_down = 1;
            break;
        case XRDP_OHOS_WM_XBUTTON1UP:
            dispatch.action = MOUSE_ACTION_BUTTON_UP;
            dispatch.button = MOUSE_BUTTON_BACK;
            dispatch.button_mask = OHOS_INPUT_MOUSE_BACK_MASK;
            break;
        case XRDP_OHOS_WM_XBUTTON2DOWN:
            dispatch.action = MOUSE_ACTION_BUTTON_DOWN;
            dispatch.button = MOUSE_BUTTON_FORWARD;
            dispatch.button_mask = OHOS_INPUT_MOUSE_FORWARD_MASK;
            dispatch.button_down = 1;
            break;
        case XRDP_OHOS_WM_XBUTTON2UP:
            dispatch.action = MOUSE_ACTION_BUTTON_UP;
            dispatch.button = MOUSE_BUTTON_FORWARD;
            dispatch.button_mask = OHOS_INPUT_MOUSE_FORWARD_MASK;
            break;
        case XRDP_OHOS_WM_WHEELUPUP:
            dispatch.wheel = 1;
            dispatch.axis_type = MOUSE_AXIS_SCROLL_VERTICAL;
            dispatch.axis_value = -OHOS_INPUT_WHEEL_STEP;
            dispatch.end_axis_after_update = 1;
            break;
        case XRDP_OHOS_WM_WHEELDOWNUP:
            dispatch.wheel = 1;
            dispatch.axis_type = MOUSE_AXIS_SCROLL_VERTICAL;
            dispatch.axis_value = OHOS_INPUT_WHEEL_STEP;
            dispatch.end_axis_after_update = 1;
            break;
        case XRDP_OHOS_WM_HWHEELLEFTUP:
            dispatch.wheel = 1;
            dispatch.axis_type = MOUSE_AXIS_SCROLL_HORIZONTAL;
            dispatch.axis_value = OHOS_INPUT_WHEEL_STEP;
            dispatch.end_axis_after_update = 1;
            break;
        case XRDP_OHOS_WM_HWHEELRIGHTUP:
            dispatch.wheel = 1;
            dispatch.axis_type = MOUSE_AXIS_SCROLL_HORIZONTAL;
            dispatch.axis_value = -OHOS_INPUT_WHEEL_STEP;
            dispatch.end_axis_after_update = 1;
            break;
        case XRDP_OHOS_WM_TOUCH_VSCROLL:
            dispatch.wheel = 1;
            dispatch.axis_type = MOUSE_AXIS_SCROLL_VERTICAL;
            dispatch.axis_value = (float)event->param3;
            dispatch.begin_axis_before_update = 1;
            dispatch.end_axis_after_update = 1;
            break;
        case XRDP_OHOS_WM_TOUCH_HSCROLL:
            dispatch.wheel = 1;
            dispatch.axis_type = MOUSE_AXIS_SCROLL_HORIZONTAL;
            dispatch.axis_value = (float)event->param3;
            dispatch.begin_axis_before_update = 1;
            dispatch.end_axis_after_update = 1;
            break;
        case XRDP_OHOS_WM_WHEELUPDOWN:
        case XRDP_OHOS_WM_WHEELDOWNDOWN:
        case XRDP_OHOS_WM_HWHEELLEFTDOWN:
        case XRDP_OHOS_WM_HWHEELRIGHTDOWN:
            dispatch.wheel = 1;
            dispatch.action = MOUSE_ACTION_AXIS_BEGIN;
            dispatch.axis_value = 0.0F;
            break;
        default:
            dispatch.supported = 0;
            break;
    }

    if (dispatch.wheel && dispatch.action != MOUSE_ACTION_AXIS_BEGIN)
    {
        dispatch.action = MOUSE_ACTION_AXIS_UPDATE;
        dispatch.button = MOUSE_BUTTON_NONE;
    }
    return dispatch;
}

void
ohos_input_resolve_mouse_coordinates(
    struct ohos_input_context *ctx,
    const struct xrdp_ohos_input_event *event,
    struct ohos_input_mouse_coordinates *coords)
{
    const struct xrdp_ohos_display_geometry *geometry;
    int source_width;
    int source_height;

    if (coords == 0)
    {
        return;
    }

    coords->display_id = 0;
    coords->display_x = 0;
    coords->display_y = 0;
    coords->global_x = 0;
    coords->global_y = 0;
    coords->source_width = 0;
    coords->source_height = 0;
    coords->target_width = 0;
    coords->target_height = 0;
    coords->full_display_x = 0;
    coords->full_display_y = 0;
    coords->fit_display_x = 0;
    coords->fit_display_y = 0;
    coords->content_rect_valid = 0;
    coords->content_left = 0;
    coords->content_top = 0;
    coords->content_width = 0;
    coords->content_height = 0;
    coords->content_clamped_x = 0;
    coords->content_clamped_y = 0;
    coords->inside_content_rect = 0;
    coords->content_mapping_active = 0;
    coords->virtual_pixel_ratio_valid = 0;
    coords->virtual_pixel_ratio = 0.0F;

    if (event == 0 || ctx == 0)
    {
        return;
    }

    ohos_input_refresh_geometry(ctx);
    geometry = &ctx->geometry;
    if (!geometry->valid || geometry->width <= 0 || geometry->height <= 0)
    {
        coords->display_x = ohos_input_clamp_long(event->param1, 0, 32767);
        coords->display_y = ohos_input_clamp_long(event->param2, 0, 32767);
        coords->global_x = coords->display_x;
        coords->global_y = coords->display_y;
        return;
    }

    source_width = event->width > 0 ? event->width : geometry->width;
    source_height = event->height > 0 ? event->height : geometry->height;
    coords->display_id = geometry->display_id > (uint64_t)INT_MAX ?
        INT_MAX : (int)geometry->display_id;
    coords->full_display_x = ohos_input_scale_coordinate(event->param1,
                                                         source_width,
                                                         geometry->width);
    coords->full_display_y = ohos_input_scale_coordinate(event->param2,
                                                         source_height,
                                                         geometry->height);
    ohos_input_resolve_content_rect(source_width, source_height,
                                    geometry->width, geometry->height,
                                    coords);
    if (coords->content_rect_valid)
    {
        coords->inside_content_rect =
            event->param1 >= coords->content_left &&
            event->param1 < coords->content_left + coords->content_width &&
            event->param2 >= coords->content_top &&
            event->param2 < coords->content_top + coords->content_height;
        coords->content_clamped_x = ohos_input_clamp_long(
            event->param1 - coords->content_left, 0,
            coords->content_width - 1);
        coords->content_clamped_y = ohos_input_clamp_long(
            event->param2 - coords->content_top, 0,
            coords->content_height - 1);
        coords->fit_display_x = ohos_input_scale_coordinate_from_content(
            event->param1, coords->content_left, coords->content_width,
            geometry->width);
        coords->fit_display_y = ohos_input_scale_coordinate_from_content(
            event->param2, coords->content_top, coords->content_height,
            geometry->height);
    }
    else
    {
        coords->fit_display_x = coords->full_display_x;
        coords->fit_display_y = coords->full_display_y;
        coords->inside_content_rect = 1;
    }
    if (coords->content_rect_valid &&
            (coords->content_left != 0 || coords->content_top != 0 ||
             coords->content_width != source_width ||
             coords->content_height != source_height))
    {
        coords->display_x = coords->fit_display_x;
        coords->display_y = coords->fit_display_y;
        coords->content_mapping_active = 1;
    }
    else
    {
        coords->display_x = coords->full_display_x;
        coords->display_y = coords->full_display_y;
    }
    if (geometry->height > 1 && coords->display_y == 0)
    {
        coords->display_y = 1;
    }
    if (geometry->width > 1 && coords->display_x == 0)
    {
        coords->display_x = 1;
    }
    coords->global_x = geometry->origin_x + coords->display_x;
    coords->global_y = geometry->origin_y + coords->display_y;
    coords->source_width = source_width;
    coords->source_height = source_height;
    coords->target_width = geometry->width;
    coords->target_height = geometry->height;
    coords->virtual_pixel_ratio_valid = geometry->virtual_pixel_ratio_valid;
    coords->virtual_pixel_ratio = geometry->virtual_pixel_ratio;
}
