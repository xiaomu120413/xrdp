/**
 * HarmonyOS xrdp module callback implementations.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "arch.h"
#include "log.h"
#include "os_calls.h"
#include "string_calls.h"
#include "xrdp_constants.h"

#include "ohos_private.h"

static struct ohos_mod *
ohos_from_mod(struct mod *mod)
{
    return (struct ohos_mod *)mod;
}

static int
ohos_param_is_sensitive(const char *name)
{
    return g_strcasecmp(name, "password") == 0 ||
           g_strcasecmp(name, "pampassword") == 0 ||
           g_strcasecmp(name, "access_code") == 0 ||
           g_strcasecmp(name, "client_info") == 0;
}

static const char *
ohos_param_log_value(const char *name, const char *value)
{
    if (ohos_param_is_sensitive(name))
    {
        return "<redacted>";
    }
    return value;
}

static int
ohos_access_authorized(struct ohos_mod *self)
{
    const char *expected_user;

    if (self->access_code[0] == '\0')
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.auth: denied client=%s reason=missing_access_code",
            self->client_name);
        return 0;
    }

    expected_user = self->access_username[0] != '\0' ?
                    self->access_username : "ohos";
    if (self->login_username[0] == '\0' || self->login_password[0] == '\0')
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.auth: denied client=%s reason=missing_credentials user=%s",
            self->client_name, self->login_username);
        return 0;
    }
    if (g_strcmp(self->login_username, expected_user) != 0 ||
            g_strcmp(self->login_password, self->access_code) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.auth: denied client=%s reason=invalid_credentials user=%s",
            self->client_name, self->login_username);
        return 0;
    }

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.auth: accepted client=%s user=%s",
        self->client_name, self->login_username);
    return 1;
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

    if (!ohos_access_authorized(self))
    {
        return 1;
    }

    ohos_reset_session_stats(self);
    ohos_input_start_session(&self->input);
    self->connected = 1;

    if (ohos_lock_frame_state() == 0)
    {
        g_ohos_active_mod = self;
        ohos_unlock_frame_state();
    }

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: connect fd=%d client=%s",
        fd, self->client_name);
    ohos_cursor_start_session(self);
    ohos_input_prime_authorization("session connect");
    (void)ohos_rdpsnd_connect(&self->rdpsnd);
    (void)ohos_cliprdr_connect(&self->cliprdr);
    ohos_forward_backend_event(self, XRDP_OHOS_BACKEND_EVENT_SESSION_CONNECT,
                               0, 0, 0, 0, 0, 0, 0);
    ohos_forward_input_event(self, XRDP_OHOS_INPUT_SESSION_CONNECT, 0, 0, 0, 0);
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
    struct xrdp_ohos_input_event input_event;
    int input_rc;

    switch (msg)
    {
        case WM_KEYDOWN:
        case WM_KEYUP:
            self->key_event_count++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: key %s flags=%ld code=%ld extra=(%ld,%ld)",
                msg == WM_KEYDOWN ? "down" : "up",
                param1, param2, param3, param4);
            break;

        case WM_KEYBRD_SYNC:
            self->key_sync_event_count++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: key_sync device_flags=%ld key_flags=%ld",
                param1, param2);
            break;

        case WM_MOUSEMOVE:
            self->mouse_move_count++;
            self->mouse_move_event_count++;
            if ((self->mouse_move_count % OHOS_MOUSE_LOG_SAMPLE) == 0)
            {
                LOG(LOG_LEVEL_DEBUG,
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
            self->mouse_button_event_count++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: mouse_button msg=%d x=%ld y=%ld",
                msg, param1, param2);
            break;

        case WM_CHANNEL_DATA:
        {
            int rv = 0;
            self->channel_data_event_count++;
            rv |= ohos_rdpsnd_process_channel_data(&self->rdpsnd,
                                                    param1, param2,
                                                    param3, param4);
            rv |= ohos_cliprdr_process_channel_data(&self->cliprdr,
                                                    param1, param2,
                                                    param3, param4);
            return rv;
        }

        default:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: ignored event msg=%d p=(%ld,%ld,%ld,%ld)",
                msg, param1, param2, param3, param4);
            break;
    }

    ohos_fill_input_event(self, msg, param1, param2, param3, param4,
                          &input_event);
    input_rc = ohos_input_handle_event(&self->input, &input_event);
    if (input_rc == 0)
    {
        ohos_cursor_handle_pointer_event(self, msg, param1, param2);
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
    ohos_forward_backend_event(self, XRDP_OHOS_BACKEND_EVENT_SESSION_DISCONNECT,
                               0, 0, 0, 0, 0, 0, 0);
    ohos_forward_input_event(self, XRDP_OHOS_INPUT_SESSION_DISCONNECT, 0, 0, 0, 0);
    ohos_log_session_summary(self, "client_disconnect");
    ohos_cursor_end_session(self, "client_disconnect");
    ohos_input_reset(&self->input, "session end");
    ohos_rdpsnd_disconnect(&self->rdpsnd);
    ohos_cliprdr_disconnect(&self->cliprdr);
    if (ohos_lock_frame_state() == 0)
    {
        if (g_ohos_active_mod == self)
        {
            g_ohos_active_mod = 0;
        }
        ohos_unlock_frame_state();
    }
    ohos_discard_pending_frame(self);
    LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.module: end");
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
    else if (g_strncmp(name, "username", 255) == 0)
    {
        g_strncpy(self->login_username, value, sizeof(self->login_username));
    }
    else if (g_strncmp(name, "password", 255) == 0)
    {
        g_strncpy(self->login_password, value, sizeof(self->login_password));
    }
    else if (g_strncmp(name, "access_username", 255) == 0)
    {
        g_strncpy(self->access_username, value, sizeof(self->access_username));
    }
    else if (g_strncmp(name, "access_code", 255) == 0)
    {
        g_strncpy(self->access_code, value, sizeof(self->access_code));
    }

    LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.module: param %s=%s", name,
        ohos_param_log_value(name, value));
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
    int rv = 0;
    if (self->frame_wait_obj != 0 && g_is_wait_obj_set(self->frame_wait_obj))
    {
        g_reset_wait_obj(self->frame_wait_obj);
        rv |= ohos_draw_external_frame(self, 0);
    }
    rv |= ohos_rdpsnd_check_wait_objs(&self->rdpsnd);
    rv |= ohos_cliprdr_check_wait_objs(&self->cliprdr);
    rv |= ohos_cursor_check_wait_objs(self);
    return rv;
}

static int
ohos_mod_frame_ack(struct mod *mod, int flags, int frame_id)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    struct ohos_frame_trace trace;
    uint64_t ack_us;
    int has_trace;

    LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.frame: ack flags=0x%8.8x frame_id=%d",
        flags, frame_id);
    ack_us = ohos_now_us();
    has_trace = ohos_lookup_frame_trace(self, frame_id, &trace);
    self->frame_ack_count++;
    if (has_trace && (self->frame_ack_count <= 5 ||
            (self->frame_ack_count % 60ULL) == 0))
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.e2e: ack frame=%d source_seq=%llu total_from_acquire=%.3fms bridge=%.3fms submitter_wait=%.3fms submitter_copy=%.3fms backend_wait=%.3fms backend_copy=%.3fms draw_wait=%.3fms avc420_copy_or_convert=%.3fms avc420_enqueue=%.3fms encode_and_client_ack=%.3fms flags=0x%8.8x pixel=%s",
            frame_id, (unsigned long long)trace.source_sequence,
            ohos_delta_us(ack_us, trace.capture_acquire_us) / 1000.0,
            ohos_delta_us(trace.bridge_queue_us, trace.capture_acquire_us) / 1000.0,
            ohos_delta_us(trace.submitter_submit_us, trace.submitter_enqueue_us) / 1000.0,
            trace.submitter_copy_us / 1000.0,
            ohos_delta_us(trace.backend_submit_us, trace.submitter_submit_us) / 1000.0,
            ohos_delta_us(trace.backend_copy_done_us, trace.backend_submit_us) / 1000.0,
            ohos_delta_us(trace.draw_start_us, trace.backend_pending_us) / 1000.0,
            trace.gfx_convert_us / 1000.0,
            trace.gfx_enqueue_us / 1000.0,
            ohos_delta_us(ack_us, trace.gfx_enqueue_done_us) / 1000.0,
            flags, ohos_frame_format_name(trace.format));
    }
    else if (!has_trace && (self->frame_ack_count <= 5 ||
             (self->frame_ack_count % 60ULL) == 0))
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.e2e: ack frame=%d has no trace flags=0x%8.8x",
            frame_id, flags);
    }
    ohos_forward_backend_event(self, XRDP_OHOS_BACKEND_EVENT_FRAME_ACK,
                               0, 0, 0, 0, 0, frame_id, flags);
    return 0;
}

static int
ohos_mod_suppress_output(struct mod *mod, int suppress,
                         int left, int top, int right, int bottom)
{
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.frame: suppress=%d rect=(%d,%d,%d,%d)",
        suppress, left, top, right, bottom);
    ohos_from_mod(mod)->suppress_output_count++;
    ohos_forward_backend_event(ohos_from_mod(mod), XRDP_OHOS_BACKEND_EVENT_SUPPRESS_OUTPUT,
                               suppress, left, top, right, bottom, 0, 0);
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

    self->monitor_resize_count++;
    self->width = width;
    self->height = height;
    if (in_progress != 0)
    {
        *in_progress = 0;
    }

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.resize: client resize %dx%d",
        width, height);
    ohos_forward_backend_event(self, XRDP_OHOS_BACKEND_EVENT_MONITOR_RESIZE,
                               0, 0, 0, width, height, 0, 0);
    return ohos_clear_frame(self, "resize waiting for external frame");
}

static int
ohos_mod_server_monitor_full_invalidate(struct mod *mod,
                                        int width, int height)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    self->monitor_full_invalidate_count++;
    self->width = width;
    self->height = height;

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.resize: full invalidate %dx%d",
        width, height);
    ohos_forward_backend_event(self, XRDP_OHOS_BACKEND_EVENT_MONITOR_FULL_INVALIDATE,
                               0, 0, 0, width, height, 0, 0);
    return ohos_clear_frame(self, "full invalidate waiting for external frame");
}

static int
ohos_mod_server_version_message(struct mod *mod)
{
    (void)mod;
    LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.module: server version message");
    return 0;
}

void
ohos_bind_mod_callbacks(struct ohos_mod *self)
{
    if (self == 0)
    {
        return;
    }

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
}
