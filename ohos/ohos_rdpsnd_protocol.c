/*
 * rdpsnd protocol PDUs and PCM chunking for the OHOS backend.
 */

#include "ohos_rdpsnd_private.h"

int
ohos_rdpsnd_send_server_formats(struct ohos_rdpsnd *rdpsnd)
{
    struct stream *s;
    char *size_ptr;
    int bytes;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 128);
    out_uint16_le(s, SNDC_FORMATS);
    size_ptr = s->p;
    out_uint16_le(s, 0);
    out_uint32_le(s, 0);
    out_uint32_le(s, 0);
    out_uint32_le(s, 0);
    out_uint16_le(s, 0);
    out_uint16_le(s, 1);
    out_uint8(s, rdpsnd->block_no);
    out_uint16_le(s, 5);
    out_uint8(s, 0);

    out_uint16_le(s, WAVE_FORMAT_PCM);
    out_uint16_le(s, OHOS_RDPSND_CHANNELS);
    out_uint32_le(s, OHOS_RDPSND_SAMPLE_RATE);
    out_uint32_le(s, OHOS_RDPSND_AVG_BYTES_PER_SEC);
    out_uint16_le(s, OHOS_RDPSND_BLOCK_ALIGN);
    out_uint16_le(s, OHOS_RDPSND_BITS_PER_SAMPLE);
    out_uint16_le(s, 0);

    s_mark_end(s);
    bytes = (int)((s->end - s->data) - 4);
    size_ptr[0] = bytes;
    size_ptr[1] = bytes >> 8;
    bytes = (int)(s->end - s->data);
    rv = ohos_rdpsnd_send_channel_data(rdpsnd, s->data, bytes);
    free_stream(s);
    return rv;
}

static int
ohos_rdpsnd_send_training(struct ohos_rdpsnd *rdpsnd)
{
    struct stream *s;
    char *size_ptr;
    unsigned int time;
    int bytes;
    int rv;

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 1032);
    out_uint16_le(s, SNDC_TRAINING);
    size_ptr = s->p;
    out_uint16_le(s, 0);
    time = g_get_elapsed_ms();
    rdpsnd->training_sent_time = time;
    out_uint16_le(s, time);
    out_uint16_le(s, 1024);
    out_uint8s(s, 1020);
    s_mark_end(s);
    bytes = (int)((s->end - s->data) - 4);
    size_ptr[0] = bytes;
    size_ptr[1] = bytes >> 8;
    bytes = (int)(s->end - s->data);
    rv = ohos_rdpsnd_send_channel_data(rdpsnd, s->data, bytes);
    free_stream(s);
    return rv;
}

static int
ohos_rdpsnd_send_wave_chunk(struct ohos_rdpsnd *rdpsnd,
                            char *data, int data_bytes)
{
    struct stream *s;
    char *size_ptr;
    unsigned int time;
    int bytes;
    int rv;

    if (data == 0 || data_bytes < 4 ||
            data_bytes > XRDP_OHOS_AUDIO_MAX_BYTES)
    {
        return 1;
    }

    make_stream(s);
    if (s == 0)
    {
        return 1;
    }
    init_stream(s, 16 + data_bytes);
    out_uint16_le(s, SNDC_WAVE);
    size_ptr = s->p;
    out_uint16_le(s, 0);
    time = g_get_elapsed_ms();
    out_uint16_le(s, time);
    out_uint16_le(s, rdpsnd->client_format_index);
    rdpsnd->block_no++;
    out_uint8(s, rdpsnd->block_no);
    rdpsnd->sent_time[rdpsnd->block_no & 0xff] = time;
    out_uint8s(s, 3);
    out_uint8a(s, data, 4);
    s_mark_end(s);
    bytes = (int)((s->end - s->data) - 4);
    bytes += data_bytes;
    bytes -= 4;
    size_ptr[0] = bytes;
    size_ptr[1] = bytes >> 8;
    bytes = (int)(s->end - s->data);
    rv = ohos_rdpsnd_send_channel_data(rdpsnd, s->data, bytes);
    if (rv == 0)
    {
        init_stream(s, data_bytes);
        out_uint32_le(s, 0);
        out_uint8a(s, data + 4, data_bytes - 4);
        s_mark_end(s);
        bytes = (int)(s->end - s->data);
        rv = ohos_rdpsnd_send_channel_data(rdpsnd, s->data, bytes);
    }
    free_stream(s);

    if (rv == 0)
    {
        rdpsnd->sent_chunks++;
        rdpsnd->sent_bytes += data_bytes;
        if (rdpsnd->sent_chunks <= 3 ||
                (rdpsnd->sent_chunks % 120) == 0)
        {
            LOG(LOG_LEVEL_INFO,
                "xrdp.ohos.rdpsnd: sent wave chunk bytes=%d block=%d chunks=%u total_bytes=%u",
                data_bytes, rdpsnd->block_no & 0xff, rdpsnd->sent_chunks,
                rdpsnd->sent_bytes);
        }
    }
    else
    {
        rdpsnd->errors++;
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.rdpsnd: failed to send wave chunk bytes=%d rv=%d",
            data_bytes, rv);
    }
    return rv;
}

static int
ohos_rdpsnd_flush_chunk(struct ohos_rdpsnd *rdpsnd)
{
    int bytes;
    int remaining;
    int rv;

    if (rdpsnd->chunk_bytes < 4)
    {
        return 0;
    }
    bytes = rdpsnd->chunk_bytes & ~3;
    remaining = rdpsnd->chunk_bytes - bytes;
    rv = ohos_rdpsnd_send_wave_chunk(rdpsnd, rdpsnd->chunk_buffer, bytes);
    if (rv == 0 && remaining > 0)
    {
        g_memmove(rdpsnd->chunk_buffer, rdpsnd->chunk_buffer + bytes,
                  remaining);
    }
    if (rv == 0)
    {
        rdpsnd->chunk_bytes = remaining;
    }
    return rv;
}

int
ohos_rdpsnd_process_pcm(struct ohos_rdpsnd *rdpsnd,
                        const char *data, int bytes)
{
    int offset = 0;
    int copy_bytes;
    int rv = 0;

    while (rv == 0 && offset < bytes)
    {
        copy_bytes = OHOS_RDPSND_CHUNK_BYTES - rdpsnd->chunk_bytes;
        if (copy_bytes > bytes - offset)
        {
            copy_bytes = bytes - offset;
        }
        g_memcpy(rdpsnd->chunk_buffer + rdpsnd->chunk_bytes,
                 data + offset, copy_bytes);
        rdpsnd->chunk_bytes += copy_bytes;
        offset += copy_bytes;
        if (rdpsnd->chunk_bytes >= OHOS_RDPSND_CHUNK_BYTES)
        {
            rv = ohos_rdpsnd_flush_chunk(rdpsnd);
        }
    }
    if (rv == 0)
    {
        rv = ohos_rdpsnd_flush_chunk(rdpsnd);
    }
    return rv;
}

static int
ohos_rdpsnd_process_formats(struct ohos_rdpsnd *rdpsnd, struct stream *s,
                            int size)
{
    int index;
    int num_formats;
    int w_format_tag;
    int channels;
    int sample_rate;
    int avg_bytes;
    int block_align;
    int bits;
    int cb_size;

    if (size < 20 || !s_check_rem_and_log(s, 20, "OHOS rdpsnd formats"))
    {
        rdpsnd->errors++;
        return 1;
    }

    in_uint8s(s, 14);
    in_uint16_le(s, num_formats);
    in_uint8s(s, 4);
    rdpsnd->client_format_lists++;
    rdpsnd->format_selected = 0;
    rdpsnd->client_format_index = -1;

    for (index = 0; index < num_formats; index++)
    {
        if (!s_check_rem_and_log(s, 18, "OHOS rdpsnd format"))
        {
            rdpsnd->errors++;
            return 1;
        }
        in_uint16_le(s, w_format_tag);
        in_uint16_le(s, channels);
        in_uint32_le(s, sample_rate);
        in_uint32_le(s, avg_bytes);
        in_uint16_le(s, block_align);
        in_uint16_le(s, bits);
        in_uint16_le(s, cb_size);
        if (cb_size < 0 || !s_check_rem_and_log(s, cb_size,
                                                "OHOS rdpsnd format extra"))
        {
            rdpsnd->errors++;
            return 1;
        }
        if (w_format_tag == WAVE_FORMAT_PCM &&
                channels == OHOS_RDPSND_CHANNELS &&
                sample_rate == OHOS_RDPSND_SAMPLE_RATE &&
                avg_bytes == OHOS_RDPSND_AVG_BYTES_PER_SEC &&
                block_align == OHOS_RDPSND_BLOCK_ALIGN &&
                bits == OHOS_RDPSND_BITS_PER_SAMPLE)
        {
            rdpsnd->client_format_index = index;
            rdpsnd->format_selected = 1;
        }
        in_uint8s(s, cb_size);
    }

    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpsnd: client formats=%d selected=%d index=%d lists=%u",
        num_formats, rdpsnd->format_selected, rdpsnd->client_format_index,
        rdpsnd->client_format_lists);

    if (rdpsnd->format_selected)
    {
        return ohos_rdpsnd_send_training(rdpsnd);
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.rdpsnd: client did not accept pcm=%dHz/%dch/%dbit",
        OHOS_RDPSND_SAMPLE_RATE, OHOS_RDPSND_CHANNELS,
        OHOS_RDPSND_BITS_PER_SAMPLE);
    return 0;
}

static int
ohos_rdpsnd_process_training(struct ohos_rdpsnd *rdpsnd)
{
    unsigned int elapsed;

    elapsed = g_get_elapsed_ms() - rdpsnd->training_sent_time;
    LOG(LOG_LEVEL_INFO, "xrdp.ohos.rdpsnd: training round_trip_ms=%u",
        elapsed);
    return 0;
}

static int
ohos_rdpsnd_process_wave_confirm(struct ohos_rdpsnd *rdpsnd,
                                 struct stream *s, int size)
{
    int timestamp;
    int block_no;
    unsigned int diff;

    if (size < 3 || !s_check_rem_and_log(s, 3, "OHOS rdpsnd confirm"))
    {
        rdpsnd->errors++;
        return 1;
    }
    in_uint16_le(s, timestamp);
    in_uint8(s, block_no);
    diff = g_get_elapsed_ms() - rdpsnd->sent_time[block_no & 0xff];
    rdpsnd->confirms++;
    if (rdpsnd->confirms <= 3 || (rdpsnd->confirms % 120) == 0)
    {
        LOG(LOG_LEVEL_INFO,
            "xrdp.ohos.rdpsnd: wave confirm block=%d timestamp=%d diff_ms=%u confirms=%u",
            block_no, timestamp, diff, rdpsnd->confirms);
    }
    return 0;
}

int
ohos_rdpsnd_process_pdu(struct ohos_rdpsnd *rdpsnd, struct stream *s)
{
    int msg_type;
    int size;

    if (rdpsnd == 0 || s == 0 ||
            !s_check_rem_and_log(s, 4, "OHOS rdpsnd header"))
    {
        return 1;
    }
    in_uint16_le(s, msg_type);
    in_uint16_le(s, size);
    switch (msg_type)
    {
        case SNDC_FORMATS:
            return ohos_rdpsnd_process_formats(rdpsnd, s, size);

        case SNDC_TRAINING:
            return ohos_rdpsnd_process_training(rdpsnd);

        case SNDC_WAVECONFIRM:
            return ohos_rdpsnd_process_wave_confirm(rdpsnd, s, size);

        case SNDC_CLOSE:
            LOG(LOG_LEVEL_INFO, "xrdp.ohos.rdpsnd: client close");
            return 0;

        default:
            LOG(LOG_LEVEL_DEBUG,
                "xrdp.ohos.rdpsnd: ignored pdu type=%d size=%d rem=%d",
                msg_type, size, s_rem(s));
            return 0;
    }
}
