#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_private.h"

#include "log.h"
#include "os_calls.h"

static struct ohos_frame_trace *
ohos_trace_slot(struct ohos_mod *self, int frame_id)
{
    if (self == 0 || frame_id <= 0)
    {
        return 0;
    }
    return &self->frame_traces[(unsigned int)frame_id % OHOS_FRAME_TRACE_SLOTS];
}

void
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

int
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

static int
ohos_min(int a, int b)
{
    return (a < b) ? a : b;
}

void
ohos_free_h264_frame(struct ohos_queued_h264_frame *frame)
{
    if (frame == 0)
    {
        return;
    }
    g_free(frame->data);
    g_free(frame);
}

void
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

void
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

int
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
    paint_width = ohos_min(self->desktop_size.target_width, frame_width);
    paint_height = ohos_min(self->desktop_size.target_height, frame_height);
    if (paint_width <= 0 || paint_height <= 0 ||
            self->desktop_size.target_left < 0 ||
            self->desktop_size.target_top < 0 ||
            self->desktop_size.target_left >= self->width ||
            self->desktop_size.target_top >= self->height ||
            mod->server_begin_update == 0 || mod->server_end_update == 0)
    {
        g_free(data);
        ohos_signal_more_frames(wait_obj, more_pending);
        return 0;
    }
    paint_width = ohos_min(paint_width,
                           self->width - self->desktop_size.target_left);
    paint_height = ohos_min(paint_height,
                            self->height - self->desktop_size.target_top);
    if (paint_width <= 0 || paint_height <= 0)
    {
        g_free(data);
        ohos_signal_more_frames(wait_obj, more_pending);
        return 0;
    }

    draw_start_us = ohos_now_us();
    if ((frame_format == XRDP_OHOS_FRAME_FORMAT_H264_AVC420 &&
            ohos_gfx_send_avc420_h264_frame(mod, data,
                                            (int)frame_data_bytes,
                                            self->desktop_size.target_left,
                                            self->desktop_size.target_top,
                                            paint_width, paint_height,
                                            sequence, source_sequence,
                                            &gfx_trace) == 0) ||
            (frame_format == XRDP_OHOS_FRAME_FORMAT_NV12 &&
            ohos_gfx_send_avc420_nv12_frame(mod, data, frame_width,
                                            frame_height, frame_stride,
                                            self->desktop_size.target_left,
                                            self->desktop_size.target_top,
                                            paint_width, paint_height,
                                            sequence, source_sequence,
                                            &gfx_trace) == 0) ||
            (frame_format != XRDP_OHOS_FRAME_FORMAT_NV12 &&
             frame_format != XRDP_OHOS_FRAME_FORMAT_H264_AVC420 &&
             ohos_gfx_send_avc420_frame(mod, data, frame_width, frame_height,
                                        self->desktop_size.target_left,
                                        self->desktop_size.target_top,
                                        paint_width, paint_height, sequence,
                                        source_sequence, &gfx_trace) == 0))
    {
        ohos_update_gfx_trace(self, sequence, frame_format, draw_start_us,
                              &gfx_trace);
        self->frame_draw_count++;
        if (self->frame_draw_count <= 3 ||
                (self->frame_draw_count % 30) == 0)
        {
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.frame: queued AVC420 frame seq=%d source_seq=%llu pixel=%s size=%dx%d dst=(%d,%d %dx%d) desktop=%dx%d bytes=%d",
                sequence, (unsigned long long)source_sequence,
                ohos_frame_format_name(frame_format), frame_width, frame_height,
                self->desktop_size.target_left, self->desktop_size.target_top,
                paint_width, paint_height, self->width, self->height,
                (int)frame_data_bytes);
            LOG(LOG_LEVEL_DEBUG,
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
        ohos_signal_more_frames(wait_obj, more_pending);
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
        rv |= mod->server_paint_rect_bpp(mod,
                                         self->desktop_size.target_left,
                                         self->desktop_size.target_top,
                                         paint_width, paint_height,
                                         data, frame_width, frame_height, 0, 0, 32);
    }
    else if (mod->server_paint_rect != 0)
    {
        rv |= mod->server_paint_rect(mod,
                                     self->desktop_size.target_left,
                                     self->desktop_size.target_top,
                                     paint_width, paint_height,
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
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.frame: painted external BGRA frame seq=%d source_seq=%llu size=%dx%d dst=(%d,%d %dx%d) rv=%d",
            sequence, (unsigned long long)source_sequence, frame_width,
            frame_height, self->desktop_size.target_left,
            self->desktop_size.target_top, paint_width, paint_height, rv);
    }

    g_free(data);
    ohos_signal_more_frames(wait_obj, more_pending);
    return rv;
}

int
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

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.frame: cleared frame %dx%d bpp=%d rv=%d reason=%s",
        self->width, self->height, self->bpp, rv,
        reason == 0 ? "" : reason);
    return rv;
}
