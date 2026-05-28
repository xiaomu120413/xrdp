#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_input.h"

#include "log.h"

#include <stdbool.h>
#include <string.h>
#include <multimodalinput/oh_input_manager.h>

#define OHOS_INPUT_MOVE_INJECT_LOG_INITIAL 20ULL
#define OHOS_INPUT_MOVE_INJECT_LOG_SAMPLE 100ULL

static uint64_t g_mouse_move_inject_log_count = 0;

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

static int
ohos_input_should_log_mouse_move_sample(uint64_t count)
{
    return count <= OHOS_INPUT_MOVE_INJECT_LOG_INITIAL ||
           (count % OHOS_INPUT_MOVE_INJECT_LOG_SAMPLE) == 0ULL;
}

static int
ohos_input_mouse_button_slot(int button)
{
    return button >= 0 && button < OHOS_INPUT_MOUSE_BUTTON_SLOT_COUNT ?
        button : -1;
}

static long
ohos_input_abs_long(long value)
{
    return value < 0 ? -value : value;
}

static long long
ohos_input_delta_ms(int64_t later, int64_t earlier)
{
    if (later <= 0 || earlier <= 0 || later < earlier)
    {
        return 0;
    }
    return (long long)(later - earlier);
}

static int
ohos_input_button_position_needs_move(
    const struct ohos_input_context *ctx,
    const struct xrdp_ohos_input_event *event)
{
    if (ctx == 0 || event == 0)
    {
        return 0;
    }
    return !ctx->has_last_pointer ||
           ctx->last_mouse_x != (int)event->param1 ||
           ctx->last_mouse_y != (int)event->param2 ||
           ctx->last_mouse_width != event->width ||
           ctx->last_mouse_height != event->height;
}

static void
ohos_input_note_pointer_position(struct ohos_input_context *ctx,
                                 const struct xrdp_ohos_input_event *event)
{
    if (ctx == 0 || event == 0)
    {
        return;
    }
    ctx->last_mouse_x = (int)event->param1;
    ctx->last_mouse_y = (int)event->param2;
    ctx->last_mouse_width = event->width;
    ctx->last_mouse_height = event->height;
    ctx->has_last_pointer = 1;
}

static void
ohos_input_store_button_down(
    struct ohos_input_context *ctx,
    const struct xrdp_ohos_input_event *event,
    const struct ohos_input_mouse_dispatch *dispatch,
    const struct ohos_input_mouse_coordinates *coords,
    int64_t action_time)
{
    struct ohos_input_mouse_button_down *down;
    int slot = dispatch == 0 ? -1 : ohos_input_mouse_button_slot(dispatch->button);

    if (ctx == 0 || event == 0 || coords == 0 || slot < 0)
    {
        return;
    }

    down = &ctx->button_down[slot];
    down->active = 1;
    down->trace_id = event->trace_id;
    down->remote_x = event->param1;
    down->remote_y = event->param2;
    down->display_x = coords->display_x;
    down->display_y = coords->display_y;
    down->global_x = coords->global_x;
    down->global_y = coords->global_y;
    down->action_time_ms = action_time;
    down->move_count = ctx->mouse_move_sent_count;

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.input: stage=click_pair state=down trace=%llu button=%d remote=(%ld,%ld) display=(%d,%d) global=(%d,%d) action_time=%lld move_count=%llu",
        (unsigned long long)event->trace_id, dispatch->button,
        event->param1, event->param2,
        coords->display_x, coords->display_y,
        coords->global_x, coords->global_y,
        (long long)action_time,
        (unsigned long long)ctx->mouse_move_sent_count);
}

static void
ohos_input_log_button_up_pair(
    struct ohos_input_context *ctx,
    const struct xrdp_ohos_input_event *event,
    const struct ohos_input_mouse_dispatch *dispatch,
    const struct ohos_input_mouse_coordinates *coords,
    int64_t action_time)
{
    struct ohos_input_mouse_button_down *down;
    int slot = dispatch == 0 ? -1 : ohos_input_mouse_button_slot(dispatch->button);
    long dx_remote;
    long dy_remote;
    int dx_display;
    int dy_display;
    uint64_t moves;
    const char *classification;

    if (ctx == 0 || event == 0 || coords == 0 || slot < 0)
    {
        return;
    }

    down = &ctx->button_down[slot];
    if (!down->active)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.input: stage=click_pair state=up_no_down trace=%llu button=%d remote=(%ld,%ld) display=(%d,%d) global=(%d,%d) action_time=%lld move_count=%llu",
            (unsigned long long)event->trace_id, dispatch->button,
            event->param1, event->param2,
            coords->display_x, coords->display_y,
            coords->global_x, coords->global_y,
            (long long)action_time,
            (unsigned long long)ctx->mouse_move_sent_count);
        return;
    }

    dx_remote = event->param1 - down->remote_x;
    dy_remote = event->param2 - down->remote_y;
    dx_display = coords->display_x - down->display_x;
    dy_display = coords->display_y - down->display_y;
    moves = ctx->mouse_move_sent_count >= down->move_count ?
        ctx->mouse_move_sent_count - down->move_count : 0;
    classification = (ohos_input_abs_long(dx_remote) <= 2 &&
        ohos_input_abs_long(dy_remote) <= 2) ? "click_like" : "drag_like";

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.input: stage=click_pair state=up trace=%llu button=%d down_trace=%llu class=%s down_remote=(%ld,%ld) up_remote=(%ld,%ld) delta_remote=(%ld,%ld) down_display=(%d,%d) up_display=(%d,%d) delta_display=(%d,%d) down_global=(%d,%d) up_global=(%d,%d) dt_ms=%lld moves=%llu down_time=%lld up_time=%lld",
        (unsigned long long)event->trace_id, dispatch->button,
        (unsigned long long)down->trace_id, classification,
        down->remote_x, down->remote_y, event->param1, event->param2,
        dx_remote, dy_remote, down->display_x, down->display_y,
        coords->display_x, coords->display_y, dx_display, dy_display,
        down->global_x, down->global_y, coords->global_x, coords->global_y,
        ohos_input_delta_ms(action_time, down->action_time_ms),
        (unsigned long long)moves,
        (long long)down->action_time_ms, (long long)action_time);

    memset(down, 0, sizeof(*down));
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
ohos_input_is_key_tracked(const struct ohos_input_context *ctx, int key_code)
{
    int i;

    if (ctx == 0 || key_code < 0)
    {
        return 0;
    }

    for (i = 0; i < ctx->pressed_key_count; ++i)
    {
        if (ctx->pressed_keys[i] == key_code)
        {
            return 1;
        }
    }
    return 0;
}

static int
ohos_input_inject_key_code(int key_code, int down, int64_t *action_time_out)
{
    struct Input_KeyEvent *key_event;
    int64_t action_time;
    int rc;

    key_event = OH_Input_CreateKeyEvent();
    if (key_event == 0)
    {
        return INPUT_PARAMETER_ERROR;
    }

    OH_Input_SetKeyEventAction(key_event,
                               down ? KEY_ACTION_DOWN : KEY_ACTION_UP);
    OH_Input_SetKeyEventKeyCode(key_event, key_code);
    action_time = (int64_t)ohos_input_now_ms();
    OH_Input_SetKeyEventActionTime(key_event, action_time);
    if (action_time_out != 0)
    {
        *action_time_out = action_time;
    }

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
    int64_t action_time = 0;
    int rc;

    down = event->msg == XRDP_OHOS_WM_KEYDOWN;
    rc = ohos_input_inject_key_code(key_code, down, &action_time);
    if (rc == INPUT_SUCCESS)
    {
        ohos_input_track_key(ctx, key_code, down);
        ctx->key_sent_count++;
        if (ohos_input_should_log_result(ctx, 1, 0))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=inject_result trace=%llu kind=key key_code=%d %s action_time=%lld scancode=%ld keysym=%ld rc=%d",
                (unsigned long long)event->trace_id,
                key_code, down ? "down" : "up",
                (long long)action_time,
                event->param3, event->param2, rc);
        }
    }
    else
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.input: stage=inject_result trace=%llu kind=key result=failed key_code=%d %s action_time=%lld scancode=%ld keysym=%ld rc=%d",
            (unsigned long long)event->trace_id,
            key_code, down ? "down" : "up",
            (long long)action_time,
            event->param3, event->param2, rc);
    }
    return rc;
}

static int
ohos_input_inject_mouse_once(
    const struct xrdp_ohos_input_event *event,
    const struct ohos_input_mouse_dispatch *dispatch,
    const struct ohos_input_mouse_coordinates *coords,
    int action, float axis_value, int64_t *action_time_out)
{
    struct Input_MouseEvent *mouse_event;
    int64_t local_time_ms;
    int should_log;
    uint64_t move_log_count = 0;
    int location_rc = INPUT_SUCCESS;
    int32_t actual_display_id = -1;
    double actual_x = 0.0;
    double actual_y = 0.0;
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
    local_time_ms = (int64_t)ohos_input_now_ms();
    if (action_time_out != 0)
    {
        *action_time_out = local_time_ms;
    }
    if (dispatch->wheel)
    {
        OH_Input_SetMouseEventAxisType(mouse_event, dispatch->axis_type);
        OH_Input_SetMouseEventAxisValue(mouse_event, axis_value);
    }

    if (event != 0 && event->msg == XRDP_OHOS_WM_MOUSEMOVE)
    {
        move_log_count = ++g_mouse_move_inject_log_count;
        should_log = ohos_input_should_log_mouse_move_sample(move_log_count);
    }
    else
    {
        should_log = event != 0;
    }
    if (should_log)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.input: stage=inject_call trace=%llu msg=%d action=%d button=%d local_time_ms=%lld requested_action_time=-1 display=(%d,%d) global=(%d,%d) axis_type=%d axis_value=%.1f wheel=%d move_log_count=%llu",
            (unsigned long long)event->trace_id,
            event->msg, action, dispatch->button,
            (long long)local_time_ms,
            coords->display_x, coords->display_y,
            coords->global_x, coords->global_y,
            dispatch->axis_type, axis_value, dispatch->wheel,
            (unsigned long long)move_log_count);
    }
    rc = OH_Input_InjectMouseEventGlobal(mouse_event);
    if (should_log || rc != INPUT_SUCCESS)
    {
        location_rc = OH_Input_GetPointerLocation(&actual_display_id,
                                                  &actual_x, &actual_y);
        LOG(rc == INPUT_SUCCESS ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR,
            "xrdp.ohos.input: stage=inject_result trace=%llu msg=%d action=%d button=%d local_time_ms=%lld requested_action_time=-1 rc=%d display=(%d,%d) global=(%d,%d) actual_rc=%d actual=(%d,%.1f,%.1f) actual_delta=(%.1f,%.1f) axis_type=%d axis_value=%.1f wheel=%d move_log_count=%llu",
            (unsigned long long)(event == 0 ? 0ULL : event->trace_id),
            event == 0 ? 0 : event->msg,
            action, dispatch->button,
            (long long)local_time_ms, rc,
            coords->display_x, coords->display_y,
            coords->global_x, coords->global_y,
            location_rc, actual_display_id, actual_x, actual_y,
            actual_x - (double)coords->display_x,
            actual_y - (double)coords->display_y,
            dispatch->axis_type, axis_value, dispatch->wheel,
            (unsigned long long)move_log_count);
    }
    OH_Input_DestroyMouseEvent(&mouse_event);
    if (rc == INPUT_PERMISSION_DENIED)
    {
        ohos_input_mark_unauthorized();
    }
    return rc;
}

static int
ohos_input_inject_pointer_move_before_button(
    struct ohos_input_context *ctx,
    const struct xrdp_ohos_input_event *event,
    const struct ohos_input_mouse_dispatch *dispatch,
    const struct ohos_input_mouse_coordinates *coords)
{
    struct ohos_input_mouse_dispatch move_dispatch;
    int64_t action_time = 0;
    int rc;

    if (ctx == 0 || event == 0 || dispatch == 0 || coords == 0 ||
        dispatch->button_mask == 0 ||
        (dispatch->action != MOUSE_ACTION_BUTTON_DOWN &&
         dispatch->action != MOUSE_ACTION_BUTTON_UP) ||
        !ohos_input_button_position_needs_move(ctx, event))
    {
        return INPUT_SUCCESS;
    }

    move_dispatch = *dispatch;
    move_dispatch.wheel = 0;
    move_dispatch.action = MOUSE_ACTION_MOVE;
    move_dispatch.axis_type = MOUSE_AXIS_SCROLL_VERTICAL;
    move_dispatch.axis_value = 0.0F;
    move_dispatch.begin_axis_before_update = 0;
    move_dispatch.end_axis_after_update = 0;
    move_dispatch.button_mask = 0;
    move_dispatch.button_down = 0;
    move_dispatch.button = dispatch->action == MOUSE_ACTION_BUTTON_UP ?
        ohos_input_active_mouse_button_from_mask(ctx->pressed_mouse_buttons) :
        ohos_input_active_mouse_button_from_mask(
            ctx->pressed_mouse_buttons & ~dispatch->button_mask);

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.input: stage=button_pre_move trace=%llu msg=%d button=%d remote=(%ld,%ld) last=(%d,%d %dx%d) display=(%d,%d) global=(%d,%d)",
        (unsigned long long)event->trace_id, event->msg, dispatch->button,
        event->param1, event->param2,
        ctx->has_last_pointer ? ctx->last_mouse_x : -1,
        ctx->has_last_pointer ? ctx->last_mouse_y : -1,
        ctx->has_last_pointer ? ctx->last_mouse_width : 0,
        ctx->has_last_pointer ? ctx->last_mouse_height : 0,
        coords->display_x, coords->display_y,
        coords->global_x, coords->global_y);

    rc = ohos_input_inject_mouse_once(event, &move_dispatch, coords,
                                      MOUSE_ACTION_MOVE, 0.0F, &action_time);
    if (rc == INPUT_SUCCESS)
    {
        ctx->mouse_move_sent_count++;
        ohos_input_note_pointer_position(ctx, event);
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
    int64_t begin_action_time = 0;
    int64_t action_time = 0;
    int64_t end_action_time = 0;
    uint32_t buttons_before = ctx == 0 ? 0U : ctx->pressed_mouse_buttons;
    uint32_t buttons_after;
    int move_before_rc = INPUT_SUCCESS;

    move_before_rc = ohos_input_inject_pointer_move_before_button(
        ctx, event, dispatch, coords);
    if (move_before_rc != INPUT_SUCCESS)
    {
        rc = move_before_rc;
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.input: button pre-move failed trace=%llu msg=%d action=%d button=%d rc=%d",
            (unsigned long long)event->trace_id,
            event->msg, dispatch->action, dispatch->button,
            move_before_rc);
        return rc;
    }
    if (dispatch->begin_axis_before_update)
    {
        begin_rc = ohos_input_inject_mouse_once(event, dispatch, coords,
                                                MOUSE_ACTION_AXIS_BEGIN, 0.0F,
                                                &begin_action_time);
    }
    rc = begin_rc == INPUT_SUCCESS ?
        ohos_input_inject_mouse_once(event, dispatch, coords,
                                     dispatch->action,
                                     dispatch->axis_value,
                                     &action_time) : begin_rc;
    if (rc == INPUT_SUCCESS && dispatch->end_axis_after_update)
    {
        end_rc = ohos_input_inject_mouse_once(event, dispatch, coords,
                                              MOUSE_ACTION_AXIS_END, 0.0F,
                                              &end_action_time);
    }

    if (begin_rc == INPUT_SUCCESS && rc == INPUT_SUCCESS &&
        end_rc == INPUT_SUCCESS)
    {
        ctx->mouse_sent_count++;
        if (event->msg == XRDP_OHOS_WM_MOUSEMOVE)
        {
            ctx->mouse_move_sent_count++;
        }
        ohos_input_note_pointer_position(ctx, event);
        if (dispatch->button_mask != 0)
        {
            if (dispatch->button_down)
            {
                ctx->pressed_mouse_buttons |= dispatch->button_mask;
                ohos_input_store_button_down(ctx, event, dispatch, coords,
                                             action_time);
            }
            else
            {
                ohos_input_log_button_up_pair(ctx, event, dispatch, coords,
                                              action_time);
                ctx->pressed_mouse_buttons &= ~dispatch->button_mask;
            }
        }
        buttons_after = ctx->pressed_mouse_buttons;
        if (ohos_input_should_log_result(ctx, 1, event->msg != XRDP_OHOS_WM_MOUSEMOVE))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: pointer inject trace=%llu mode=mouse msg=%d action=%d button=%d local_time_ms=%lld begin_local_ms=%lld end_local_ms=%lld requested_action_time=-1 pressed=0x%8.8x->0x%8.8x remote=(%ld,%ld) active=%s display=(%d,%d) global=(%d,%d) full=(%d,%d) fit=(%d,%d) source=%dx%d target=%dx%d fit_rect=%d rect=(%d,%d %dx%d) fit_clamped=(%d,%d) inside=%d vpr_valid=%d vpr=%.3f axis_type=%d axis_value=%.1f rc=%d end_rc=%d",
                (unsigned long long)event->trace_id,
                event->msg, dispatch->action, dispatch->button,
                (long long)action_time,
                (long long)begin_action_time,
                (long long)end_action_time,
                buttons_before, buttons_after,
                event->param1, event->param2,
                coords->content_mapping_active ? "content-fit" :
                    "full-scale",
                coords->display_x, coords->display_y,
                coords->global_x, coords->global_y,
                coords->full_display_x, coords->full_display_y,
                coords->fit_display_x, coords->fit_display_y,
                coords->source_width, coords->source_height,
                coords->target_width, coords->target_height,
                coords->content_rect_valid,
                coords->content_left, coords->content_top,
                coords->content_width, coords->content_height,
                coords->content_clamped_x, coords->content_clamped_y,
                coords->inside_content_rect,
                coords->virtual_pixel_ratio_valid,
                coords->virtual_pixel_ratio,
                dispatch->axis_type, dispatch->axis_value, rc, end_rc);
        }
    }
    else
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.input: mouse inject failed trace=%llu msg=%d action=%d button=%d local_time_ms=%lld begin_local_ms=%lld end_local_ms=%lld requested_action_time=-1 rc=%d begin_rc=%d end_rc=%d",
            (unsigned long long)event->trace_id,
            event->msg, dispatch->action, dispatch->button,
            (long long)action_time,
            (long long)begin_action_time,
            (long long)end_action_time, rc, begin_rc, end_rc);
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
    ctx->mouse_move_sent_count = 0;
    ctx->log_count = 0;
    ctx->pressed_key_count = 0;
    ctx->pressed_mouse_buttons = 0;
    ctx->has_last_pointer = 0;
    ctx->last_mouse_x = 0;
    ctx->last_mouse_y = 0;
    ctx->last_mouse_width = 0;
    ctx->last_mouse_height = 0;
    memset(ctx->button_down, 0, sizeof(ctx->button_down));
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
    uint64_t event_seq;
    uint64_t trace_id;

    if (ctx == 0 || event == 0 || !ohos_input_is_input_event(event))
    {
        return 0;
    }

    ctx->handled_count++;
    event_seq = ctx->handled_count;
    trace_id = event->trace_id;
    if (event->msg == XRDP_OHOS_WM_KEYDOWN ||
        event->msg == XRDP_OHOS_WM_KEYUP)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.input: stage=input_recv trace=%llu seq=%llu kind=key msg=%d flags=%ld keysym=%ld scancode=%ld extra=%ld connected=%d desktop=%dx%d",
            (unsigned long long)trace_id,
            (unsigned long long)event_seq, event->msg, event->param1,
            event->param2, event->param3, event->param4, event->connected,
            event->width, event->height);
        key_code = ohos_input_map_key(event);
        if (key_code < 0)
        {
            ctx->unmapped_count++;
            ctx->dropped_count++;
            LOG(LOG_LEVEL_ERROR,
                "xrdp.ohos.input: stage=drop trace=%llu seq=%llu kind=key reason=unmapped msg=%d flags=%ld keysym=%ld scancode=%ld extra=%ld",
                (unsigned long long)trace_id,
                (unsigned long long)event_seq,
                event->msg, event->param1, event->param2,
                event->param3, event->param4);
            return 1;
        }
        if (event->msg == XRDP_OHOS_WM_KEYUP &&
            !ohos_input_is_key_tracked(ctx, key_code))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=ignore trace=%llu seq=%llu kind=key reason=untracked_key_up key_code=%d flags=%ld keysym=%ld scancode=%ld extra=%ld pressed_keys=%d pressed_buttons=0x%8.8x",
                (unsigned long long)trace_id,
                (unsigned long long)event_seq,
                key_code, event->param1, event->param2,
                event->param3, event->param4,
                ctx->pressed_key_count, ctx->pressed_mouse_buttons);
            return 0;
        }
        if (!ohos_input_ensure_authorized("key event"))
        {
            ctx->auth_pending_count++;
            ctx->dropped_count++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=drop trace=%llu seq=%llu kind=key reason=auth_pending msg=%d auth_pending=%llu dropped=%llu",
                (unsigned long long)trace_id,
                (unsigned long long)event_seq, event->msg,
                (unsigned long long)ctx->auth_pending_count,
                (unsigned long long)ctx->dropped_count);
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
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.input: stage=drop trace=%llu seq=%llu kind=mouse reason=unsupported msg=%d remote=(%ld,%ld)",
            (unsigned long long)trace_id,
            (unsigned long long)event_seq, event->msg,
            event->param1, event->param2);
        return 0;
    }
    if (!ohos_input_ensure_authorized("mouse event"))
    {
        ctx->auth_pending_count++;
        ctx->dropped_count++;
        if (event->msg != XRDP_OHOS_WM_MOUSEMOVE ||
            event_seq <= 5ULL || (event_seq % 200ULL) == 0ULL)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=drop trace=%llu seq=%llu kind=mouse reason=auth_pending msg=%d remote=(%ld,%ld) auth_pending=%llu dropped=%llu",
                (unsigned long long)trace_id,
                (unsigned long long)event_seq, event->msg,
                event->param1, event->param2,
                (unsigned long long)ctx->auth_pending_count,
                (unsigned long long)ctx->dropped_count);
        }
        return 1;
    }

    if (ohos_input_is_mouse_move(event) && dispatch.button < 0)
    {
        dispatch.button =
            ohos_input_active_mouse_button_from_mask(ctx->pressed_mouse_buttons);
    }
    ohos_input_resolve_mouse_coordinates(ctx, event, &coords);
    if (event->msg != XRDP_OHOS_WM_MOUSEMOVE ||
        event_seq <= 10ULL || (event_seq % 200ULL) == 0ULL)
    {
            LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.input: stage=input_mapped trace=%llu seq=%llu msg=%d action=%d button=%d button_mask=0x%8.8x down=%d pressed_before=0x%8.8x remote=(%ld,%ld) active=%s display=(%d,%d) global=(%d,%d) full=(%d,%d) fit=(%d,%d) desktop=%dx%d target=%dx%d fit_rect=%d rect=(%d,%d %dx%d) fit_clamped=(%d,%d) inside=%d",
            (unsigned long long)trace_id,
            (unsigned long long)event_seq, event->msg, dispatch.action,
            dispatch.button, dispatch.button_mask, dispatch.button_down,
            ctx->pressed_mouse_buttons, event->param1, event->param2,
            coords.content_mapping_active ? "content-fit" :
                "full-scale",
            coords.display_x, coords.display_y, coords.global_x,
            coords.global_y, coords.full_display_x, coords.full_display_y,
            coords.fit_display_x, coords.fit_display_y,
            event->width, event->height,
            coords.target_width, coords.target_height,
            coords.content_rect_valid, coords.content_left, coords.content_top,
            coords.content_width, coords.content_height,
            coords.content_clamped_x, coords.content_clamped_y,
            coords.inside_content_rect);
    }
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
        rc = ohos_input_inject_key_code(key_code, 0, 0);
        LOG(rc == INPUT_SUCCESS ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR,
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
    event.trace_id = 0;
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
    LOG(rc == INPUT_SUCCESS ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR,
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
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.input: clear pressed state without release reason=%s keys=%d buttons=0x%8.8x",
            reason == 0 ? "" : reason, ctx->pressed_key_count,
            pressed_buttons);
    }

    ctx->pressed_key_count = 0;
    ctx->pressed_mouse_buttons = 0;
    ctx->has_last_pointer = 0;
    ctx->mouse_move_sent_count = 0;
    memset(ctx->button_down, 0, sizeof(ctx->button_down));
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
