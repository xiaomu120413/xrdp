#ifndef XRDP_OHOS_INPUT_H
#define XRDP_OHOS_INPUT_H

#include "xrdp_ohos.h"

#include <stdint.h>

#define OHOS_INPUT_MAX_PRESSED_KEYS 64
#define OHOS_INPUT_MOUSE_LEFT_MASK 0x00000001U
#define OHOS_INPUT_MOUSE_MIDDLE_MASK 0x00000002U
#define OHOS_INPUT_MOUSE_RIGHT_MASK 0x00000004U
#define OHOS_INPUT_MOUSE_FORWARD_MASK 0x00000008U
#define OHOS_INPUT_MOUSE_BACK_MASK 0x00000010U

struct ohos_input_context
{
    uint64_t handled_count;
    uint64_t sent_count;
    uint64_t dropped_count;
    uint64_t auth_pending_count;
    uint64_t unmapped_count;
    uint64_t key_sent_count;
    uint64_t mouse_sent_count;
    uint64_t log_count;
    int pressed_keys[OHOS_INPUT_MAX_PRESSED_KEYS];
    int pressed_key_count;
    uint32_t pressed_mouse_buttons;
    int has_last_pointer;
    int last_mouse_x;
    int last_mouse_y;
    int last_mouse_width;
    int last_mouse_height;
    struct xrdp_ohos_display_geometry geometry;
    uint64_t geometry_query_ms;
};

struct ohos_input_mouse_coordinates
{
    int display_id;
    int display_x;
    int display_y;
    int global_x;
    int global_y;
    int source_width;
    int source_height;
    int target_width;
    int target_height;
    int content_rect_valid;
    int content_left;
    int content_top;
    int content_width;
    int content_height;
    int virtual_pixel_ratio_valid;
    float virtual_pixel_ratio;
};

struct ohos_input_mouse_dispatch
{
    int supported;
    int wheel;
    int action;
    int button;
    int axis_type;
    float axis_value;
    int begin_axis_before_update;
    int end_axis_after_update;
    uint32_t button_mask;
    int button_down;
};

void
ohos_input_init(struct ohos_input_context *ctx);

void
ohos_input_deinit(struct ohos_input_context *ctx);

void
ohos_input_start_session(struct ohos_input_context *ctx);

void
ohos_input_prime_authorization(const char *reason);

void
ohos_input_reset(struct ohos_input_context *ctx, const char *reason);

int
ohos_input_handle_event(struct ohos_input_context *ctx,
                        const struct xrdp_ohos_input_event *event);

void
ohos_input_log_summary(const struct ohos_input_context *ctx,
                       const char *reason);

uint64_t
ohos_input_now_ms(void);

int64_t
ohos_input_next_mouse_action_time(void);

int
ohos_input_ensure_authorized(const char *reason);

int
ohos_input_refresh_authorized_status(void);

void
ohos_input_mark_unauthorized(void);

int
ohos_input_map_key(const struct xrdp_ohos_input_event *event);

int
ohos_input_is_input_event(const struct xrdp_ohos_input_event *event);

int
ohos_input_is_mouse_move(const struct xrdp_ohos_input_event *event);

struct ohos_input_mouse_dispatch
ohos_input_map_mouse(const struct xrdp_ohos_input_event *event);

void
ohos_input_resolve_mouse_coordinates(
    struct ohos_input_context *ctx,
    const struct xrdp_ohos_input_event *event,
    struct ohos_input_mouse_coordinates *coordinates);

int
ohos_input_active_mouse_button_from_mask(uint32_t mask);

int
ohos_input_mouse_button_up_message_from_mask(uint32_t mask);

#endif
