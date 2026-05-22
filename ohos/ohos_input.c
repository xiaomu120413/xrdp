#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_input.h"

#include "log.h"

#include <stdbool.h>
#include <string.h>
#include <multimodalinput/oh_input_manager.h>

static int
ohos_input_should_log_result(struct ohos_input_context *ctx, int ok,
                             int important)
{
    if (important || !ok)
    {
        return 1;
    }
    if (ctx == 0)
    {
        return 0;
    }
    ctx->log_count++;
    return ctx->log_count <= 60ULL || (ctx->log_count % 200ULL) == 0ULL;
}

static void
ohos_input_track_key(struct ohos_input_context *ctx, int key_code, int down)
{
    int i;

    if (ctx == 0 || key_code < 0)
    {
        return;
    }

    for (i = 0; i < ctx->pressed_key_count; ++i)
    {
        if (ctx->pressed_keys[i] == key_code)
        {
            if (!down)
            {
                ctx->pressed_keys[i] =
                    ctx->pressed_keys[ctx->pressed_key_count - 1];
                ctx->pressed_key_count--;
            }
            return;
        }
    }

    if (down && ctx->pressed_key_count < OHOS_INPUT_MAX_PRESSED_KEYS)
    {
        ctx->pressed_keys[ctx->pressed_key_count++] = key_code;
    }
}

static int
ohos_input_inject_key_code(int key_code, int down)
{
    struct Input_KeyEvent *key_event;
    int rc;

    key_event = OH_Input_CreateKeyEvent();
    if (key_event == 0)
    {
        return INPUT_PARAMETER_ERROR;
    }

    OH_Input_SetKeyEventAction(key_event,
                               down ? KEY_ACTION_DOWN : KEY_ACTION_UP);
    OH_Input_SetKeyEventKeyCode(key_event, key_code);
    OH_Input_SetKeyEventActionTime(key_event, (int64_t)ohos_input_now_ms());

    rc = OH_Input_InjectKeyEvent(key_event);
    OH_Input_DestroyKeyEvent(&key_event);
    if (rc == INPUT_PERMISSION_DENIED)
    {
        ohos_input_mark_unauthorized();
    }
    return rc;
}

static int
ohos_input_inject_key(struct ohos_input_context *ctx,
                      const struct xrdp_ohos_input_event *event,
                      int key_code)
{
    int down;
    int rc;

    down = event->msg == XRDP_OHOS_WM_KEYDOWN;
    rc = ohos_input_inject_key_code(key_code, down);
    if (rc == INPUT_SUCCESS)
    {
        ohos_input_track_key(ctx, key_code, down);
        ctx->key_sent_count++;
        if (ohos_input_should_log_result(ctx, 1, 0))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: key inject key_code=%d %s scancode=%ld keysym=%ld rc=%d",
                key_code, down ? "down" : "up",
                event->param3, event->param2, rc);
        }
    }
    else
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.input: key inject failed key_code=%d %s scancode=%ld keysym=%ld rc=%d",
            key_code, down ? "down" : "up",
            event->param3, event->param2, rc);
    }
    return rc;
}

static int
ohos_input_inject_mouse_once(
    const struct ohos_input_mouse_dispatch *dispatch,
    const struct ohos_input_mouse_coordinates *coords,
    int action, float axis_value)
{
    struct Input_MouseEvent *mouse_event;
    int ordered_button_sequence;
    int rc;

    mouse_event = OH_Input_CreateMouseEvent();
    if (mouse_event == 0)
    {
        return INPUT_PARAMETER_ERROR;
    }

    OH_Input_SetMouseEventAction(mouse_event, action);
    OH_Input_SetMouseEventDisplayX(mouse_event, coords->display_x);
    OH_Input_SetMouseEventDisplayY(mouse_event, coords->display_y);
    OH_Input_SetMouseEventDisplayId(mouse_event, coords->display_id);
    OH_Input_SetMouseEventGlobalX(mouse_event, coords->global_x);
    OH_Input_SetMouseEventGlobalY(mouse_event, coords->global_y);
    OH_Input_SetMouseEventButton(mouse_event, dispatch->button);
    ordered_button_sequence = action == MOUSE_ACTION_BUTTON_DOWN ||
        action == MOUSE_ACTION_BUTTON_UP;
    OH_Input_SetMouseEventActionTime(mouse_event, ordered_button_sequence ?
                                     ohos_input_next_mouse_action_time() :
                                     (int64_t)ohos_input_now_ms());
    if (dispatch->wheel)
    {
        OH_Input_SetMouseEventAxisType(mouse_event, dispatch->axis_type);
        OH_Input_SetMouseEventAxisValue(mouse_event, axis_value);
    }

    rc = OH_Input_InjectMouseEventGlobal(mouse_event);
    OH_Input_DestroyMouseEvent(&mouse_event);
    if (rc == INPUT_PERMISSION_DENIED)
    {
        ohos_input_mark_unauthorized();
    }
    return rc;
}

static int
ohos_input_inject_mouse(struct ohos_input_context *ctx,
                        const struct xrdp_ohos_input_event *event,
                        const struct ohos_input_mouse_dispatch *dispatch,
                        const struct ohos_input_mouse_coordinates *coords)
{
    int begin_rc = INPUT_SUCCESS;
    int rc;
    int end_rc = INPUT_SUCCESS;

    if (dispatch->begin_axis_before_update)
    {
        begin_rc = ohos_input_inject_mouse_once(dispatch, coords,
                                                MOUSE_ACTION_AXIS_BEGIN, 0.0F);
    }
    rc = begin_rc == INPUT_SUCCESS ?
        ohos_input_inject_mouse_once(dispatch, coords, dispatch->action,
                                     dispatch->axis_value) : begin_rc;
    if (rc == INPUT_SUCCESS && dispatch->end_axis_after_update)
    {
        end_rc = ohos_input_inject_mouse_once(dispatch, coords,
                                              MOUSE_ACTION_AXIS_END, 0.0F);
    }

    if (begin_rc == INPUT_SUCCESS && rc == INPUT_SUCCESS &&
        end_rc == INPUT_SUCCESS)
    {
        ctx->mouse_sent_count++;
        ctx->last_mouse_x = (int)event->param1;
        ctx->last_mouse_y = (int)event->param2;
        ctx->last_mouse_width = event->width;
        ctx->last_mouse_height = event->height;
        ctx->has_last_pointer = 1;
        if (dispatch->button_mask != 0)
        {
            if (dispatch->button_down)
            {
                ctx->pressed_mouse_buttons |= dispatch->button_mask;
            }
            else
            {
                ctx->pressed_mouse_buttons &= ~dispatch->button_mask;
            }
        }
        if (ohos_input_should_log_result(ctx, 1, event->msg != XRDP_OHOS_WM_MOUSEMOVE))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: mouse inject msg=%d action=%d button=%d remote=(%ld,%ld) display=(%d,%d) global=(%d,%d) source=%dx%d target=%dx%d axis_type=%d axis_value=%.1f rc=%d end_rc=%d",
                event->msg, dispatch->action, dispatch->button,
                event->param1, event->param2,
                coords->display_x, coords->display_y,
                coords->global_x, coords->global_y,
                coords->source_width, coords->source_height,
                coords->target_width, coords->target_height,
                dispatch->axis_type, dispatch->axis_value, rc, end_rc);
        }
    }
    else
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.input: mouse inject failed msg=%d action=%d button=%d rc=%d begin_rc=%d end_rc=%d",
            event->msg, dispatch->action, dispatch->button,
            rc, begin_rc, end_rc);
    }
    return begin_rc == INPUT_SUCCESS && rc == INPUT_SUCCESS &&
           end_rc == INPUT_SUCCESS ? INPUT_SUCCESS : rc;
}

void
ohos_input_start_session(struct ohos_input_context *ctx)
{
    if (ctx == 0)
    {
        return;
    }

    ctx->handled_count = 0;
    ctx->sent_count = 0;
    ctx->dropped_count = 0;
    ctx->auth_pending_count = 0;
    ctx->unmapped_count = 0;
    ctx->key_sent_count = 0;
    ctx->mouse_sent_count = 0;
    ctx->log_count = 0;
    ctx->pressed_key_count = 0;
    ctx->pressed_mouse_buttons = 0;
    ctx->has_last_pointer = 0;
    ctx->last_mouse_x = 0;
    ctx->last_mouse_y = 0;
    ctx->last_mouse_width = 0;
    ctx->last_mouse_height = 0;
    ctx->geometry.valid = 0;
    ctx->geometry_query_ms = 0;
}

void
ohos_input_init(struct ohos_input_context *ctx)
{
    if (ctx == 0)
    {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ohos_input_start_session(ctx);
}

void
ohos_input_deinit(struct ohos_input_context *ctx)
{
    ohos_input_reset(ctx, "deinit");
}

int
ohos_input_is_input_event(const struct xrdp_ohos_input_event *event)
{
    if (event == 0 ||
        event->msg == XRDP_OHOS_INPUT_SESSION_CONNECT ||
        event->msg == XRDP_OHOS_INPUT_SESSION_DISCONNECT)
    {
        return 0;
    }
    return event->msg == XRDP_OHOS_WM_KEYDOWN ||
           event->msg == XRDP_OHOS_WM_KEYUP ||
           (event->msg >= XRDP_OHOS_WM_MOUSEMOVE &&
            event->msg <= XRDP_OHOS_WM_XBUTTON2DOWN) ||
           event->msg == XRDP_OHOS_WM_TOUCH_VSCROLL ||
           event->msg == XRDP_OHOS_WM_TOUCH_HSCROLL;
}

int
ohos_input_handle_event(struct ohos_input_context *ctx,
                        const struct xrdp_ohos_input_event *event)
{
    struct ohos_input_mouse_dispatch dispatch;
    struct ohos_input_mouse_coordinates coords;
    int key_code;
    int rc;

    if (ctx == 0 || event == 0 || !ohos_input_is_input_event(event))
    {
        return 0;
    }

    ctx->handled_count++;
    if (event->msg == XRDP_OHOS_WM_KEYDOWN ||
        event->msg == XRDP_OHOS_WM_KEYUP)
    {
        key_code = ohos_input_map_key(event);
        if (key_code < 0)
        {
            ctx->unmapped_count++;
            ctx->dropped_count++;
            LOG(LOG_LEVEL_ERROR,
                "xrdp.ohos.input: unmapped key msg=%d flags=%ld keysym=%ld scancode=%ld extra=%ld",
                event->msg, event->param1, event->param2,
                event->param3, event->param4);
            return 1;
        }
        if (!ohos_input_ensure_authorized("key event"))
        {
            ctx->auth_pending_count++;
            ctx->dropped_count++;
            return 1;
        }
        rc = ohos_input_inject_key(ctx, event, key_code);
        if (rc == INPUT_SUCCESS)
        {
            ctx->sent_count++;
            return 0;
        }
        ctx->dropped_count++;
        return 1;
    }

    dispatch = ohos_input_map_mouse(event);
    if (!dispatch.supported)
    {
        return 0;
    }
    if (!ohos_input_ensure_authorized("mouse event"))
    {
        ctx->auth_pending_count++;
        ctx->dropped_count++;
        return 1;
    }

    if (ohos_input_is_mouse_move(event) && dispatch.button < 0)
    {
        dispatch.button =
            ohos_input_active_mouse_button_from_mask(ctx->pressed_mouse_buttons);
    }
    ohos_input_resolve_mouse_coordinates(ctx, event, &coords);
    rc = ohos_input_inject_mouse(ctx, event, &dispatch, &coords);
    if (rc == INPUT_SUCCESS)
    {
        ctx->sent_count++;
        return 0;
    }
    ctx->dropped_count++;
    return 1;
}

static void
ohos_input_release_pressed_keys(struct ohos_input_context *ctx)
{
    int i;
    int key_code;
    int rc;

    while (ctx->pressed_key_count > 0)
    {
        i = ctx->pressed_key_count - 1;
        key_code = ctx->pressed_keys[i];
        rc = ohos_input_inject_key_code(key_code, 0);
        LOG(rc == INPUT_SUCCESS ? LOG_LEVEL_INFO : LOG_LEVEL_ERROR,
            "xrdp.ohos.input: release key key_code=%d rc=%d",
            key_code, rc);
        ctx->pressed_key_count--;
    }
}

static void
ohos_input_release_mouse_button(struct ohos_input_context *ctx, uint32_t mask)
{
    struct xrdp_ohos_input_event event;
    struct ohos_input_mouse_dispatch dispatch;
    struct ohos_input_mouse_coordinates coords;
    int rc;

    event.version = XRDP_OHOS_INPUT_EVENT_VERSION;
    event.msg = ohos_input_mouse_button_up_message_from_mask(mask);
    event.param1 = ctx->has_last_pointer ? ctx->last_mouse_x : 0;
    event.param2 = ctx->has_last_pointer ? ctx->last_mouse_y : 0;
    event.param3 = 0;
    event.param4 = 0;
    event.width = ctx->has_last_pointer ? ctx->last_mouse_width : 0;
    event.height = ctx->has_last_pointer ? ctx->last_mouse_height : 0;
    event.bpp = 0;
    event.connected = 0;
    dispatch = ohos_input_map_mouse(&event);
    ohos_input_resolve_mouse_coordinates(ctx, &event, &coords);
    rc = ohos_input_inject_mouse(ctx, &event, &dispatch, &coords);
    LOG(rc == INPUT_SUCCESS ? LOG_LEVEL_INFO : LOG_LEVEL_ERROR,
        "xrdp.ohos.input: release mouse mask=0x%8.8x rc=%d",
        mask, rc);
}

void
ohos_input_reset(struct ohos_input_context *ctx, const char *reason)
{
    uint32_t pressed_buttons;

    if (ctx == 0)
    {
        return;
    }

    pressed_buttons = ctx->pressed_mouse_buttons;
    if ((ctx->pressed_key_count > 0 || pressed_buttons != 0) &&
        ohos_input_refresh_authorized_status())
    {
        ohos_input_release_pressed_keys(ctx);
        if ((pressed_buttons & OHOS_INPUT_MOUSE_LEFT_MASK) != 0U)
        {
            ohos_input_release_mouse_button(ctx, OHOS_INPUT_MOUSE_LEFT_MASK);
        }
        if ((pressed_buttons & OHOS_INPUT_MOUSE_MIDDLE_MASK) != 0U)
        {
            ohos_input_release_mouse_button(ctx, OHOS_INPUT_MOUSE_MIDDLE_MASK);
        }
        if ((pressed_buttons & OHOS_INPUT_MOUSE_RIGHT_MASK) != 0U)
        {
            ohos_input_release_mouse_button(ctx, OHOS_INPUT_MOUSE_RIGHT_MASK);
        }
        if ((pressed_buttons & OHOS_INPUT_MOUSE_FORWARD_MASK) != 0U)
        {
            ohos_input_release_mouse_button(ctx, OHOS_INPUT_MOUSE_FORWARD_MASK);
        }
        if ((pressed_buttons & OHOS_INPUT_MOUSE_BACK_MASK) != 0U)
        {
            ohos_input_release_mouse_button(ctx, OHOS_INPUT_MOUSE_BACK_MASK);
        }
    }
    else if (ctx->pressed_key_count > 0 || pressed_buttons != 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.input: clear pressed state without release reason=%s keys=%d buttons=0x%8.8x",
            reason == 0 ? "" : reason, ctx->pressed_key_count,
            pressed_buttons);
    }

    ctx->pressed_key_count = 0;
    ctx->pressed_mouse_buttons = 0;
    ctx->has_last_pointer = 0;
}

void
ohos_input_log_summary(const struct ohos_input_context *ctx, const char *reason)
{
    if (ctx == 0)
    {
        return;
    }

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.input: summary reason=%s handled=%llu sent=%llu key_sent=%llu mouse_sent=%llu dropped=%llu auth_pending=%llu unmapped=%llu pressed_keys=%d pressed_buttons=0x%8.8x",
        reason == 0 ? "" : reason,
        (unsigned long long)ctx->handled_count,
        (unsigned long long)ctx->sent_count,
        (unsigned long long)ctx->key_sent_count,
        (unsigned long long)ctx->mouse_sent_count,
        (unsigned long long)ctx->dropped_count,
        (unsigned long long)ctx->auth_pending_count,
        (unsigned long long)ctx->unmapped_count,
        ctx->pressed_key_count,
        ctx->pressed_mouse_buttons);
}
