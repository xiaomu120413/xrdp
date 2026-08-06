#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_private.h"

#include "log.h"
#include "os_calls.h"
#include "string_calls.h"

static uint64_t
ohos_h264_interval_us_from_frame_rate(uint32_t frame_rate)
{
    if (frame_rate == 0 || frame_rate > OHOS_H264_MAX_FRAME_RATE)
    {
        frame_rate = OHOS_H264_DEFAULT_FRAME_RATE;
    }
    return 1000000ULL / frame_rate;
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
                          XRDP_OHOS_FEATURE_RDPSND |
                          XRDP_OHOS_FEATURE_DISPLAY_GEOMETRY |
                          XRDP_OHOS_FEATURE_DIRECT_INPUT |
                          XRDP_OHOS_FEATURE_INTERNAL_CAPTURE |
                          XRDP_OHOS_FEATURE_CAPTURE_DIAGNOSTICS |
                          XRDP_OHOS_FEATURE_INPUT_AUTHORIZATION |
                          XRDP_OHOS_FEATURE_PRINT |
                          XRDP_OHOS_FEATURE_RDPECAM;
    info->status_flags = 0;
    return XRDP_OHOS_BACKEND_STATUS_OK;
}

int EXPORT_CC
xrdp_ohos_backend_prime_input_authorization(const char *reason)
{
    ohos_input_prime_authorization(reason == 0 ? "xrdp backend load" : reason);
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
    int cleared_count;
    int sync_frame;
    int waiting_for_sync;
    uint64_t wait_signal_count;

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

    sync_frame = (frame->flags & XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC) != 0;
    waiting_for_sync = target->h264_waiting_for_sync && !sync_frame;
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
        if (!sync_frame)
        {
            target->h264_drop_count++;
            target->h264_waiting_for_sync = 1;
            LOG(LOG_LEVEL_WARNING,
                "xrdp.ohos.h264: queue overflow count=%d limit=%d source_seq=%llu dropped=%d; waiting for sync frame",
                target->h264_queue_count, OHOS_H264_QUEUE_LIMIT,
                (unsigned long long)frame->source_sequence,
                target->h264_drop_count);
            ohos_unlock_frame_state();
            ohos_free_h264_frame(queued);
            return XRDP_OHOS_BACKEND_STATUS_BACKPRESSURE;
        }
        cleared_count = target->h264_queue_count;
        ohos_clear_h264_queue_locked(target);
        target->h264_drop_count += cleared_count;
        target->h264_waiting_for_sync = 0;
    }
    else if (sync_frame)
    {
        if (target->h264_waiting_for_sync)
        {
            cleared_count = target->h264_queue_count;
            ohos_clear_h264_queue_locked(target);
            target->h264_drop_count += cleared_count;
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
    wait_signal_count = ++target->frame_wait_signal_count;
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
    if (sequence <= 5 || (sequence % 60) == 0 ||
            wait_signal_count <= 5 || (wait_signal_count % 300ULL) == 0)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.h264: queued encoded frame seq=%d source_seq=%llu bytes=%d queue=%d sync=%d wait_signal=%llu wait_obj=%p",
            sequence, (unsigned long long)frame->source_sequence,
            frame->bytes, queue_count,
            (frame->flags & XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC) != 0,
            (unsigned long long)wait_signal_count, (void *)wait_obj);
    }

    return XRDP_OHOS_BACKEND_STATUS_OK;
}

int EXPORT_CC
xrdp_ohos_backend_set_encoded_frame_rate(uint32_t frame_rate)
{
    struct ohos_mod *target;
    uint64_t interval_us;

    if (frame_rate == 0 || frame_rate > OHOS_H264_MAX_FRAME_RATE)
    {
        frame_rate = OHOS_H264_DEFAULT_FRAME_RATE;
    }
    interval_us = ohos_h264_interval_us_from_frame_rate(frame_rate);

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

    target->h264_target_frame_rate = frame_rate;
    target->h264_render_min_interval_us = interval_us;
    ohos_unlock_frame_state();

    LOG(LOG_LEVEL_DEBUG,
        "xrdp.ohos.h264: target frame rate set fps=%u interval_us=%llu",
        frame_rate, (unsigned long long)interval_us);
    return XRDP_OHOS_BACKEND_STATUS_OK;
}

int EXPORT_CC
xrdp_ohos_backend_can_accept_encoded_frame(void)
{
    struct ohos_mod *target;
    const char *skip_reason = 0;
    int frames_in_flight = 0;
    int flow_limit = OHOS_H264_DEFAULT_FLOW_LIMIT;
    int status = XRDP_OHOS_BACKEND_STATUS_BACKPRESSURE;

    if (ohos_lock_frame_state() != 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_LOCK_FAILED;
    }

    target = g_ohos_active_mod;
    if (target == 0 || !target->connected)
    {
        status = XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION;
    }
    else
    {
        if (target->h264_flow_ack_frame_id > target->frame_sequence)
        {
            target->h264_flow_ack_frame_id = target->frame_sequence;
        }
        frames_in_flight = target->frame_sequence -
                           target->h264_flow_ack_frame_id;
        flow_limit = target->h264_flow_limit <= 0 ?
                     OHOS_H264_DEFAULT_FLOW_LIMIT :
                     target->h264_flow_limit;
        if (target->h264_queue_count >= OHOS_H264_QUEUE_LIMIT)
        {
            skip_reason = "module-queue";
        }
        else if (frames_in_flight >= flow_limit)
        {
            skip_reason = "xrdp-frame-ack";
        }
        else
        {
            status = XRDP_OHOS_BACKEND_STATUS_OK;
            skip_reason = 0;
        }
        if (skip_reason != 0)
        {
            target->h264_pre_encode_skip_count++;
            if (target->h264_pre_encode_skip_count <= 3 ||
                    (target->h264_pre_encode_skip_count % 300ULL) == 0)
            {
                LOG(LOG_LEVEL_DEBUG,
                    "xrdp.ohos.h264: skip encode before encoder reason=%s queue=%d queue_limit=%d in_flight=%d flow_limit=%d ack=%d frame=%d skipped=%llu",
                    skip_reason,
                    target->h264_queue_count, OHOS_H264_QUEUE_LIMIT,
                    frames_in_flight, flow_limit,
                    target->h264_flow_ack_frame_id, target->frame_sequence,
                    (unsigned long long)target->h264_pre_encode_skip_count);
            }
        }
    }

    ohos_unlock_frame_state();
    return status;
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
