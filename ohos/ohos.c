/**
 * Minimal HarmonyOS backend for Phase 1 xrdp bring-up.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "arch.h"
#include "os_calls.h"
#include "log.h"

#include "ohos_private.h"

tintptr EXPORT_CC
mod_init(void)
{
    struct ohos_mod *self;

    self = (struct ohos_mod *)g_malloc(sizeof(struct ohos_mod), 1);
    ohos_init_frame_state();
    self->frame_wait_obj = g_create_wait_obj("xrdp_ohos_frame");
    ohos_cursor_init(&self->cursor);
    ohos_input_init(&self->input);
    ohos_cliprdr_init(&self->cliprdr, &self->mod, self->frame_wait_obj);
    ohos_rdpsnd_init(&self->rdpsnd, &self->mod, self->frame_wait_obj);
    self->mod.size = sizeof(struct mod);
    self->mod.version = XRDP_OHOS_MOD_VERSION;
    self->mod.handle = (tintptr)self;
    ohos_bind_mod_callbacks(self);

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: init");
    return (tintptr)self;
}

int EXPORT_CC
mod_exit(tintptr handle)
{
    struct ohos_mod *self = (struct ohos_mod *)handle;

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: exit");
    if (self != 0)
    {
        if (self->connected)
        {
            ohos_log_session_summary(self, "module_exit");
            ohos_cursor_end_session(self, "module_exit");
        }
        if (ohos_lock_frame_state() == 0)
        {
            if (g_ohos_active_mod == self)
            {
                g_ohos_active_mod = 0;
            }
            ohos_unlock_frame_state();
        }
        ohos_discard_pending_frame(self);
        ohos_input_deinit(&self->input);
        ohos_cliprdr_deinit(&self->cliprdr);
        ohos_rdpsnd_deinit(&self->rdpsnd);
        if (self->frame_wait_obj != 0)
        {
            g_delete_wait_obj(self->frame_wait_obj);
        }
        g_free(self);
    }
    return 0;
}
