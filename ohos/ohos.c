/**
 * Minimal HarmonyOS backend for Phase 1 xrdp bring-up.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "arch.h"
#include "os_calls.h"
#include "string_calls.h"
#include "thread_calls.h"
#include "log.h"
#include "xrdp_constants.h"
#include "xup.h"

#define OHOS_MOD_VER 4
#define OHOS_EVENT_SESSION_CONNECT 0
#define OHOS_EVENT_SESSION_DISCONNECT -1
#define OHOS_MOUSE_LOG_SAMPLE 64
#define OHOS_FRAME_MAX_DIMENSION 8192
#define OHOS_INPUT_EVENT_VERSION 1

struct xrdp_ohos_input_event
{
    int version;
    int msg;
    long param1;
    long param2;
    long param3;
    long param4;
    int width;
    int height;
    int bpp;
    int connected;
};

typedef void (*xrdp_ohos_input_event_fn)(const struct xrdp_ohos_input_event *event,
                                         void *user_data);

struct ohos_mod
{
    struct mod mod;
    int width;
    int height;
    int bpp;
    int connected;
    int mouse_move_count;
    int frame_draw_count;
    int frame_sequence;
    int frame_width;
    int frame_height;
    int frame_pending;
    char *frame_data;
    tintptr frame_wait_obj;
    char client_name[256];
};

static tbus g_ohos_frame_mutex = 0;
static struct ohos_mod *g_ohos_active_mod = 0;
static int g_ohos_frame_sequence = 0;
static tbus g_ohos_input_mutex = 0;
static xrdp_ohos_input_event_fn g_ohos_input_callback = 0;
static void *g_ohos_input_callback_user = 0;

int EXPORT_CC
xrdp_ohos_backend_submit_bgra_frame(const void *data, int width, int height,
                                    int stride);

int EXPORT_CC
xrdp_ohos_backend_set_input_callback(xrdp_ohos_input_event_fn callback,
                                     void *user_data);

static int
ohos_ensure_frame_mutex(void)
{
    if (g_ohos_frame_mutex == 0)
    {
        g_ohos_frame_mutex = tc_mutex_create();
    }
    return g_ohos_frame_mutex != 0;
}

static int
ohos_lock_frame_state(void)
{
    if (!ohos_ensure_frame_mutex())
    {
        return 1;
    }
    return tc_mutex_lock(g_ohos_frame_mutex);
}

static int
ohos_unlock_frame_state(void)
{
    if (g_ohos_frame_mutex == 0)
    {
        return 1;
    }
    return tc_mutex_unlock(g_ohos_frame_mutex);
}

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

static struct ohos_mod *
ohos_from_mod(struct mod *mod)
{
    return (struct ohos_mod *)mod;
}

static void
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

    event.version = OHOS_INPUT_EVENT_VERSION;
    event.msg = msg;
    event.param1 = (long)param1;
    event.param2 = (long)param2;
    event.param3 = (long)param3;
    event.param4 = (long)param4;
    event.width = self->width;
    event.height = self->height;
    event.bpp = self->bpp;
    event.connected = self->connected;
    callback(&event, user_data);
}

static int
ohos_min(int a, int b)
{
    return (a < b) ? a : b;
}

static int
ohos_fill_rect(struct mod *mod, int color, int x, int y, int cx, int cy)
{
    if (mod->server_set_fgcolor == 0 || mod->server_fill_rect == 0)
    {
        return 1;
    }

    mod->server_set_fgcolor(mod, color);
    return mod->server_fill_rect(mod, x, y, cx, cy);
}

static void
ohos_discard_pending_frame(struct ohos_mod *self)
{
    char *data = 0;

    if (self == 0 || ohos_lock_frame_state() != 0)
    {
        return;
    }

    data = self->frame_data;
    self->frame_data = 0;
    self->frame_pending = 0;
    self->frame_width = 0;
    self->frame_height = 0;
    ohos_unlock_frame_state();

    if (data != 0)
    {
        g_free(data);
    }
}

static int
ohos_draw_external_frame(struct ohos_mod *self, int *painted)
{
    struct mod *mod;
    char *data = 0;
    int frame_width = 0;
    int frame_height = 0;
    int sequence = 0;
    int paint_width;
    int paint_height;
    int rv = 0;

    if (self == 0)
    {
        return 0;
    }
    if (painted != 0)
    {
        *painted = 0;
    }

    if (ohos_lock_frame_state() != 0)
    {
        return 1;
    }

    if (self->frame_pending && self->frame_data != 0)
    {
        data = self->frame_data;
        frame_width = self->frame_width;
        frame_height = self->frame_height;
        sequence = self->frame_sequence;
        self->frame_data = 0;
        self->frame_pending = 0;
    }

    ohos_unlock_frame_state();

    if (data == 0)
    {
        return 0;
    }
    if (painted != 0)
    {
        *painted = 1;
    }

    mod = &self->mod;
    paint_width = ohos_min(self->width, frame_width);
    paint_height = ohos_min(self->height, frame_height);
    if (paint_width <= 0 || paint_height <= 0 ||
            mod->server_begin_update == 0 || mod->server_end_update == 0)
    {
        g_free(data);
        return 0;
    }

    rv |= mod->server_begin_update(mod);
    if (mod->server_paint_rect_bpp != 0)
    {
        rv |= mod->server_paint_rect_bpp(mod, 0, 0, paint_width, paint_height,
                                         data, frame_width, frame_height, 0, 0, 32);
    }
    else if (mod->server_paint_rect != 0)
    {
        rv |= mod->server_paint_rect(mod, 0, 0, paint_width, paint_height,
                                     data, frame_width, frame_height, 0, 0);
    }
    else
    {
        rv = 1;
    }
    rv |= mod->server_end_update(mod);

    self->frame_draw_count++;
    if (self->frame_draw_count <= 3 || (self->frame_draw_count % 30) == 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.frame: painted external BGRA frame seq=%d size=%dx%d dst=%dx%d rv=%d",
            sequence, frame_width, frame_height, paint_width, paint_height, rv);
    }

    g_free(data);
    return rv;
}

static int
ohos_clear_frame(struct ohos_mod *self, const char *reason)
{
    struct mod *mod = &self->mod;
    int rv = 0;

    if (self->width <= 0 || self->height <= 0 ||
            mod->server_begin_update == 0 || mod->server_end_update == 0)
    {
        return 0;
    }

    rv |= mod->server_begin_update(mod);
    rv |= ohos_fill_rect(mod, 0x000000, 0, 0, self->width, self->height);
    rv |= mod->server_end_update(mod);

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.frame: cleared frame %dx%d bpp=%d rv=%d reason=%s",
        self->width, self->height, self->bpp, rv,
        reason == 0 ? "" : reason);
    return rv;
}

static int
ohos_mod_start(struct mod *mod, int width, int height, int bpp)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    self->width = width;
    self->height = height;
    self->bpp = bpp;

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: start width=%d height=%d bpp=%d",
        width, height, bpp);
    return ohos_clear_frame(self, "start waiting for external frame");
}

static int
ohos_mod_connect(struct mod *mod, int fd)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    int painted = 0;
    int rv;
    self->connected = 1;

    if (ohos_lock_frame_state() == 0)
    {
        g_ohos_active_mod = self;
        ohos_unlock_frame_state();
    }

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: connect fd=%d client=%s",
        fd, self->client_name);
    ohos_forward_input_event(self, OHOS_EVENT_SESSION_CONNECT, 0, 0, 0, 0);
    rv = ohos_draw_external_frame(self, &painted);
    if (painted)
    {
        return rv;
    }
    return ohos_clear_frame(self, "connect waiting for external frame");
}

static int
ohos_mod_event(struct mod *mod, int msg, tbus param1, tbus param2,
               tbus param3, tbus param4)
{
    struct ohos_mod *self = ohos_from_mod(mod);

    switch (msg)
    {
        case WM_KEYDOWN:
        case WM_KEYUP:
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.input: key %s flags=%ld code=%ld extra=(%ld,%ld)",
                msg == WM_KEYDOWN ? "down" : "up",
                param1, param2, param3, param4);
            break;

        case WM_KEYBRD_SYNC:
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.input: key_sync device_flags=%ld key_flags=%ld",
                param1, param2);
            break;

        case WM_MOUSEMOVE:
            self->mouse_move_count++;
            if ((self->mouse_move_count % OHOS_MOUSE_LOG_SAMPLE) == 0)
            {
                LOG(LOG_LEVEL_INFO,
                    "xrdp.ohos.input: mouse_move x=%ld y=%ld count=%d",
                    param1, param2, self->mouse_move_count);
            }
            break;

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_BUTTON3DOWN:
        case WM_BUTTON3UP:
        case WM_BUTTON4DOWN:
        case WM_BUTTON4UP:
        case WM_BUTTON5DOWN:
        case WM_BUTTON5UP:
        case WM_BUTTON6DOWN:
        case WM_BUTTON6UP:
        case WM_BUTTON7DOWN:
        case WM_BUTTON7UP:
        case WM_BUTTON8DOWN:
        case WM_BUTTON8UP:
        case WM_BUTTON9DOWN:
        case WM_BUTTON9UP:
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.input: mouse_button msg=%d x=%ld y=%ld",
                msg, param1, param2);
            break;

        case WM_CHANNEL_DATA:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.channel: ignored channel data id_flags=%ld bytes=%ld total=%ld",
                param1, param2, param4);
            break;

        default:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: ignored event msg=%d p=(%ld,%ld,%ld,%ld)",
                msg, param1, param2, param3, param4);
            break;
    }

    ohos_forward_input_event(self, msg, param1, param2, param3, param4);
    return 0;
}

static int
ohos_mod_signal(struct mod *mod)
{
    (void)mod;
    return 0;
}

static int
ohos_mod_end(struct mod *mod)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    self->connected = 0;
    if (ohos_lock_frame_state() == 0)
    {
        if (g_ohos_active_mod == self)
        {
            g_ohos_active_mod = 0;
        }
        ohos_unlock_frame_state();
    }
    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: end");
    ohos_forward_input_event(self, OHOS_EVENT_SESSION_DISCONNECT, 0, 0, 0, 0);
    return 0;
}

static int
ohos_mod_set_param(struct mod *mod, const char *name, const char *value)
{
    struct ohos_mod *self = ohos_from_mod(mod);

    if (name == 0 || value == 0)
    {
        return 0;
    }

    if (g_strncmp(name, "client_name", 255) == 0)
    {
        g_strncpy(self->client_name, value, sizeof(self->client_name));
    }

    LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.module: param %s=%s", name, value);
    return 0;
}

static int
ohos_mod_get_wait_objs(struct mod *mod, tbus *read_objs, int *rcount,
                       tbus *write_objs, int *wcount, int *timeout)
{
    struct ohos_mod *self = ohos_from_mod(mod);

    (void)write_objs;
    (void)wcount;

    if (read_objs != 0 && rcount != 0 && self->frame_wait_obj != 0)
    {
        read_objs[*rcount] = self->frame_wait_obj;
        (*rcount)++;
    }
    if (timeout != 0 && *timeout < 0)
    {
        *timeout = 1000;
    }
    return 0;
}

static int
ohos_mod_check_wait_objs(struct mod *mod)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    if (self->frame_wait_obj != 0 && g_is_wait_obj_set(self->frame_wait_obj))
    {
        g_reset_wait_obj(self->frame_wait_obj);
        return ohos_draw_external_frame(self, 0);
    }
    return 0;
}

static int
ohos_mod_frame_ack(struct mod *mod, int flags, int frame_id)
{
    (void)mod;
    LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.frame: ack flags=0x%8.8x frame_id=%d",
        flags, frame_id);
    return 0;
}

static int
ohos_mod_suppress_output(struct mod *mod, int suppress,
                         int left, int top, int right, int bottom)
{
    (void)mod;
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.frame: suppress=%d rect=(%d,%d,%d,%d)",
        suppress, left, top, right, bottom);
    return 0;
}

static int
ohos_mod_server_monitor_resize(struct mod *mod,
                               int width, int height,
                               int num_monitors,
                               const struct monitor_info *monitors,
                               int *in_progress)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    (void)num_monitors;
    (void)monitors;

    self->width = width;
    self->height = height;
    if (in_progress != 0)
    {
        *in_progress = 0;
    }

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.resize: client resize %dx%d",
        width, height);
    return ohos_clear_frame(self, "resize waiting for external frame");
}

static int
ohos_mod_server_monitor_full_invalidate(struct mod *mod,
                                        int width, int height)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    self->width = width;
    self->height = height;

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.resize: full invalidate %dx%d",
        width, height);
    return ohos_clear_frame(self, "full invalidate waiting for external frame");
}

static int
ohos_mod_server_version_message(struct mod *mod)
{
    (void)mod;
    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: server version message");
    return 0;
}

tintptr EXPORT_CC
mod_init(void)
{
    struct ohos_mod *self;

    self = (struct ohos_mod *)g_malloc(sizeof(struct ohos_mod), 1);
    ohos_ensure_frame_mutex();
    self->frame_wait_obj = g_create_wait_obj("xrdp_ohos_frame");
    self->mod.size = sizeof(struct mod);
    self->mod.version = OHOS_MOD_VER;
    self->mod.handle = (tintptr)self;
    self->mod.mod_start = ohos_mod_start;
    self->mod.mod_connect = ohos_mod_connect;
    self->mod.mod_event = ohos_mod_event;
    self->mod.mod_signal = ohos_mod_signal;
    self->mod.mod_end = ohos_mod_end;
    self->mod.mod_set_param = ohos_mod_set_param;
    self->mod.mod_get_wait_objs = ohos_mod_get_wait_objs;
    self->mod.mod_check_wait_objs = ohos_mod_check_wait_objs;
    self->mod.mod_frame_ack = ohos_mod_frame_ack;
    self->mod.mod_suppress_output = ohos_mod_suppress_output;
    self->mod.mod_server_monitor_resize = ohos_mod_server_monitor_resize;
    self->mod.mod_server_monitor_full_invalidate =
        ohos_mod_server_monitor_full_invalidate;
    self->mod.mod_server_version_message = ohos_mod_server_version_message;

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
        if (ohos_lock_frame_state() == 0)
        {
            if (g_ohos_active_mod == self)
            {
                g_ohos_active_mod = 0;
            }
            ohos_unlock_frame_state();
        }
        ohos_discard_pending_frame(self);
        if (self->frame_wait_obj != 0)
        {
            g_delete_wait_obj(self->frame_wait_obj);
        }
        g_free(self);
    }
    return 0;
}

int EXPORT_CC
xrdp_ohos_backend_submit_bgra_frame(const void *data, int width, int height,
                                    int stride)
{
    struct ohos_mod *target;
    char *packed;
    char *old_data = 0;
    tintptr wait_obj = 0;
    int row;
    int row_bytes;
    size_t packed_bytes;

    if (data == 0 || width <= 0 || height <= 0 ||
            width > OHOS_FRAME_MAX_DIMENSION ||
            height > OHOS_FRAME_MAX_DIMENSION)
    {
        return -1;
    }

    row_bytes = width * 4;
    packed_bytes = (size_t)row_bytes * (size_t)height;
    if (stride < row_bytes ||
            packed_bytes / (size_t)height != (size_t)row_bytes)
    {
        return -1;
    }

    packed = (char *)g_malloc(packed_bytes, 0);
    if (packed == 0)
    {
        return -2;
    }

    for (row = 0; row < height; row++)
    {
        g_memcpy(packed + ((size_t)row * (size_t)row_bytes),
                 ((const char *)data) + ((size_t)row * (size_t)stride),
                 row_bytes);
    }

    if (ohos_lock_frame_state() != 0)
    {
        g_free(packed);
        return -3;
    }

    target = g_ohos_active_mod;
    if (target == 0 || !target->connected)
    {
        ohos_unlock_frame_state();
        g_free(packed);
        return -4;
    }

    old_data = target->frame_data;
    target->frame_data = packed;
    target->frame_width = width;
    target->frame_height = height;
    target->frame_sequence = ++g_ohos_frame_sequence;
    target->frame_pending = 1;
    wait_obj = target->frame_wait_obj;
    ohos_unlock_frame_state();

    if (old_data != 0)
    {
        g_free(old_data);
    }
    if (wait_obj != 0)
    {
        g_set_wait_obj(wait_obj);
    }

    return 0;
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
