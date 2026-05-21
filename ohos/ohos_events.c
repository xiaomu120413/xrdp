#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_private.h"

#include "log.h"
#include "thread_calls.h"

static tbus g_ohos_input_mutex = 0;
static xrdp_ohos_input_event_fn g_ohos_input_callback = 0;
static void *g_ohos_input_callback_user = 0;
static xrdp_ohos_backend_event_fn g_ohos_event_callback = 0;
static void *g_ohos_event_callback_user = 0;

static int
ohos_ensure_input_mutex(void)
{
    if (g_ohos_input_mutex == 0)
    {
        g_ohos_input_mutex = tc_mutex_create();
    }
    return g_ohos_input_mutex != 0;
}

static int
ohos_lock_input_state(void)
{
    if (!ohos_ensure_input_mutex())
    {
        return 1;
    }
    return tc_mutex_lock(g_ohos_input_mutex);
}

static int
ohos_unlock_input_state(void)
{
    if (g_ohos_input_mutex == 0)
    {
        return 1;
    }
    return tc_mutex_unlock(g_ohos_input_mutex);
}

void
ohos_fill_input_event(struct ohos_mod *self, int msg, tbus param1,
                      tbus param2, tbus param3, tbus param4,
                      struct xrdp_ohos_input_event *event)
{
    if (event == 0)
    {
        return;
    }

    event->version = XRDP_OHOS_INPUT_EVENT_VERSION;
    event->msg = msg;
    event->param1 = (long)param1;
    event->param2 = (long)param2;
    event->param3 = (long)param3;
    event->param4 = (long)param4;
    if (self != 0)
    {
        event->width = self->width;
        event->height = self->height;
        event->bpp = self->bpp;
        event->connected = self->connected;
    }
    else
    {
        event->width = 0;
        event->height = 0;
        event->bpp = 0;
        event->connected = 0;
    }
}

void
ohos_forward_input_event(struct ohos_mod *self, int msg, tbus param1,
                         tbus param2, tbus param3, tbus param4)
{
    xrdp_ohos_input_event_fn callback;
    void *user_data;
    struct xrdp_ohos_input_event event;

    if (self == 0 || ohos_lock_input_state() != 0)
    {
        return;
    }

    callback = g_ohos_input_callback;
    user_data = g_ohos_input_callback_user;
    ohos_unlock_input_state();

    if (callback == 0)
    {
        return;
    }

    self->input_forwarded_count++;
    ohos_fill_input_event(self, msg, param1, param2, param3, param4, &event);
    callback(&event, user_data);
}

void
ohos_forward_backend_event(struct ohos_mod *self, int type, int suppress,
                           int left, int top, int right, int bottom,
                           int frame_id, int flags)
{
    xrdp_ohos_backend_event_fn callback;
    void *user_data;
    struct xrdp_ohos_backend_event event;
    struct ohos_frame_trace trace;
    int has_trace = 0;

    if (self == 0 || ohos_lock_input_state() != 0)
    {
        return;
    }

    callback = g_ohos_event_callback;
    user_data = g_ohos_event_callback_user;
    ohos_unlock_input_state();

    if (callback == 0)
    {
        return;
    }

    event.version = XRDP_OHOS_BACKEND_EVENT_VERSION;
    event.type = type;
    event.width = self->width;
    event.height = self->height;
    event.bpp = self->bpp;
    event.connected = self->connected;
    event.suppress = suppress;
    event.left = left;
    event.top = top;
    event.right = right;
    event.bottom = bottom;
    event.frame_id = frame_id;
    event.flags = flags;
    event.source_sequence = 0;
    event.capture_acquire_us = 0;
    event.ack_us = 0;
    if (type == XRDP_OHOS_BACKEND_EVENT_FRAME_ACK)
    {
        has_trace = ohos_lookup_frame_trace(self, frame_id, &trace);
        if (has_trace)
        {
            event.source_sequence = trace.source_sequence;
            event.capture_acquire_us = trace.capture_acquire_us;
        }
        event.ack_us = ohos_now_us();
    }
    callback(&event, user_data);
}

int EXPORT_CC
xrdp_ohos_backend_set_input_callback(xrdp_ohos_input_event_fn callback,
                                     void *user_data)
{
    if (ohos_lock_input_state() != 0)
    {
        return 1;
    }

    g_ohos_input_callback = callback;
    g_ohos_input_callback_user = user_data;
    ohos_unlock_input_state();

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.input: callback %s",
        callback == 0 ? "cleared" : "registered");
    return 0;
}

int EXPORT_CC
xrdp_ohos_backend_set_event_callback(xrdp_ohos_backend_event_fn callback,
                                     void *user_data)
{
    if (ohos_lock_input_state() != 0)
    {
        return 1;
    }

    g_ohos_event_callback = callback;
    g_ohos_event_callback_user = user_data;
    ohos_unlock_input_state();

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.event: callback %s",
        callback == 0 ? "cleared" : "registered");
    return 0;
}
