/**
 * Shared HarmonyOS backend module state.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "thread_calls.h"

#include "ohos_private.h"

tbus g_ohos_frame_mutex = 0;
struct ohos_mod *g_ohos_active_mod = 0;
int g_ohos_frame_sequence = 0;

int
ohos_init_frame_state(void)
{
    if (g_ohos_frame_mutex == 0)
    {
        g_ohos_frame_mutex = tc_mutex_create();
    }
    return g_ohos_frame_mutex != 0;
}

int
ohos_lock_frame_state(void)
{
    if (!ohos_init_frame_state())
    {
        return 1;
    }
    return tc_mutex_lock(g_ohos_frame_mutex);
}

int
ohos_unlock_frame_state(void)
{
    if (g_ohos_frame_mutex == 0)
    {
        return 1;
    }
    return tc_mutex_unlock(g_ohos_frame_mutex);
}
