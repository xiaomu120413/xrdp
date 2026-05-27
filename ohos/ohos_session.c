/**
 * HarmonyOS backend session counters and summary logging.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "arch.h"
#include "log.h"

#include <time.h>

#include "ohos_private.h"

uint64_t
ohos_now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

const char *
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

uint64_t
ohos_delta_us(uint64_t later, uint64_t earlier)
{
    if (later == 0 || earlier == 0 || later < earlier)
    {
        return 0;
    }
    return later - earlier;
}

void
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
    self->input_trace_count = 0;
    self->last_mouse_move_trace_id = 0;
    self->last_mouse_move_x = 0;
    self->last_mouse_move_y = 0;
    self->last_mouse_move_us = 0;
    self->input_forwarded_count = 0;
    self->channel_data_event_count = 0;
    self->frame_ack_count = 0;
    self->suppress_output_count = 0;
    self->monitor_resize_count = 0;
    self->monitor_full_invalidate_count = 0;
    self->raw_frame_submit_count = 0;
    self->h264_frame_submit_count = 0;
    self->frame_wait_signal_count = 0;
    self->frame_wait_wake_count = 0;
    self->audio_frame_submit_count = 0;
    self->audio_bytes_submitted = 0;
    self->mouse_move_count = 0;
    self->frame_draw_count = 0;
    self->h264_drop_count = 0;
    self->h264_waiting_for_sync = 0;
    self->h264_flow_ack_frame_id = self->frame_sequence;
    self->h264_pre_encode_skip_count = 0;
    self->h264_last_accept_us = 0;
    self->h264_target_frame_rate = OHOS_H264_DEFAULT_FRAME_RATE;
    self->h264_render_min_interval_us =
        OHOS_H264_DEFAULT_RENDER_MIN_INTERVAL_US;
    ohos_cursor_init(&self->cursor);
}

void
ohos_log_session_summary(struct ohos_mod *self, const char *reason)
{
    uint64_t now_us;
    uint64_t duration_ms = 0;
    int h264_queue_count = 0;
    int h264_drop_count = 0;
    int h264_waiting_for_sync = 0;
    uint64_t h264_pre_encode_skip_count = 0;
    uint32_t h264_target_frame_rate = 0;
    uint64_t h264_render_min_interval_us = 0;

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
        h264_pre_encode_skip_count = self->h264_pre_encode_skip_count;
        h264_target_frame_rate = self->h264_target_frame_rate;
        h264_render_min_interval_us = self->h264_render_min_interval_us;
        ohos_unlock_frame_state();
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.session: disconnect summary reason=%s duration_ms=%llu desktop=%dx%d bpp=%d frames_drawn=%d raw_frames=%llu h264_frames=%llu h264_dropped=%d frame_acks=%llu audio_frames=%llu audio_bytes=%llu audio_sent_chunks=%u audio_dropped=%d audio_errors=%u input_sent=%llu input_dropped=%llu input_unmapped=%llu channel_data=%llu cliprdr_local_lists=%u cliprdr_remote_lists=%u cliprdr_reads=%u cliprdr_writes=%u cliprdr_errors=%u",
        reason == 0 ? "" : reason,
        (unsigned long long)duration_ms, self->width, self->height,
        self->bpp, self->frame_draw_count,
        (unsigned long long)self->raw_frame_submit_count,
        (unsigned long long)self->h264_frame_submit_count,
        h264_drop_count, (unsigned long long)self->frame_ack_count,
        (unsigned long long)self->audio_frame_submit_count,
        (unsigned long long)self->audio_bytes_submitted,
        self->rdpsnd.sent_chunks, self->rdpsnd.dropped_buffers,
        self->rdpsnd.errors,
        (unsigned long long)self->input.sent_count,
        (unsigned long long)self->input.dropped_count,
        (unsigned long long)self->input.unmapped_count,
        (unsigned long long)self->channel_data_event_count,
        self->cliprdr.local_format_lists_sent,
        self->cliprdr.remote_format_lists_received,
        self->cliprdr.pasteboard_reads, self->cliprdr.pasteboard_writes,
        self->cliprdr.errors);
    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.session: frame detail client=%s h264_queue=%d h264_waiting_sync=%d h264_pre_encode_skips=%llu h264_target_fps=%u h264_interval_us=%llu rdpsnd_submitted=%u rdpsnd_sent_chunks=%u rdpsnd_sent_bytes=%u rdpsnd_errors=%u",
        self->client_name, h264_queue_count, h264_waiting_for_sync,
        (unsigned long long)h264_pre_encode_skip_count,
        h264_target_frame_rate,
        (unsigned long long)h264_render_min_interval_us,
        self->rdpsnd.submitted_buffers, self->rdpsnd.sent_chunks,
        self->rdpsnd.sent_bytes, self->rdpsnd.errors);
    LOG(LOG_LEVEL_DEBUG,
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
    ohos_input_log_summary(&self->input, reason);
    LOG(LOG_LEVEL_DEBUG,
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
