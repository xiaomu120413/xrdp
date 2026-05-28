/*
 * HarmonyOS rdpsnd lifecycle and audio queue.
 */

#include "ohos_rdpsnd_private.h"

int
ohos_rdpsnd_lock(struct ohos_rdpsnd *rdpsnd)
{
    if (rdpsnd == 0 || rdpsnd->lock == 0)
    {
        return 1;
    }
    return tc_mutex_lock(rdpsnd->lock);
}

int
ohos_rdpsnd_unlock(struct ohos_rdpsnd *rdpsnd)
{
    if (rdpsnd == 0 || rdpsnd->lock == 0)
    {
        return 1;
    }
    return tc_mutex_unlock(rdpsnd->lock);
}

static void
ohos_rdpsnd_free_buffer(struct ohos_rdpsnd_buffer *buffer)
{
    if (buffer == 0)
    {
        return;
    }
    g_free(buffer->data);
    g_free(buffer);
}

static void
ohos_rdpsnd_reset_queue_locked(struct ohos_rdpsnd *rdpsnd)
{
    struct ohos_rdpsnd_buffer *buffer;

    while (rdpsnd->queue_head != 0)
    {
        buffer = rdpsnd->queue_head;
        rdpsnd->queue_head = buffer->next;
        ohos_rdpsnd_free_buffer(buffer);
    }
    rdpsnd->queue_tail = 0;
    rdpsnd->queued_bytes = 0;
    rdpsnd->chunk_bytes = 0;
}

static void
ohos_rdpsnd_drop_oldest_locked(struct ohos_rdpsnd *rdpsnd)
{
    struct ohos_rdpsnd_buffer *buffer;

    buffer = rdpsnd->queue_head;
    if (buffer == 0)
    {
        return;
    }
    rdpsnd->queue_head = buffer->next;
    if (rdpsnd->queue_head == 0)
    {
        rdpsnd->queue_tail = 0;
    }
    rdpsnd->queued_bytes -= buffer->bytes;
    rdpsnd->dropped_buffers++;
    ohos_rdpsnd_free_buffer(buffer);
}

static void
ohos_rdpsnd_reset_stats(struct ohos_rdpsnd *rdpsnd)
{
    rdpsnd->dropped_buffers = 0;
    rdpsnd->submitted_buffers = 0;
    rdpsnd->sent_chunks = 0;
    rdpsnd->sent_bytes = 0;
    rdpsnd->client_format_lists = 0;
    rdpsnd->confirms = 0;
    rdpsnd->errors = 0;
}

static int
ohos_rdpsnd_audio_format_supported(
    const struct xrdp_ohos_audio_frame *frame)
{
    return frame != 0 && frame->data != 0 &&
           frame->format == XRDP_OHOS_AUDIO_FORMAT_PCM_S16LE &&
           frame->bytes > 0 && frame->bytes <= XRDP_OHOS_AUDIO_MAX_BYTES &&
           frame->sample_rate == OHOS_RDPSND_SAMPLE_RATE &&
           frame->channels == OHOS_RDPSND_CHANNELS &&
           frame->bits_per_sample == OHOS_RDPSND_BITS_PER_SAMPLE;
}

void
ohos_rdpsnd_init(struct ohos_rdpsnd *rdpsnd, struct mod *mod,
                 tintptr wake_obj)
{
    if (rdpsnd == 0)
    {
        return;
    }
    g_memset(rdpsnd, 0, sizeof(struct ohos_rdpsnd));
    rdpsnd->mod = mod;
    rdpsnd->channel_id = -1;
    rdpsnd->client_format_index = -1;
    rdpsnd->wake_obj = wake_obj;
    rdpsnd->lock = tc_mutex_create();
}

void
ohos_rdpsnd_deinit(struct ohos_rdpsnd *rdpsnd)
{
    if (rdpsnd == 0)
    {
        return;
    }
    if (ohos_rdpsnd_lock(rdpsnd) == 0)
    {
        ohos_rdpsnd_reset_queue_locked(rdpsnd);
        ohos_rdpsnd_unlock(rdpsnd);
    }
    ohos_rdpsnd_channel_reset(rdpsnd);
    if (rdpsnd->lock != 0)
    {
        tc_mutex_delete(rdpsnd->lock);
    }
    g_memset(rdpsnd, 0, sizeof(struct ohos_rdpsnd));
    rdpsnd->channel_id = -1;
}

int
ohos_rdpsnd_connect(struct ohos_rdpsnd *rdpsnd)
{
    int rv;

    if (rdpsnd == 0 || rdpsnd->mod == 0)
    {
        return 1;
    }
    rdpsnd->connected = 1;
    rdpsnd->channel_ready = 0;
    rdpsnd->format_selected = 0;
    rdpsnd->client_format_index = -1;
    rdpsnd->block_no = 0;
    ohos_rdpsnd_reset_stats(rdpsnd);

    if (rdpsnd->mod->server_chansrv_in_use != 0 &&
            rdpsnd->mod->server_chansrv_in_use(rdpsnd->mod))
    {
        LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.rdpsnd: chansrv owns rdpsnd channel");
        return 0;
    }
    if (rdpsnd->mod->server_get_channel_id == 0 ||
            rdpsnd->mod->server_send_to_channel == 0)
    {
        LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.rdpsnd: channel callbacks unavailable");
        return 0;
    }

    rdpsnd->channel_id =
        rdpsnd->mod->server_get_channel_id(rdpsnd->mod,
                                           RDPSND_SVC_CHANNEL_NAME);
    if (rdpsnd->channel_id < 0)
    {
        LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.rdpsnd: rdpsnd channel unavailable");
        return 0;
    }

    rv = ohos_rdpsnd_send_server_formats(rdpsnd);
    if (rv == 0)
    {
        rdpsnd->channel_ready = 1;
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.rdpsnd: channel ready id=%d pcm=%dHz/%dch/%dbit",
            rdpsnd->channel_id, OHOS_RDPSND_SAMPLE_RATE,
            OHOS_RDPSND_CHANNELS, OHOS_RDPSND_BITS_PER_SAMPLE);
    }
    else
    {
        rdpsnd->errors++;
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.rdpsnd: channel init failed id=%d rv=%d",
            rdpsnd->channel_id, rv);
    }
    return rv;
}

void
ohos_rdpsnd_disconnect(struct ohos_rdpsnd *rdpsnd)
{
    if (rdpsnd == 0)
    {
        return;
    }
    rdpsnd->connected = 0;
    rdpsnd->channel_ready = 0;
    rdpsnd->format_selected = 0;
    rdpsnd->channel_id = -1;
    rdpsnd->client_format_index = -1;
    if (ohos_rdpsnd_lock(rdpsnd) == 0)
    {
        ohos_rdpsnd_reset_queue_locked(rdpsnd);
        ohos_rdpsnd_unlock(rdpsnd);
    }
    ohos_rdpsnd_channel_reset(rdpsnd);
}

int
ohos_rdpsnd_submit_audio(struct ohos_rdpsnd *rdpsnd,
                         const struct xrdp_ohos_audio_frame *frame)
{
    struct ohos_rdpsnd_buffer *buffer;
    char *data;
    int queued_bytes;
    int dropped;
    int dropped_before;

    if (rdpsnd == 0 || frame == 0 || frame->data == 0 ||
            frame->bytes <= 0 || frame->bytes > XRDP_OHOS_AUDIO_MAX_BYTES)
    {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }
    if (!ohos_rdpsnd_audio_format_supported(frame))
    {
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    buffer = (struct ohos_rdpsnd_buffer *)
             g_malloc(sizeof(struct ohos_rdpsnd_buffer), 1);
    if (buffer == 0)
    {
        return XRDP_OHOS_BACKEND_STATUS_NO_MEMORY;
    }
    data = (char *)g_malloc(frame->bytes, 0);
    if (data == 0)
    {
        g_free(buffer);
        return XRDP_OHOS_BACKEND_STATUS_NO_MEMORY;
    }
    g_memcpy(data, frame->data, frame->bytes);
    buffer->data = data;
    buffer->bytes = frame->bytes;
    buffer->source_timestamp = frame->source_timestamp;

    if (ohos_rdpsnd_lock(rdpsnd) != 0)
    {
        ohos_rdpsnd_free_buffer(buffer);
        return XRDP_OHOS_BACKEND_STATUS_LOCK_FAILED;
    }
    if (!rdpsnd->connected || !rdpsnd->channel_ready ||
            !rdpsnd->format_selected)
    {
        ohos_rdpsnd_unlock(rdpsnd);
        ohos_rdpsnd_free_buffer(buffer);
        return XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION;
    }
    if (rdpsnd->queue_tail != 0)
    {
        rdpsnd->queue_tail->next = buffer;
    }
    else
    {
        rdpsnd->queue_head = buffer;
    }
    rdpsnd->queue_tail = buffer;
    rdpsnd->queued_bytes += buffer->bytes;
    rdpsnd->submitted_buffers++;

    dropped_before = rdpsnd->dropped_buffers;
    while (rdpsnd->queued_bytes > OHOS_RDPSND_MAX_QUEUE_BYTES)
    {
        ohos_rdpsnd_drop_oldest_locked(rdpsnd);
    }
    queued_bytes = rdpsnd->queued_bytes;
    dropped = rdpsnd->dropped_buffers;
    ohos_rdpsnd_unlock(rdpsnd);

    if (rdpsnd->submitted_buffers <= 3 ||
            (rdpsnd->submitted_buffers % 120) == 0 ||
            (dropped != dropped_before &&
             (dropped <= 3 || (dropped % 120) == 0)))
    {
        LOG(dropped != dropped_before ? LOG_LEVEL_WARNING : LOG_LEVEL_DEBUG,
            "xrdp.ohos.rdpsnd: audio queued bytes=%d queued_bytes=%d submitted=%u dropped=%d ts=%llu",
            frame->bytes, queued_bytes, rdpsnd->submitted_buffers, dropped,
            (unsigned long long)frame->source_timestamp);
    }
    if (rdpsnd->wake_obj != 0)
    {
        g_set_wait_obj(rdpsnd->wake_obj);
    }
    return XRDP_OHOS_BACKEND_STATUS_OK;
}

int
ohos_rdpsnd_check_wait_objs(struct ohos_rdpsnd *rdpsnd)
{
    struct ohos_rdpsnd_buffer *buffer;
    int rv = 0;

    if (rdpsnd == 0)
    {
        return 0;
    }

    for (;;)
    {
        if (ohos_rdpsnd_lock(rdpsnd) != 0)
        {
            return 1;
        }
        buffer = rdpsnd->queue_head;
        if (buffer != 0)
        {
            rdpsnd->queue_head = buffer->next;
            if (rdpsnd->queue_head == 0)
            {
                rdpsnd->queue_tail = 0;
            }
            rdpsnd->queued_bytes -= buffer->bytes;
        }
        ohos_rdpsnd_unlock(rdpsnd);

        if (buffer == 0)
        {
            return rv;
        }
        if (rdpsnd->format_selected)
        {
            rv |= ohos_rdpsnd_process_pcm(rdpsnd, buffer->data,
                                          buffer->bytes);
        }
        ohos_rdpsnd_free_buffer(buffer);
    }
}
