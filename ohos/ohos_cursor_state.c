/**
 * HarmonyOS cursor state and diagnostics.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "log.h"
#include "os_calls.h"

#include "ohos_private.h"

void
ohos_cursor_init(struct ohos_cursor_state *cursor)
{
    if (cursor == 0)
    {
        return;
    }

    g_memset(cursor, 0, sizeof(*cursor));
}

void
ohos_cursor_log_summary(struct ohos_mod *self, const char *reason)
{
    const struct ohos_cursor_state *cursor;

    if (self == 0)
    {
        return;
    }

    cursor = &self->cursor;
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.cursor: summary reason=%s queries=%llu updates=%llu image_updates=%llu defaulted=%llu errors=%llu visible_valid=%d visible=%d style_valid=%d style=%d sent_kind=%d sent_system_valid=%d sent_system=0x%4.4x sent_image_valid=%d sent_image=%dx%d hot=(%d,%d) has_pos=%d pos=(%d,%d)",
        reason == 0 ? "" : reason,
        (unsigned long long)cursor->query_count,
        (unsigned long long)cursor->update_count,
        (unsigned long long)cursor->image_update_count,
        (unsigned long long)cursor->defaulted_count,
        (unsigned long long)cursor->error_count,
        cursor->visible_valid, cursor->visible,
        cursor->style_valid, cursor->style,
        cursor->sent_kind,
        cursor->sent_system_pointer_valid, cursor->sent_system_pointer,
        cursor->sent_image_valid, cursor->sent_image_width,
        cursor->sent_image_height, cursor->sent_image_hot_x,
        cursor->sent_image_hot_y,
        cursor->has_position, cursor->last_x, cursor->last_y);
}
