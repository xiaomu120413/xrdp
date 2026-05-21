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

#include <time.h>

#define XRDP_OHOS_API EXPORT_CC
#include "ohos_cliprdr.h"
#include "ohos_gfx_avc420.h"
#include "ohos_rdpsnd.h"
#include "xrdp_ohos.h"

#define OHOS_MOUSE_LOG_SAMPLE 64
#define OHOS_FRAME_TRACE_SLOTS 256
#define OHOS_H264_QUEUE_LIMIT 30

struct ohos_frame_trace
{
    int valid;
    int frame_id;
    int format;
    uint64_t source_sequence;
    uint64_t capture_timestamp_us;
    uint64_t capture_acquire_us;
    uint64_t bridge_queue_us;
    uint64_t submitter_enqueue_us;
    uint64_t submitter_submit_us;
    uint64_t submitter_copy_us;
    uint64_t backend_submit_us;
    uint64_t backend_copy_done_us;
    uint64_t backend_pending_us;
    uint64_t draw_start_us;
    uint64_t gfx_convert_done_us;
    uint64_t gfx_enqueue_done_us;
    uint32_t gfx_convert_us;
    uint32_t gfx_enqueue_us;
};

struct ohos_queued_h264_frame
{
    char *data;
    size_t data_bytes;
    int width;
    int height;
    int sequence;
    uint64_t source_sequence;
    uint64_t capture_timestamp_us;
    uint64_t capture_acquire_us;
    uint64_t bridge_queue_us;
    uint64_t submitter_enqueue_us;
    uint64_t submitter_submit_us;
    uint64_t submitter_copy_us;
    uint64_t backend_submit_us;
    uint64_t backend_copy_done_us;
    uint64_t backend_pending_us;
    unsigned int flags;
    struct ohos_queued_h264_frame *next;
};

struct ohos_mod
{
    struct mod mod;
    int width;
    int height;
    int bpp;
    int connected;
    uint64_t session_start_us;
    uint64_t key_event_count;
    uint64_t key_sync_event_count;
    uint64_t mouse_move_event_count;
    uint64_t mouse_button_event_count;
    uint64_t input_forwarded_count;
    uint64_t channel_data_event_count;
    uint64_t frame_ack_count;
    uint64_t suppress_output_count;
    uint64_t monitor_resize_count;
    uint64_t monitor_full_invalidate_count;
    uint64_t raw_frame_submit_count;
    uint64_t h264_frame_submit_count;
    uint64_t audio_frame_submit_count;
    uint64_t audio_bytes_submitted;
    int mouse_move_count;
    int frame_draw_count;
    int frame_sequence;
    int frame_format;
    int frame_stride;
    size_t frame_data_bytes;
    uint64_t frame_source_sequence;
    uint64_t frame_capture_timestamp_us;
    uint64_t frame_capture_acquire_us;
    uint64_t frame_bridge_queue_us;
    uint64_t frame_submitter_enqueue_us;
    uint64_t frame_submitter_submit_us;
    uint64_t frame_submitter_copy_us;
    uint64_t frame_backend_submit_us;
    uint64_t frame_backend_copy_done_us;
    uint64_t frame_backend_pending_us;
    int frame_width;
    int frame_height;
    int frame_pending;
    char *frame_data;
    tintptr frame_wait_obj;
    struct ohos_queued_h264_frame *h264_head;
    struct ohos_queued_h264_frame *h264_tail;
    int h264_queue_count;
    int h264_drop_count;
    int h264_waiting_for_sync;
    char client_name[256];
    struct ohos_cliprdr cliprdr;
    struct ohos_rdpsnd rdpsnd;
    struct ohos_frame_trace frame_traces[OHOS_FRAME_TRACE_SLOTS];
};

static tbus g_ohos_frame_mutex = 0;
static struct ohos_mod *g_ohos_active_mod = 0;
static int g_ohos_frame_sequence = 0;
static tbus g_ohos_input_mutex = 0;
static xrdp_ohos_input_event_fn g_ohos_input_callback = 0;
static void *g_ohos_input_callback_user = 0;
static xrdp_ohos_backend_event_fn g_ohos_event_callback = 0;
static void *g_ohos_event_callback_user = 0;

static uint64_t
ohos_now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

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

static const char *
ohos_frame_format_name(int format)
{
    switch (format)
    {
        case XRDP_OHOS_FRAME_FORMAT_BGRA_8888:
            return "bgra";
        case XRDP_OHOS_FRAME_FORMAT_RGBA_8888:
            return "rgba";
        case XRDP_OHOS_FRAME_FORMAT_NV12:
            return "nv12";
        case XRDP_OHOS_FRAME_FORMAT_H264_AVC420:
            return "h264-avc420";
        default:
            return "unknown";
    }
}

static uint64_t
ohos_delta_us(uint64_t later, uint64_t earlier)
{
    if (later == 0 || earlier == 0 || later < earlier)
    {
        return 0;
    }
    return later - earlier;
}

static void
ohos_reset_session_stats(struct ohos_mod *self)
{
    if (self == 0)
    {
        return;
    }

    self->session_start_us = ohos_now_us();
    self->key_event_count = 0;
    self->key_sync_event_count = 0;
    self->mouse_move_event_count = 0;
    self->mouse_button_event_count = 0;
    self->input_forwarded_count = 0;
    self->channel_data_event_count = 0;
    self->frame_ack_count = 0;
    self->suppress_output_count = 0;
    self->monitor_resize_count = 0;
    self->monitor_full_invalidate_count = 0;
    self->raw_frame_submit_count = 0;
    self->h264_frame_submit_count = 0;
    self->audio_frame_submit_count = 0;
    self->audio_bytes_submitted = 0;
    self->mouse_move_count = 0;
    self->frame_draw_count = 0;
    self->h264_drop_count = 0;
    self->h264_waiting_for_sync = 0;
}

static void
ohos_log_session_summary(struct ohos_mod *self, const char *reason)
{
    uint64_t now_us;
    uint64_t duration_ms = 0;
    int h264_queue_count = 0;
    int h264_drop_count = 0;
    int h264_waiting_for_sync = 0;

    if (self == 0)
    {
        return;
    }

    now_us = ohos_now_us();
    if (now_us >= self->session_start_us)
    {
        duration_ms = (now_us - self->session_start_us) / 1000ULL;
    }
    if (ohos_lock_frame_state() == 0)
    {
        h264_queue_count = self->h264_queue_count;
        h264_drop_count = self->h264_drop_count;
        h264_waiting_for_sync = self->h264_waiting_for_sync;
        ohos_unlock_frame_state();
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.session: summary reason=%s client=%s duration_ms=%llu size=%dx%d bpp=%d frames_drawn=%d raw_submitted=%llu h264_submitted=%llu h264_queue=%d h264_dropped=%d h264_waiting_sync=%d",
        reason == 0 ? "" : reason, self->client_name,
        (unsigned long long)duration_ms, self->width, self->height, self->bpp,
        self->frame_draw_count,
        (unsigned long long)self->raw_frame_submit_count,
        (unsigned long long)self->h264_frame_submit_count,
        h264_queue_count, h264_drop_count, h264_waiting_for_sync);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.session: input keys=%llu key_sync=%llu mouse_move=%llu mouse_button=%llu forwarded=%llu channel_data=%llu frame_ack=%llu suppress=%llu resize=%llu full_invalidate=%llu",
        (unsigned long long)self->key_event_count,
        (unsigned long long)self->key_sync_event_count,
        (unsigned long long)self->mouse_move_event_count,
        (unsigned long long)self->mouse_button_event_count,
        (unsigned long long)self->input_forwarded_count,
        (unsigned long long)self->channel_data_event_count,
        (unsigned long long)self->frame_ack_count,
        (unsigned long long)self->suppress_output_count,
        (unsigned long long)self->monitor_resize_count,
        (unsigned long long)self->monitor_full_invalidate_count);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.session: rdpsnd submitted=%u sent_chunks=%u sent_bytes=%u dropped=%d errors=%u app_audio_frames=%llu app_audio_bytes=%llu cliprdr local_lists=%u remote_lists=%u local_requests=%u remote_responses=%u pb_reads=%u pb_writes=%u pb_changes=%u suppressed=%u errors=%u",
        self->rdpsnd.submitted_buffers, self->rdpsnd.sent_chunks,
        self->rdpsnd.sent_bytes, self->rdpsnd.dropped_buffers,
        self->rdpsnd.errors,
        (unsigned long long)self->audio_frame_submit_count,
        (unsigned long long)self->audio_bytes_submitted,
        self->cliprdr.local_format_lists_sent,
        self->cliprdr.remote_format_lists_received,
        self->cliprdr.local_requests_received,
        self->cliprdr.remote_responses_received,
        self->cliprdr.pasteboard_reads, self->cliprdr.pasteboard_writes,
        self->cliprdr.pasteboard_changes,
        self->cliprdr.suppressed_changes, self->cliprdr.errors);
}

static struct ohos_frame_trace *
ohos_trace_slot(struct ohos_mod *self, int frame_id)
{
    if (self == 0 || frame_id <= 0)
    {
        return 0;
    }
    return &self->frame_traces[(unsigned int)frame_id % OHOS_FRAME_TRACE_SLOTS];
}

static void
ohos_store_frame_trace_locked(struct ohos_mod *self)
{
    struct ohos_frame_trace *trace;

    if (self == 0 || self->frame_sequence <= 0)
    {
        return;
    }

    trace = ohos_trace_slot(self, self->frame_sequence);
    if (trace == 0)
    {
        return;
    }

    trace->valid = 1;
    trace->frame_id = self->frame_sequence;
    trace->format = self->frame_format;
    trace->source_sequence = self->frame_source_sequence;
    trace->capture_timestamp_us = self->frame_capture_timestamp_us;
    trace->capture_acquire_us = self->frame_capture_acquire_us;
    trace->bridge_queue_us = self->frame_bridge_queue_us;
    trace->submitter_enqueue_us = self->frame_submitter_enqueue_us;
    trace->submitter_submit_us = self->frame_submitter_submit_us;
    trace->submitter_copy_us = self->frame_submitter_copy_us;
    trace->backend_submit_us = self->frame_backend_submit_us;
    trace->backend_copy_done_us = self->frame_backend_copy_done_us;
    trace->backend_pending_us = self->frame_backend_pending_us;
    trace->draw_start_us = 0;
    trace->gfx_convert_done_us = 0;
    trace->gfx_enqueue_done_us = 0;
    trace->gfx_convert_us = 0;
    trace->gfx_enqueue_us = 0;
}

static int
ohos_lookup_frame_trace(struct ohos_mod *self, int frame_id,
                        struct ohos_frame_trace *out_trace)
{
    struct ohos_frame_trace *trace;

    if (self == 0 || out_trace == 0 || ohos_lock_frame_state() != 0)
    {
        return 0;
    }

    trace = ohos_trace_slot(self, frame_id);
    if (trace != 0 && trace->valid && trace->frame_id == frame_id)
    {
        *out_trace = *trace;
        ohos_unlock_frame_state();
        return 1;
    }

    ohos_unlock_frame_state();
    return 0;
}

static void
ohos_update_gfx_trace(struct ohos_mod *self, int frame_id, int format,
                      uint64_t draw_start_us,
                      const struct ohos_gfx_avc420_trace *gfx_trace)
{
    struct ohos_frame_trace *trace;

    if (self == 0 || gfx_trace == 0 || ohos_lock_frame_state() != 0)
    {
        return;
    }

    trace = ohos_trace_slot(self, frame_id);
    if (trace != 0 && trace->valid && trace->frame_id == frame_id)
    {
        trace->format = format;
        trace->draw_start_us = draw_start_us;
        trace->gfx_convert_done_us = gfx_trace->convert_done_us;
        trace->gfx_enqueue_done_us = gfx_trace->enqueue_done_us;
        trace->gfx_convert_us = gfx_trace->convert_us;
        trace->gfx_enqueue_us = gfx_trace->enqueue_us;
    }

    ohos_unlock_frame_state();
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

    self->input_forwarded_count++;
    event.version = XRDP_OHOS_INPUT_EVENT_VERSION;
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

static void
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

static int
ohos_min(int a, int b)
{
    return (a < b) ? a : b;
}

static void
ohos_free_h264_frame(struct ohos_queued_h264_frame *frame)
{
    if (frame == 0)
    {
        return;
    }
    g_free(frame->data);
    g_free(frame);
}

static void
ohos_clear_h264_queue_locked(struct ohos_mod *self)
{
    struct ohos_queued_h264_frame *frame;
    struct ohos_queued_h264_frame *next;

    if (self == 0)
    {
        return;
    }

    frame = self->h264_head;
    while (frame != 0)
    {
        next = frame->next;
        ohos_free_h264_frame(frame);
        frame = next;
    }
    self->h264_head = 0;
    self->h264_tail = 0;
    self->h264_queue_count = 0;
    self->h264_waiting_for_sync = 0;
}

static void
ohos_signal_more_frames(tintptr wait_obj, int more_pending)
{
    if (more_pending && wait_obj != 0)
    {
        g_set_wait_obj(wait_obj);
    }
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
    self->frame_format = 0;
    self->frame_stride = 0;
    self->frame_data_bytes = 0;
    ohos_clear_h264_queue_locked(self);
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
    int frame_format = 0;
    int frame_stride = 0;
    size_t frame_data_bytes = 0;
    int sequence = 0;
    uint64_t source_sequence = 0;
    uint64_t capture_acquire_us = 0;
    uint64_t bridge_queue_us = 0;
    uint64_t submitter_enqueue_us = 0;
    uint64_t submitter_submit_us = 0;
    uint64_t submitter_copy_us = 0;
    uint64_t backend_submit_us = 0;
    uint64_t backend_copy_done_us = 0;
    uint64_t backend_pending_us = 0;
    uint64_t draw_start_us;
    struct ohos_gfx_avc420_trace gfx_trace;
    struct ohos_queued_h264_frame *queued_h264 = 0;
    tintptr wait_obj = 0;
    int paint_width;
    int paint_height;
    int more_pending = 0;
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

    if (self->h264_head != 0)
    {
        queued_h264 = self->h264_head;
        self->h264_head = queued_h264->next;
        if (self->h264_head == 0)
        {
            self->h264_tail = 0;
        }
        if (self->h264_queue_count > 0)
        {
            self->h264_queue_count--;
        }

        data = queued_h264->data;
        queued_h264->data = 0;
        frame_width = queued_h264->width;
        frame_height = queued_h264->height;
        frame_format = XRDP_OHOS_FRAME_FORMAT_H264_AVC420;
        frame_stride = (int)queued_h264->data_bytes;
        frame_data_bytes = queued_h264->data_bytes;
        sequence = queued_h264->sequence;
        source_sequence = queued_h264->source_sequence;
        capture_acquire_us = queued_h264->capture_acquire_us;
        bridge_queue_us = queued_h264->bridge_queue_us;
        submitter_enqueue_us = queued_h264->submitter_enqueue_us;
        submitter_submit_us = queued_h264->submitter_submit_us;
        submitter_copy_us = queued_h264->submitter_copy_us;
        backend_submit_us = queued_h264->backend_submit_us;
        backend_copy_done_us = queued_h264->backend_copy_done_us;
        backend_pending_us = queued_h264->backend_pending_us;
        more_pending = self->h264_head != 0 ||
                       (self->frame_pending && self->frame_data != 0);
        wait_obj = self->frame_wait_obj;
        self->frame_pending = more_pending;
        g_free(queued_h264);
    }
    else if (self->frame_pending && self->frame_data != 0)
    {
        data = self->frame_data;
        frame_width = self->frame_width;
        frame_height = self->frame_height;
        frame_format = self->frame_format;
        frame_stride = self->frame_stride;
        frame_data_bytes = self->frame_data_bytes;
        sequence = self->frame_sequence;
        source_sequence = self->frame_source_sequence;
        capture_acquire_us = self->frame_capture_acquire_us;
        bridge_queue_us = self->frame_bridge_queue_us;
        submitter_enqueue_us = self->frame_submitter_enqueue_us;
        submitter_submit_us = self->frame_submitter_submit_us;
        submitter_copy_us = self->frame_submitter_copy_us;
        backend_submit_us = self->frame_backend_submit_us;
        backend_copy_done_us = self->frame_backend_copy_done_us;
        backend_pending_us = self->frame_backend_pending_us;
        self->frame_data = 0;
        self->frame_pending = 0;
        self->frame_data_bytes = 0;
        more_pending = self->h264_head != 0;
        wait_obj = self->frame_wait_obj;
    }

    ohos_unlock_frame_state();

    if (data == 0)
    {
        ohos_signal_more_frames(wait_obj, more_pending);
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
        ohos_signal_more_frames(wait_obj, more_pending);
        return 0;
    }

    draw_start_us = ohos_now_us();
    if ((frame_format == XRDP_OHOS_FRAME_FORMAT_H264_AVC420 &&
            ohos_gfx_send_avc420_h264_frame(mod, data,
                                            (int)frame_data_bytes,
                                            paint_width, paint_height,
                                            sequence, source_sequence,
                                            &gfx_trace) == 0) ||
            (frame_format == XRDP_OHOS_FRAME_FORMAT_NV12 &&
            ohos_gfx_send_avc420_nv12_frame(mod, data, frame_width,
                                            frame_height, frame_stride,
                                            paint_width, paint_height,
                                            sequence, source_sequence,
                                            &gfx_trace) == 0) ||
            (frame_format != XRDP_OHOS_FRAME_FORMAT_NV12 &&
             frame_format != XRDP_OHOS_FRAME_FORMAT_H264_AVC420 &&
             ohos_gfx_send_avc420_frame(mod, data, frame_width, frame_height,
                                        paint_width, paint_height, sequence,
                                        source_sequence, &gfx_trace) == 0))
    {
        ohos_update_gfx_trace(self, sequence, frame_format, draw_start_us,
                              &gfx_trace);
        self->frame_draw_count++;
        if (self->frame_draw_count <= 3 ||
                (self->frame_draw_count % 30) == 0)
        {
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.frame: queued AVC420 frame seq=%d source_seq=%llu pixel=%s size=%dx%d dst=%dx%d bytes=%d",
                sequence, (unsigned long long)source_sequence,
                ohos_frame_format_name(frame_format), frame_width, frame_height,
                paint_width, paint_height, (int)frame_data_bytes);
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.e2e: enqueue frame=%d source_seq=%llu capture_to_bridge=%.3fms bridge_to_submitter=%.3fms submitter_copy=%.3fms submitter_to_backend=%.3fms backend_copy=%.3fms backend_to_draw=%.3fms avc420_copy_or_convert=%.3fms avc420_enqueue=%.3fms pixel=%s",
                sequence, (unsigned long long)source_sequence,
                ohos_delta_us(bridge_queue_us, capture_acquire_us) / 1000.0,
                ohos_delta_us(submitter_enqueue_us, bridge_queue_us) / 1000.0,
                submitter_copy_us / 1000.0,
                ohos_delta_us(backend_submit_us, submitter_submit_us) / 1000.0,
                ohos_delta_us(backend_copy_done_us, backend_submit_us) / 1000.0,
                ohos_delta_us(draw_start_us, backend_pending_us) / 1000.0,
                gfx_trace.convert_us / 1000.0,
                gfx_trace.enqueue_us / 1000.0,
                ohos_frame_format_name(frame_format));
        }
        g_free(data);
        return 0;
    }

    if (frame_format == XRDP_OHOS_FRAME_FORMAT_NV12 ||
            frame_format == XRDP_OHOS_FRAME_FORMAT_H264_AVC420)
    {
        LOG(LOG_LEVEL_WARNING,
            "xrdp.ohos.frame: AVC420 enqueue failed seq=%d source_seq=%llu pixel=%s size=%dx%d bytes=%d",
            sequence, (unsigned long long)source_sequence,
            ohos_frame_format_name(frame_format), frame_width, frame_height,
            (int)frame_data_bytes);
        g_free(data);
        ohos_signal_more_frames(wait_obj, more_pending);
        return 1;
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
            "xrdp.ohos.frame: painted external BGRA frame seq=%d source_seq=%llu size=%dx%d dst=%dx%d rv=%d",
            sequence, (unsigned long long)source_sequence, frame_width,
            frame_height, paint_width, paint_height, rv);
    }

    g_free(data);
    ohos_signal_more_frames(wait_obj, more_pending);
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
    ohos_reset_session_stats(self);
    self->connected = 1;

    if (ohos_lock_frame_state() == 0)
    {
        g_ohos_active_mod = self;
        ohos_unlock_frame_state();
    }

    LOG(LOG_LEVEL_INFO, "xrdp.ohos.module: connect fd=%d client=%s",
        fd, self->client_name);
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
    ohos_log_session_summary(self, "end");
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
    ohos_forward_backend_event(self, XRDP_OHOS_BACKEND_EVENT_SESSION_DISCONNECT,
                               0, 0, 0, 0, 0, 0, 0);
    ohos_forward_input_event(self, XRDP_OHOS_INPUT_SESSION_DISCONNECT, 0, 0, 0, 0);
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
    int rv = 0;
    if (self->frame_wait_obj != 0 && g_is_wait_obj_set(self->frame_wait_obj))
    {
        g_reset_wait_obj(self->frame_wait_obj);
        rv |= ohos_draw_external_frame(self, 0);
    }
    rv |= ohos_rdpsnd_check_wait_objs(&self->rdpsnd);
    rv |= ohos_cliprdr_check_wait_objs(&self->cliprdr);
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
        LOG(LOG_LEVEL_INFO,
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
        LOG(LOG_LEVEL_INFO,
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
    ohos_cliprdr_init(&self->cliprdr, &self->mod, self->frame_wait_obj);
    ohos_rdpsnd_init(&self->rdpsnd, &self->mod, self->frame_wait_obj);
    self->mod.size = sizeof(struct mod);
    self->mod.version = XRDP_OHOS_MOD_VERSION;
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

int EXPORT_CC
xrdp_ohos_backend_get_abi_info(struct xrdp_ohos_abi_info *info)
{
    uint32_t caller_size;
    uint32_t write_size;

    if (info == 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    caller_size = info->size;
    write_size = caller_size < sizeof(struct xrdp_ohos_abi_info) ?
                 caller_size : sizeof(struct xrdp_ohos_abi_info);
    if (write_size > 0)
    {
        g_memset(info, 0, write_size);
    }
    if (caller_size >= sizeof(info->size))
    {
        info->size = sizeof(struct xrdp_ohos_abi_info);
    }
    if (caller_size < sizeof(struct xrdp_ohos_abi_info))
    {
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    info->api_version = XRDP_OHOS_API_VERSION;
    info->mod_version = XRDP_OHOS_MOD_VERSION;
    info->input_event_version = XRDP_OHOS_INPUT_EVENT_VERSION;
    info->backend_event_version = XRDP_OHOS_BACKEND_EVENT_VERSION;
    info->feature_flags = XRDP_OHOS_FEATURE_RAW_FRAME_SUBMIT |
                          XRDP_OHOS_FEATURE_ENCODED_H264_SUBMIT |
                          XRDP_OHOS_FEATURE_AUDIO_SUBMIT |
                          XRDP_OHOS_FEATURE_INPUT_CALLBACK |
                          XRDP_OHOS_FEATURE_BACKEND_EVENT_CALLBACK |
                          XRDP_OHOS_FEATURE_CLIPRDR |
                          XRDP_OHOS_FEATURE_RDPSND;
    info->status_flags = 0;
    return XRDP_OHOS_BACKEND_STATUS_OK;
}

int EXPORT_CC
xrdp_ohos_backend_submit_frame(const struct xrdp_ohos_frame *frame)
{
    struct ohos_mod *target;
    char *packed;
    char *old_data = 0;
    tintptr wait_obj = 0;
    int row;
    int row_bytes;
    int stored_format;
    size_t packed_bytes;
    uint64_t backend_submit_us;
    uint64_t backend_copy_done_us;
    uint64_t backend_pending_us;

    backend_submit_us = ohos_now_us();
    if (frame == 0 || frame->data == 0 || frame->width <= 0 ||
            frame->height <= 0 ||
            frame->width > XRDP_OHOS_FRAME_MAX_DIMENSION ||
            frame->height > XRDP_OHOS_FRAME_MAX_DIMENSION)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    if (frame->format != XRDP_OHOS_FRAME_FORMAT_BGRA_8888 &&
            frame->format != XRDP_OHOS_FRAME_FORMAT_RGBA_8888 &&
            frame->format != XRDP_OHOS_FRAME_FORMAT_NV12)
    {
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    if (frame->format == XRDP_OHOS_FRAME_FORMAT_NV12)
    {
        size_t y_bytes;

        if ((frame->width & 1) != 0 || (frame->height & 1) != 0 ||
                frame->stride < frame->width)
        {
            return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        }
        row_bytes = frame->width;
        y_bytes = (size_t)frame->width * (size_t)frame->height;
        if (y_bytes / (size_t)frame->height != (size_t)frame->width ||
                y_bytes > ((size_t)-1 / 3U) * 2U)
        {
            return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        }
        packed_bytes = y_bytes + (y_bytes / 2U);
        stored_format = XRDP_OHOS_FRAME_FORMAT_NV12;
    }
    else
    {
        row_bytes = frame->width * 4;
        packed_bytes = (size_t)row_bytes * (size_t)frame->height;
        if (frame->stride < row_bytes ||
                packed_bytes / (size_t)frame->height != (size_t)row_bytes)
        {
            return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        }
        stored_format = XRDP_OHOS_FRAME_FORMAT_BGRA_8888;
    }

    if (packed_bytes == 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    packed = (char *)g_malloc(packed_bytes, 0);
    if (packed == 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_NO_MEMORY;
    }

    if (frame->format == XRDP_OHOS_FRAME_FORMAT_NV12)
    {
        const char *source_y = (const char *)frame->data;
        const char *source_uv = source_y +
                                ((size_t)frame->stride * (size_t)frame->height);
        char *target_y = packed;
        char *target_uv = packed + ((size_t)frame->width * (size_t)frame->height);

        for (row = 0; row < frame->height; row++)
        {
            g_memcpy(target_y + ((size_t)row * (size_t)frame->width),
                     source_y + ((size_t)row * (size_t)frame->stride),
                     frame->width);
        }
        for (row = 0; row < frame->height / 2; row++)
        {
            g_memcpy(target_uv + ((size_t)row * (size_t)frame->width),
                     source_uv + ((size_t)row * (size_t)frame->stride),
                     frame->width);
        }
    }
    else
    {
        for (row = 0; row < frame->height; row++)
        {
            const char *source;
            char *target_row;

            source = ((const char *)frame->data) +
                     ((size_t)row * (size_t)frame->stride);
            target_row = packed + ((size_t)row * (size_t)row_bytes);
            if (frame->format == XRDP_OHOS_FRAME_FORMAT_BGRA_8888)
            {
                g_memcpy(target_row, source, row_bytes);
            }
            else
            {
                int x;
                for (x = 0; x < frame->width; x++)
                {
                    const char *src = source + (x * 4);
                    char *dst = target_row + (x * 4);
                    dst[0] = src[2];
                    dst[1] = src[1];
                    dst[2] = src[0];
                    dst[3] = src[3];
                }
            }
        }
    }
    backend_copy_done_us = ohos_now_us();

    if (ohos_lock_frame_state() != 0)
    {
        g_free(packed);
        return XRDP_OHOS_BACKEND_STATUS_LOCK_FAILED;
    }

    target = g_ohos_active_mod;
    if (target == 0 || !target->connected)
    {
        ohos_unlock_frame_state();
        g_free(packed);
        return XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION;
    }

    old_data = target->frame_data;
    ohos_clear_h264_queue_locked(target);
    target->frame_data = packed;
    target->frame_width = frame->width;
    target->frame_height = frame->height;
    target->frame_format = stored_format;
    target->frame_stride = row_bytes;
    target->frame_data_bytes = packed_bytes;
    target->frame_source_sequence = frame->source_sequence;
    target->frame_capture_timestamp_us = frame->capture_timestamp_us;
    target->frame_capture_acquire_us = frame->capture_acquire_us;
    target->frame_bridge_queue_us = frame->bridge_queue_us;
    target->frame_submitter_enqueue_us = frame->submitter_enqueue_us;
    target->frame_submitter_submit_us = frame->submitter_submit_us;
    target->frame_submitter_copy_us = frame->submitter_copy_us;
    target->frame_backend_submit_us = backend_submit_us;
    target->frame_backend_copy_done_us = backend_copy_done_us;
    target->frame_sequence = ++g_ohos_frame_sequence;
    backend_pending_us = ohos_now_us();
    target->frame_backend_pending_us = backend_pending_us;
    target->frame_pending = 1;
    target->raw_frame_submit_count++;
    ohos_store_frame_trace_locked(target);
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

    return XRDP_OHOS_BACKEND_STATUS_OK;
}

int EXPORT_CC
xrdp_ohos_backend_submit_encoded_frame(
    const struct xrdp_ohos_encoded_frame *frame)
{
    struct ohos_mod *target;
    struct ohos_queued_h264_frame *queued;
    char *packed;
    char *old_data = 0;
    tintptr wait_obj = 0;
    uint64_t backend_submit_us;
    uint64_t backend_copy_done_us;
    uint64_t backend_pending_us;
    int sequence;
    int queue_count;
    int waiting_for_sync;

    backend_submit_us = ohos_now_us();
    if (frame == 0 || frame->data == 0 || frame->bytes <= 0 ||
            frame->width <= 0 || frame->height <= 0 ||
            frame->width > XRDP_OHOS_FRAME_MAX_DIMENSION ||
            frame->height > XRDP_OHOS_FRAME_MAX_DIMENSION ||
            frame->format != XRDP_OHOS_ENCODED_FRAME_FORMAT_H264_AVC420)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    packed = (char *)g_malloc(frame->bytes, 0);
    if (packed == 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_NO_MEMORY;
    }
    g_memcpy(packed, frame->data, frame->bytes);
    backend_copy_done_us = ohos_now_us();

    queued = (struct ohos_queued_h264_frame *)
             g_malloc(sizeof(struct ohos_queued_h264_frame), 1);
    if (queued == 0)
    {
        g_free(packed);
        return XRDP_OHOS_BACKEND_STATUS_NO_MEMORY;
    }
    queued->data = packed;
    queued->data_bytes = (size_t)frame->bytes;
    queued->width = frame->width;
    queued->height = frame->height;
    queued->source_sequence = frame->source_sequence;
    queued->capture_timestamp_us = frame->capture_timestamp_us;
    queued->capture_acquire_us = frame->capture_acquire_us;
    queued->bridge_queue_us = frame->bridge_queue_us;
    queued->submitter_enqueue_us = frame->encoder_output_us;
    queued->submitter_submit_us = frame->encoder_output_us;
    queued->submitter_copy_us =
        (backend_copy_done_us >= backend_submit_us) ?
        (backend_copy_done_us - backend_submit_us) : 0;
    queued->backend_submit_us = backend_submit_us;
    queued->backend_copy_done_us = backend_copy_done_us;
    queued->flags = frame->flags;
    queued->next = 0;

    if (ohos_lock_frame_state() != 0)
    {
        ohos_free_h264_frame(queued);
        return XRDP_OHOS_BACKEND_STATUS_LOCK_FAILED;
    }

    target = g_ohos_active_mod;
    if (target == 0 || !target->connected)
    {
        ohos_unlock_frame_state();
        ohos_free_h264_frame(queued);
        return XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION;
    }

    waiting_for_sync = target->h264_waiting_for_sync &&
                       ((frame->flags & XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC) == 0);
    if (waiting_for_sync)
    {
        target->h264_drop_count++;
        if (target->h264_drop_count <= 5 ||
                (target->h264_drop_count % 60) == 0)
        {
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.h264: dropping non-sync frame while waiting for recovery source_seq=%llu dropped=%d",
                (unsigned long long)frame->source_sequence,
                target->h264_drop_count);
        }
        ohos_unlock_frame_state();
        ohos_free_h264_frame(queued);
        return XRDP_OHOS_BACKEND_STATUS_BACKPRESSURE;
    }

    if (target->h264_queue_count >= OHOS_H264_QUEUE_LIMIT)
    {
        target->h264_drop_count++;
        target->h264_waiting_for_sync = 1;
        if ((frame->flags & XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC) == 0)
        {
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.h264: queue overflow count=%d limit=%d source_seq=%llu dropped=%d; waiting for sync frame",
                target->h264_queue_count, OHOS_H264_QUEUE_LIMIT,
                (unsigned long long)frame->source_sequence,
                target->h264_drop_count);
            ohos_unlock_frame_state();
            ohos_free_h264_frame(queued);
            return XRDP_OHOS_BACKEND_STATUS_BACKPRESSURE;
        }
        ohos_clear_h264_queue_locked(target);
        target->h264_waiting_for_sync = 0;
    }
    else if ((frame->flags & XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC) != 0)
    {
        if (target->h264_waiting_for_sync)
        {
            ohos_clear_h264_queue_locked(target);
        }
        target->h264_waiting_for_sync = 0;
    }

    old_data = target->frame_data;
    target->frame_data = 0;
    target->frame_width = frame->width;
    target->frame_height = frame->height;
    target->frame_format = XRDP_OHOS_FRAME_FORMAT_H264_AVC420;
    target->frame_stride = frame->bytes;
    target->frame_data_bytes = (size_t)frame->bytes;
    target->frame_source_sequence = frame->source_sequence;
    target->frame_capture_timestamp_us = frame->capture_timestamp_us;
    target->frame_capture_acquire_us = frame->capture_acquire_us;
    target->frame_bridge_queue_us = frame->bridge_queue_us;
    target->frame_submitter_enqueue_us = frame->encoder_output_us;
    target->frame_submitter_submit_us = frame->encoder_output_us;
    target->frame_submitter_copy_us =
        (backend_copy_done_us >= backend_submit_us) ?
        (backend_copy_done_us - backend_submit_us) : 0;
    target->frame_backend_submit_us = backend_submit_us;
    target->frame_backend_copy_done_us = backend_copy_done_us;
    sequence = ++g_ohos_frame_sequence;
    target->frame_sequence = sequence;
    backend_pending_us = ohos_now_us();
    target->frame_backend_pending_us = backend_pending_us;
    queued->sequence = sequence;
    queued->backend_pending_us = backend_pending_us;
    target->frame_pending = 1;
    ohos_store_frame_trace_locked(target);
    if (target->h264_tail != 0)
    {
        target->h264_tail->next = queued;
    }
    else
    {
        target->h264_head = queued;
    }
    target->h264_tail = queued;
    target->h264_queue_count++;
    target->h264_frame_submit_count++;
    queue_count = target->h264_queue_count;
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
    if (sequence <= 5 || (sequence % 60) == 0 || queue_count > 1)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.h264: queued encoded frame seq=%d source_seq=%llu bytes=%d queue=%d sync=%d",
            sequence, (unsigned long long)frame->source_sequence,
            frame->bytes, queue_count,
            (frame->flags & XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC) != 0);
    }

    return XRDP_OHOS_BACKEND_STATUS_OK;
}

int EXPORT_CC
xrdp_ohos_backend_submit_audio_frame(
    const struct xrdp_ohos_audio_frame *frame)
{
    struct ohos_mod *target;
    int rv;

    if (frame == 0 || frame->data == 0 ||
            frame->bytes <= 0 || frame->bytes > XRDP_OHOS_AUDIO_MAX_BYTES)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (frame->format != XRDP_OHOS_AUDIO_FORMAT_PCM_S16LE)
    {
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    if (ohos_lock_frame_state() != 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_LOCK_FAILED;
    }

    target = g_ohos_active_mod;
    if (target == 0 || !target->connected)
    {
        ohos_unlock_frame_state();
        return XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION;
    }
    rv = ohos_rdpsnd_submit_audio(&target->rdpsnd, frame);
    if (rv == XRDP_OHOS_BACKEND_STATUS_OK)
    {
        target->audio_frame_submit_count++;
        target->audio_bytes_submitted += (uint64_t)frame->bytes;
    }
    ohos_unlock_frame_state();
    return rv;
}

int EXPORT_CC
xrdp_ohos_backend_submit_bgra_frame(const void *data, int width, int height,
                                    int stride)
{
    struct xrdp_ohos_frame frame;

    g_memset(&frame, 0, sizeof(frame));
    frame.data = data;
    frame.width = width;
    frame.height = height;
    frame.stride = stride;
    frame.format = XRDP_OHOS_FRAME_FORMAT_BGRA_8888;
    frame.source_sequence = 0;
    return xrdp_ohos_backend_submit_frame(&frame);
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
