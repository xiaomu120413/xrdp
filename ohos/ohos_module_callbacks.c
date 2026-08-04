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
ohos_parse_desktop_limit_param(const char *value, int current_value)
{
    int parsed;

    if (value == 0 || value[0] == '\0')
    {
        return current_value;
    }

    parsed = g_atoi(value);
    if (parsed == 0)
    {
        return 0;
    }
    if (parsed < 200 || parsed > XRDP_OHOS_FRAME_MAX_DIMENSION)
    {
        return current_value;
    }
    return parsed;
}

static int
ohos_access_authorized(struct ohos_mod *self)
{
    if (self->access_code[0] == '\0')
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.auth: access code gate disabled client=%s",
            self->client_name);
        return 1;
    }

    if (self->login_password[0] == '\0')
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.auth: denied client=%s reason=missing_access_password",
            self->client_name);
        return 0;
    }
    if (g_strcmp(self->login_password, self->access_code) != 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.auth: denied client=%s reason=invalid_access_code",
            self->client_name);
        return 0;
    }

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.auth: accepted client=%s",
        self->client_name);
    return 1;
}

static int
ohos_mod_should_log_native_result(int msg)
{
    return msg >= WM_LBUTTONUP && msg <= WM_BUTTON9DOWN;
}

static void
ohos_init_single_monitor(int width, int height, struct monitor_info *monitor)
{
    if (monitor == 0)
    {
        return;
    }

    monitor->left = 0;
    monitor->top = 0;
    monitor->right = width - 1;
    monitor->bottom = height - 1;
    monitor->flags = 0;
    monitor->physical_width = 0;
    monitor->physical_height = 0;
    monitor->orientation = 0;
    monitor->desktop_scale_factor = 0;
    monitor->device_scale_factor = 0;
    monitor->is_primary = TS_MONITOR_PRIMARY;
}

static int
ohos_mod_request_client_desktop_size(struct ohos_mod *self,
                                     const char *reason)
{
    struct monitor_info monitor;
    int rv;

    if (self == 0 || !self->desktop_size.normalized)
    {
        return 0;
    }
    if (self->mod.client_monitor_resize == 0)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.resize: cannot request client desktop reason=%s target=%dx%d missing_client_monitor_resize=1",
            reason == 0 ? "" : reason,
            self->desktop_size.desktop_width,
            self->desktop_size.desktop_height);
        return 1;
    }

    ohos_init_single_monitor(self->desktop_size.desktop_width,
                             self->desktop_size.desktop_height, &monitor);
    rv = self->mod.client_monitor_resize(&self->mod,
                                         self->desktop_size.desktop_width,
                                         self->desktop_size.desktop_height,
                                         1, &monitor);
    LOG(rv == 0 ? LOG_LEVEL_INFO : LOG_LEVEL_WARNING,
        "xrdp.ohos.resize: request client desktop reason=%s requested=%dx%d desktop=%dx%d content=(%d,%d %dx%d) display=%dx%d rv=%d",
        reason == 0 ? "" : reason,
        self->desktop_size.requested_width,
        self->desktop_size.requested_height,
        self->desktop_size.desktop_width,
        self->desktop_size.desktop_height,
        self->desktop_size.target_left,
        self->desktop_size.target_top,
        self->desktop_size.target_width,
        self->desktop_size.target_height,
        self->desktop_size.display_width,
        self->desktop_size.display_height, rv);
    return rv;
}

static int
ohos_mod_update_desktop_size(struct ohos_mod *self, int requested_width,
                             int requested_height, const char *reason,
                             int request_client_resize)
{
    struct ohos_desktop_size desktop;
    int old_width;
    int old_height;
    int resize_rv = 0;

    if (self == 0)
    {
        return 1;
    }

    old_width = self->width;
    old_height = self->height;
    if (ohos_select_desktop_size(requested_width, requested_height,
                                 self->max_desktop_width,
                                 self->max_desktop_height,
                                 &desktop) != 0)
    {
        desktop.requested_width = requested_width;
        desktop.requested_height = requested_height;
        desktop.desktop_width = requested_width;
        desktop.desktop_height = requested_height;
        desktop.target_left = 0;
        desktop.target_top = 0;
        desktop.target_width = requested_width;
        desktop.target_height = requested_height;
        desktop.max_width = self->max_desktop_width;
        desktop.max_height = self->max_desktop_height;
        desktop.display_width = 0;
        desktop.display_height = 0;
        desktop.normalized = 0;
        desktop.limited_by_max = 0;
        desktop.limited_by_aspect = 0;
        desktop.valid_display = 0;
    }

    self->requested_width = requested_width;
    self->requested_height = requested_height;
    self->desktop_size = desktop;
    self->width = desktop.desktop_width;
    self->height = desktop.desktop_height;

    if (old_width != self->width || old_height != self->height ||
            desktop.normalized || desktop.limited_by_aspect)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.resize: desktop reason=%s requested=%dx%d desktop=%dx%d content=(%d,%d %dx%d) old=%dx%d display=%dx%d max=%dx%d display_valid=%d normalized=%d limited_by_max=%d limited_by_aspect=%d connected=%d",
            reason == 0 ? "" : reason,
            desktop.requested_width, desktop.requested_height,
            desktop.desktop_width, desktop.desktop_height,
            desktop.target_left, desktop.target_top,
            desktop.target_width, desktop.target_height,
            old_width, old_height,
            desktop.display_width, desktop.display_height,
            desktop.max_width, desktop.max_height,
            desktop.valid_display, desktop.normalized,
            desktop.limited_by_max, desktop.limited_by_aspect,
            self->connected);
    }

    if (request_client_resize && desktop.normalized)
    {
        resize_rv = ohos_mod_request_client_desktop_size(self, reason);
        if (resize_rv != 0)
        {
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.resize: client resize request failed; keeping selected desktop reason=%s requested=%dx%d desktop=%dx%d content=(%d,%d %dx%d) max=%dx%d limited_by_max=%d limited_by_aspect=%d rv=%d",
                reason == 0 ? "" : reason,
                requested_width, requested_height,
                self->width, self->height,
                desktop.target_left, desktop.target_top,
                desktop.target_width, desktop.target_height,
                desktop.max_width, desktop.max_height,
                desktop.limited_by_max, desktop.limited_by_aspect,
                resize_rv);
        }
    }

    return resize_rv;
}

static void
ohos_forward_desktop_event(struct ohos_mod *self, int type)
{
    int left;
    int top;
    int right;
    int bottom;

    if (self == 0)
    {
        return;
    }

    left = self->desktop_size.target_left;
    top = self->desktop_size.target_top;
    right = left + self->desktop_size.target_width;
    bottom = top + self->desktop_size.target_height;
    ohos_forward_backend_event(self, type, 0, left, top, right, bottom, 0, 0);
}

static int
ohos_mod_start(struct mod *mod, int width, int height, int bpp)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    self->bpp = bpp;

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: start width=%d height=%d bpp=%d",
        width, height, bpp);
    (void)ohos_mod_update_desktop_size(self, width, height, "module_start", 0);
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
    (void)ohos_mod_update_desktop_size(self,
                                       self->requested_width > 0 ?
                                           self->requested_width : self->width,
                                       self->requested_height > 0 ?
                                           self->requested_height :
                                           self->height,
                                       "session_connect", 1);
    ohos_cursor_start_session(self);
    ohos_input_prime_authorization("session connect");
    (void)ohos_rdpdr_print_connect(&self->rdpdr_print);
    (void)ohos_rdpsnd_connect(&self->rdpsnd);
    (void)ohos_cliprdr_connect(&self->cliprdr);
    ohos_forward_desktop_event(self, XRDP_OHOS_BACKEND_EVENT_SESSION_CONNECT);
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
    uint64_t trace_id = ++self->input_trace_count;

    switch (msg)
    {
        case WM_KEYDOWN:
        case WM_KEYUP:
            self->key_event_count++;
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.input: stage=module_recv trace=%llu key=%s flags=%ld code=%ld extra=(%ld,%ld) desktop=%dx%d connected=%d",
                (unsigned long long)trace_id,
                msg == WM_KEYDOWN ? "down" : "up",
                param1, param2, param3, param4,
                self->width, self->height, self->connected);
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
            self->last_mouse_move_trace_id = trace_id;
            self->last_mouse_move_x = (long)param1;
            self->last_mouse_move_y = (long)param2;
            self->last_mouse_move_us = ohos_now_us();
            if ((self->mouse_move_count % OHOS_MOUSE_LOG_SAMPLE) == 0)
            {
                LOG(LOG_LEVEL_DEBUG,
                    "xrdp.ohos.input: stage=module_recv trace=%llu mouse_move x=%ld y=%ld desktop=%dx%d count=%d connected=%d",
                    (unsigned long long)trace_id,
                    param1, param2, self->width, self->height,
                    self->mouse_move_count, self->connected);
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
                "xrdp.ohos.input: stage=module_recv trace=%llu mouse_button msg=%d x=%ld y=%ld extra=(%ld,%ld) desktop=%dx%d count=%llu connected=%d last_move_trace=%llu last_move=(%ld,%ld) last_move_age_us=%llu",
                (unsigned long long)trace_id,
                msg, param1, param2, param3, param4,
                self->width, self->height,
                (unsigned long long)self->mouse_button_event_count,
                self->connected,
                (unsigned long long)self->last_mouse_move_trace_id,
                self->last_mouse_move_x, self->last_mouse_move_y,
                (unsigned long long)ohos_delta_us(ohos_now_us(),
                                                  self->last_mouse_move_us));
            break;

        case WM_CHANNEL_DATA:
        {
            int rv = 0;
            self->channel_data_event_count++;
            rv |= ohos_rdpdr_print_process_channel_data(&self->rdpdr_print,
                                                        param1, param2,
                                                        param3, param4);
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
                          trace_id, &input_event);
    input_rc = ohos_input_handle_event(&self->input, &input_event);
    if (input_rc == 0)
    {
        ohos_cursor_handle_pointer_event(self, msg, param1, param2);
    }
    if (ohos_mod_should_log_native_result(msg))
    {
        LOG(input_rc == 0 ? LOG_LEVEL_DEBUG : LOG_LEVEL_WARNING,
            "xrdp.ohos.input: stage=native_handle trace=%llu result=%s msg=%d p=(%ld,%ld,%ld,%ld) dropped=%llu sent=%llu",
            (unsigned long long)trace_id,
            input_rc == 0 ? "ok" : "drop",
            msg, param1, param2, param3, param4,
            (unsigned long long)self->input.dropped_count,
            (unsigned long long)self->input.sent_count);
    }
    else if (input_rc != 0 && msg != WM_MOUSEMOVE)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.input: stage=native_handle trace=%llu result=drop msg=%d p=(%ld,%ld,%ld,%ld) dropped=%llu sent=%llu",
            (unsigned long long)trace_id,
            msg, param1, param2, param3, param4,
            (unsigned long long)self->input.dropped_count,
            (unsigned long long)self->input.sent_count);
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
    ohos_rdpdr_print_disconnect(&self->rdpdr_print);
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
        g_strncpy(self->client_name, value, sizeof(self->client_name) - 1);
    }
    else if (g_strncmp(name, "password", 255) == 0)
    {
        g_strncpy(self->login_password, value,
                  sizeof(self->login_password) - 1);
    }
    else if (g_strncmp(name, "access_code", 255) == 0)
    {
        g_strncpy(self->access_code, value, sizeof(self->access_code) - 1);
    }
    else if (g_strncmp(name, "max_desktop_width", 255) == 0)
    {
        self->max_desktop_width = ohos_parse_desktop_limit_param(
            value, self->max_desktop_width);
    }
    else if (g_strncmp(name, "max_desktop_height", 255) == 0)
    {
        self->max_desktop_height = ohos_parse_desktop_limit_param(
            value, self->max_desktop_height);
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
    (void)ohos_rdpdr_print_get_wait_objs(&self->rdpdr_print,
                                         read_objs, rcount);
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
        int before_queue = 0;
        int before_pending = 0;
        int after_queue = 0;
        int after_pending = 0;
        uint64_t wake_count = ++self->frame_wait_wake_count;
        if (ohos_lock_frame_state() == 0)
        {
            before_queue = self->h264_queue_count;
            before_pending = self->frame_pending;
            ohos_unlock_frame_state();
        }
        if (wake_count <= 5 || (wake_count % 300ULL) == 0)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.frame: wait wake=%llu before queue=%d pending=%d drawn=%d signals=%llu",
                (unsigned long long)wake_count, before_queue, before_pending,
                self->frame_draw_count,
                (unsigned long long)self->frame_wait_signal_count);
        }
        g_reset_wait_obj(self->frame_wait_obj);
        rv |= ohos_draw_external_frame(self, 0);
        if (ohos_lock_frame_state() == 0)
        {
            after_queue = self->h264_queue_count;
            after_pending = self->frame_pending;
            ohos_unlock_frame_state();
        }
        if (wake_count <= 5 || (wake_count % 300ULL) == 0)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.frame: wait handled wake=%llu rv=%d after queue=%d pending=%d drawn=%d",
                (unsigned long long)wake_count, rv, after_queue, after_pending,
                self->frame_draw_count);
        }
    }
    rv |= ohos_rdpdr_print_check_wait_objs(&self->rdpdr_print);
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
    if (ohos_lock_frame_state() == 0)
    {
        if (frame_id > self->h264_flow_ack_frame_id)
        {
            self->h264_flow_ack_frame_id = frame_id;
        }
        ohos_unlock_frame_state();
    }
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
    LOG(LOG_LEVEL_DEBUG,
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
    (void)ohos_mod_update_desktop_size(self, width, height,
                                       "client_monitor_resize", 1);
    if (in_progress != 0)
    {
        *in_progress = 0;
    }

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.resize: client resize requested=%dx%d active=%dx%d",
        width, height, self->width, self->height);
    ohos_forward_desktop_event(self, XRDP_OHOS_BACKEND_EVENT_MONITOR_RESIZE);
    return ohos_clear_frame(self, "resize waiting for external frame");
}

static int
ohos_mod_server_monitor_full_invalidate(struct mod *mod,
                                        int width, int height)
{
    struct ohos_mod *self = ohos_from_mod(mod);
    self->monitor_full_invalidate_count++;
    (void)ohos_mod_update_desktop_size(self, width, height,
                                       "full_invalidate", 0);

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.resize: full invalidate requested=%dx%d active=%dx%d",
        width, height, self->width, self->height);
    ohos_forward_desktop_event(self,
                               XRDP_OHOS_BACKEND_EVENT_MONITOR_FULL_INVALIDATE);
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
