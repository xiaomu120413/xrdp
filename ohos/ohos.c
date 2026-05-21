/**
 * Minimal HarmonyOS backend for Phase 1 xrdp bring-up.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "arch.h"
#include "os_calls.h"
#include "string_calls.h"
#include "log.h"
#include "xrdp_constants.h"
#include "xup.h"

#define OHOS_MOD_VER 4
#define OHOS_MOUSE_LOG_SAMPLE 64

struct ohos_mod
{
    struct mod mod;
    int width;
    int height;
    int bpp;
    int connected;
    int mouse_move_count;
    char client_name[256];
};

static struct ohos_mod *
ohos_from_mod(struct mod *mod)
{
    return (struct ohos_mod *)mod;
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

static int
ohos_draw_test_frame(struct ohos_mod *self)
{
    struct mod *mod = &self->mod;
    int tile;
    int x;
    int y;
    int i;
    int rv = 0;
    const int colors[] =
    {
        0x1f2933,
        0x0ea5e9,
        0x22c55e,
        0xf59e0b,
        0xef4444,
        0xf8fafc,
        0x111827,
        0x6366f1
    };

    if (self->width <= 0 || self->height <= 0 ||
            mod->server_begin_update == 0 || mod->server_end_update == 0)
    {
        return 0;
    }

    tile = ohos_min(self->width, self->height) / 8;
    if (tile < 32)
    {
        tile = 32;
    }

    rv |= mod->server_begin_update(mod);
    rv |= ohos_fill_rect(mod, 0x0b1220, 0, 0, self->width, self->height);

    for (y = 0; y < self->height; y += tile)
    {
        for (x = 0; x < self->width; x += tile)
        {
            i = ((x / tile) + (y / tile)) % (int)(sizeof(colors) / sizeof(colors[0]));
            rv |= ohos_fill_rect(mod, colors[i], x, y,
                                 ohos_min(tile - 2, self->width - x),
                                 ohos_min(tile - 2, self->height - y));
        }
    }

    rv |= ohos_fill_rect(mod, 0x000000, 0, 0, self->width, 4);
    rv |= ohos_fill_rect(mod, 0x000000, 0, self->height - 4, self->width, 4);
    rv |= ohos_fill_rect(mod, 0x000000, 0, 0, 4, self->height);
    rv |= ohos_fill_rect(mod, 0x000000, self->width - 4, 0, 4, self->height);
    rv |= mod->server_end_update(mod);

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.frame: sent dummy frame %dx%d bpp=%d rv=%d",
        self->width, self->height, self->bpp, rv);
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
    return ohos_draw_test_frame(self);
}

static int
ohos_mod_connect(struct mod *mod, int fd)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    self->connected = 1;

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: connect fd=%d client=%s",
        fd, self->client_name);
    return ohos_draw_test_frame(self);
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
    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: end");
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
    (void)mod;
    (void)read_objs;
    (void)write_objs;
    if (rcount != 0)
    {
        *rcount = 0;
    }
    if (wcount != 0)
    {
        *wcount = 0;
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
    (void)mod;
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
    return ohos_draw_test_frame(self);
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
    return ohos_draw_test_frame(self);
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
        g_free(self);
    }
    return 0;
}
