/**
 * HarmonyOS cursor bridge.
 *
 * Screen capture keeps the platform cursor out of the video stream. This
 * module mirrors the current OHOS cursor state to the RDP client using pointer
 * updates. Unsupported OHOS styles intentionally degrade to the default RDP
 * system pointer rather than enabling capture-side cursor rendering.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "arch.h"
#include "log.h"
#include "ms-rdpbcgr.h"
#include "os_calls.h"
#include "xrdp_constants.h"

#include <stdbool.h>
#include <multimodalinput/oh_input_manager.h>

#include "ohos_cursor_image.h"
#include "ohos_cursor_system.h"
#include "ohos_private.h"

#define OHOS_CURSOR_QUERY_INTERVAL_US 100000ULL
#define OHOS_CURSOR_STYLE_DEFAULT 0

enum ohos_cursor_sent_kind
{
    OHOS_CURSOR_SENT_NONE = 0,
    OHOS_CURSOR_SENT_SYSTEM = 1,
    OHOS_CURSOR_SENT_IMAGE = 2
};

struct ohos_cursor_snapshot
{
    int visible;
    int style;
    OH_PixelmapNative *pixelmap;
};

static int
ohos_cursor_is_pointer_event(int msg)
{
    return msg >= WM_MOUSEMOVE && msg <= WM_BUTTON9DOWN;
}

static const char *
ohos_cursor_system_pointer_name(int pointer_type)
{
    switch (pointer_type)
    {
        case SYSPTR_NULL:
            return "null";
        case SYSPTR_DEFAULT:
            return "default";
        default:
            return "unknown";
    }
}

static void
ohos_cursor_snapshot_deinit(struct ohos_cursor_snapshot *snapshot)
{
    if (snapshot == 0)
    {
        return;
    }

    if (snapshot->pixelmap != 0)
    {
        OH_PixelmapNative_Release(snapshot->pixelmap);
        snapshot->pixelmap = 0;
    }
}

static int
ohos_cursor_query(struct ohos_cursor_snapshot *snapshot)
{
    Input_CursorInfo *cursor_info;
    Input_PointerStyle pointer_style;
    bool cursor_visible = true;
    int rc;

    if (snapshot == 0)
    {
        return INPUT_PARAMETER_ERROR;
    }

    snapshot->visible = 1;
    snapshot->style = OHOS_CURSOR_STYLE_DEFAULT;
    snapshot->pixelmap = 0;

    cursor_info = OH_Input_CursorInfo_Create();
    if (cursor_info == 0)
    {
        return INPUT_PARAMETER_ERROR;
    }

    rc = OH_Input_GetCursorInfo(cursor_info, &snapshot->pixelmap);
    if (rc == INPUT_SUCCESS)
    {
        rc = OH_Input_CursorInfo_IsVisible(cursor_info, &cursor_visible);
    }
    if (rc == INPUT_SUCCESS)
    {
        snapshot->visible = cursor_visible ? 1 : 0;
        if (cursor_visible)
        {
            pointer_style = (Input_PointerStyle)OHOS_CURSOR_STYLE_DEFAULT;
            rc = OH_Input_CursorInfo_GetStyle(cursor_info, &pointer_style);
            if (rc == INPUT_SUCCESS)
            {
                snapshot->style = (int)pointer_style;
            }
        }
    }

    OH_Input_CursorInfo_Destroy(&cursor_info);
    if (rc != INPUT_SUCCESS)
    {
        ohos_cursor_snapshot_deinit(snapshot);
    }
    return rc;
}

static int
ohos_cursor_rdp_pointer_type(int visible, int style, int *defaulted)
{
    if (defaulted != 0)
    {
        *defaulted = 0;
    }

    if (!visible)
    {
        return SYSPTR_NULL;
    }

    if (ohos_cursor_system_style_is_null(style))
    {
        return SYSPTR_NULL;
    }

    if (style == OHOS_CURSOR_STYLE_DEFAULT)
    {
        return SYSPTR_DEFAULT;
    }

    if (defaulted != 0)
    {
        *defaulted = 1;
    }
    return SYSPTR_DEFAULT;
}

static int
ohos_cursor_send_system_pointer(struct ohos_mod *self, int pointer_type,
                                const char *reason)
{
    struct ohos_cursor_state *cursor;
    int rv;

    if (self == 0)
    {
        return 1;
    }

    cursor = &self->cursor;
    if (cursor->sent_kind == OHOS_CURSOR_SENT_SYSTEM &&
        cursor->sent_system_pointer_valid &&
        cursor->sent_system_pointer == pointer_type)
    {
        return 0;
    }

    if (self->mod.server_set_pointer_system == 0)
    {
        cursor->error_count++;
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cursor: cannot send pointer=%s reason=%s server_callback=missing errors=%llu",
            ohos_cursor_system_pointer_name(pointer_type),
            reason == 0 ? "" : reason,
            (unsigned long long)cursor->error_count);
        return 1;
    }

    rv = self->mod.server_set_pointer_system(&self->mod, pointer_type);
    if (rv == 0)
    {
        cursor->sent_system_pointer_valid = 1;
        cursor->sent_system_pointer = pointer_type;
        cursor->sent_kind = OHOS_CURSOR_SENT_SYSTEM;
        cursor->sent_image_valid = 0;
        cursor->update_count++;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cursor: sent pointer=%s type=0x%4.4x reason=%s updates=%llu",
            ohos_cursor_system_pointer_name(pointer_type), pointer_type,
            reason == 0 ? "" : reason,
            (unsigned long long)cursor->update_count);
    }
    else
    {
        cursor->error_count++;
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cursor: send failed pointer=%s type=0x%4.4x reason=%s rv=%d errors=%llu",
            ohos_cursor_system_pointer_name(pointer_type), pointer_type,
            reason == 0 ? "" : reason, rv,
            (unsigned long long)cursor->error_count);
    }
    return rv;
}

static int
ohos_cursor_send_image_pointer(struct ohos_mod *self,
                               const struct ohos_cursor_image *image,
                               int style, const char *reason)
{
    struct ohos_cursor_state *cursor;
    int rv;

    if (self == 0 || image == 0 || image->data == 0 || image->mask == 0)
    {
        return 1;
    }

    cursor = &self->cursor;
    if (cursor->sent_kind == OHOS_CURSOR_SENT_IMAGE &&
        cursor->sent_image_valid &&
        cursor->sent_image_hash == image->hash &&
        cursor->sent_image_width == image->width &&
        cursor->sent_image_height == image->height &&
        cursor->sent_image_hot_x == image->hot_x &&
        cursor->sent_image_hot_y == image->hot_y)
    {
        return 0;
    }

    if (self->mod.server_set_pointer_large != 0)
    {
        rv = self->mod.server_set_pointer_large(&self->mod, image->hot_x,
                                                image->hot_y, image->data,
                                                image->mask, image->bpp,
                                                image->width, image->height);
    }
    else
    {
        cursor->error_count++;
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cursor: cannot send image pointer reason=%s size=%dx%d server_callback=missing errors=%llu",
            reason == 0 ? "" : reason, image->width, image->height,
            (unsigned long long)cursor->error_count);
        return 1;
    }

    if (rv == 0)
    {
        cursor->sent_kind = OHOS_CURSOR_SENT_IMAGE;
        cursor->sent_system_pointer_valid = 0;
        cursor->sent_image_valid = 1;
        cursor->sent_image_width = image->width;
        cursor->sent_image_height = image->height;
        cursor->sent_image_hot_x = image->hot_x;
        cursor->sent_image_hot_y = image->hot_y;
        cursor->sent_image_hash = image->hash;
        cursor->update_count++;
        cursor->image_update_count++;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cursor: sent image pointer style=%d size=%dx%d hot=(%d,%d) reason=%s updates=%llu image_updates=%llu",
            style, image->width, image->height, image->hot_x, image->hot_y,
            reason == 0 ? "" : reason,
            (unsigned long long)cursor->update_count,
            (unsigned long long)cursor->image_update_count);
    }
    else
    {
        cursor->error_count++;
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.cursor: image pointer send failed style=%d size=%dx%d hot=(%d,%d) reason=%s rv=%d errors=%llu",
            style, image->width, image->height, image->hot_x, image->hot_y,
            reason == 0 ? "" : reason, rv,
            (unsigned long long)cursor->error_count);
    }
    return rv;
}

static int
ohos_cursor_refresh(struct ohos_mod *self, const char *reason, int force)
{
    struct ohos_cursor_state *cursor;
    struct ohos_cursor_snapshot snapshot;
    int defaulted;
    int pointer_type;
    int rc;
    uint64_t now_us;

    if (self == 0 || !self->connected)
    {
        return 0;
    }

    cursor = &self->cursor;
    now_us = ohos_now_us();
    if (!force && cursor->last_query_us != 0 &&
        now_us - cursor->last_query_us < OHOS_CURSOR_QUERY_INTERVAL_US)
    {
        return 0;
    }
    cursor->last_query_us = now_us;
    cursor->query_count++;

    g_memset(&snapshot, 0, sizeof(snapshot));
    rc = ohos_cursor_query(&snapshot);
    if (rc != INPUT_SUCCESS)
    {
        cursor->error_count++;
        cursor->defaulted_count++;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cursor: query failed rc=%d reason=%s default=SYSPTR_DEFAULT errors=%llu",
            rc, reason == 0 ? "" : reason,
            (unsigned long long)cursor->error_count);
        return ohos_cursor_send_system_pointer(self, SYSPTR_DEFAULT,
                                               "query_failed_default");
    }

    if (snapshot.visible && snapshot.pixelmap != 0)
    {
        struct ohos_cursor_image image;
        int image_rv;

        ohos_cursor_image_init(&image);
        image_rv = ohos_cursor_image_from_pixelmap(snapshot.pixelmap,
                                                   snapshot.style, &image);
        if (image_rv == 0)
        {
            cursor->visible_valid = 1;
            cursor->visible = snapshot.visible;
            cursor->style_valid = 1;
            cursor->style = snapshot.style;
            rc = ohos_cursor_send_image_pointer(self, &image, snapshot.style,
                                                reason);
            ohos_cursor_image_deinit(&image);
            ohos_cursor_snapshot_deinit(&snapshot);
            if (rc == 0)
            {
                return 0;
            }
        }
        else
        {
            cursor->defaulted_count++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cursor: pixelmap conversion failed style=%d reason=%s default=SYSPTR_DEFAULT defaulted=%llu",
                snapshot.style, reason == 0 ? "" : reason,
                (unsigned long long)cursor->defaulted_count);
        }
        ohos_cursor_image_deinit(&image);
    }

    if (snapshot.visible && ohos_cursor_system_style_is_null(snapshot.style))
    {
        cursor->visible_valid = 1;
        cursor->visible = snapshot.visible;
        cursor->style_valid = 1;
        cursor->style = snapshot.style;
        ohos_cursor_snapshot_deinit(&snapshot);
        return ohos_cursor_send_system_pointer(self, SYSPTR_NULL, reason);
    }

    if (snapshot.visible)
    {
        const struct ohos_cursor_image *system_image;

        if (ohos_cursor_system_get_image(snapshot.style, &system_image) == 0)
        {
            cursor->visible_valid = 1;
            cursor->visible = snapshot.visible;
            cursor->style_valid = 1;
            cursor->style = snapshot.style;
            rc = ohos_cursor_send_image_pointer(self, system_image,
                                                snapshot.style, reason);
            ohos_cursor_snapshot_deinit(&snapshot);
            if (rc == 0)
            {
                return 0;
            }
        }
        else
        {
            cursor->defaulted_count++;
            if (!cursor->style_valid || cursor->style != snapshot.style ||
                (cursor->defaulted_count <= 5ULL ||
                 (cursor->defaulted_count % 100ULL) == 0ULL))
            {
                LOG(LOG_LEVEL_DEBUG,
                    "xrdp.ohos.cursor: system icon unavailable style=%d reason=%s default=SYSPTR_DEFAULT defaulted=%llu",
                    snapshot.style, reason == 0 ? "" : reason,
                    (unsigned long long)cursor->defaulted_count);
            }
        }
    }

    pointer_type = ohos_cursor_rdp_pointer_type(snapshot.visible,
                                               snapshot.style, &defaulted);
    if (defaulted)
    {
        cursor->defaulted_count++;
        if (!cursor->style_valid || cursor->style != snapshot.style ||
            (cursor->defaulted_count <= 5ULL ||
             (cursor->defaulted_count % 100ULL) == 0ULL))
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.cursor: unsupported style=%d reason=%s default=SYSPTR_DEFAULT defaulted=%llu",
                snapshot.style, reason == 0 ? "" : reason,
                (unsigned long long)cursor->defaulted_count);
        }
    }

    cursor->visible_valid = 1;
    cursor->visible = snapshot.visible;
    cursor->style_valid = 1;
    cursor->style = snapshot.style;
    ohos_cursor_snapshot_deinit(&snapshot);
    return ohos_cursor_send_system_pointer(self, pointer_type, reason);
}

void
ohos_cursor_start_session(struct ohos_mod *self)
{
    if (self == 0)
    {
        return;
    }

    ohos_cursor_init(&self->cursor);
    (void)ohos_cursor_send_system_pointer(self, SYSPTR_DEFAULT,
                                          "session_connect");
    (void)ohos_cursor_refresh(self, "session_connect", 1);
}

void
ohos_cursor_end_session(struct ohos_mod *self, const char *reason)
{
    if (self == 0)
    {
        return;
    }

    ohos_cursor_log_summary(self, reason);
    ohos_cursor_init(&self->cursor);
}

void
ohos_cursor_handle_pointer_event(struct ohos_mod *self, int msg,
                                 tbus x, tbus y)
{
    struct ohos_cursor_state *cursor;

    if (self == 0 || !self->connected || !ohos_cursor_is_pointer_event(msg))
    {
        return;
    }

    cursor = &self->cursor;
    cursor->last_x = (int)x;
    cursor->last_y = (int)y;
    cursor->has_position = 1;
    (void)ohos_cursor_refresh(self,
                              msg == WM_MOUSEMOVE ? "mouse_move" :
                              "mouse_button", 0);
}

int
ohos_cursor_check_wait_objs(struct ohos_mod *self)
{
    (void)ohos_cursor_refresh(self, "poll", 0);
    return 0;
}
