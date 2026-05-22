#ifndef XRDP_OHOS_CURSOR_H
#define XRDP_OHOS_CURSOR_H

#include "arch.h"

#include <stdint.h>

struct ohos_mod;

struct ohos_cursor_state
{
    int sent_kind;
    int sent_system_pointer_valid;
    int sent_system_pointer;
    int sent_image_valid;
    int sent_image_width;
    int sent_image_height;
    int sent_image_hot_x;
    int sent_image_hot_y;
    uint64_t sent_image_hash;
    int style_valid;
    int style;
    int visible_valid;
    int visible;
    int has_position;
    int last_x;
    int last_y;
    uint64_t last_query_us;
    uint64_t query_count;
    uint64_t update_count;
    uint64_t image_update_count;
    uint64_t defaulted_count;
    uint64_t error_count;
};

void
ohos_cursor_init(struct ohos_cursor_state *cursor);

void
ohos_cursor_start_session(struct ohos_mod *self);

void
ohos_cursor_end_session(struct ohos_mod *self, const char *reason);

void
ohos_cursor_handle_pointer_event(struct ohos_mod *self, int msg,
                                 tbus x, tbus y);

int
ohos_cursor_check_wait_objs(struct ohos_mod *self);

void
ohos_cursor_log_summary(struct ohos_mod *self, const char *reason);

#endif /* XRDP_OHOS_CURSOR_H */
