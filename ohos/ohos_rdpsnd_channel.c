/*
 * Static virtual channel transport for the OHOS rdpsnd backend.
 */

#include "ohos_rdpsnd_private.h"

void
ohos_rdpsnd_channel_reset(struct ohos_rdpsnd *rdpsnd)
{
    if (rdpsnd == 0)
    {
        return;
    }
    free_stream(rdpsnd->dechunker_s);
    rdpsnd->dechunker_s = 0;
}

int
ohos_rdpsnd_send_channel_data(struct ohos_rdpsnd *rdpsnd,
                              char *data, int data_len)
{
    int rv = 0;
    int pos = 0;
    int pdu_len = 0;
    int flags;

    if (rdpsnd == 0 || rdpsnd->mod == 0 ||
            rdpsnd->mod->server_send_to_channel == 0 ||
            rdpsnd->channel_id < 0 || data == 0 || data_len <= 0)
    {
        return 1;
    }

    for (pos = 0; rv == 0 && pos < data_len; pos += pdu_len)
    {
        pdu_len = data_len - pos;
        if (pdu_len > CHANNEL_CHUNK_LENGTH)
        {
            pdu_len = CHANNEL_CHUNK_LENGTH;
        }

        if (pos == 0)
        {
            flags = ((pos + pdu_len) == data_len) ?
                    (XR_CHANNEL_FLAG_FIRST | XR_CHANNEL_FLAG_LAST) :
                    (XR_CHANNEL_FLAG_FIRST | XR_CHANNEL_FLAG_SHOW_PROTOCOL);
        }
        else if ((pos + pdu_len) == data_len)
        {
            flags = XR_CHANNEL_FLAG_LAST | XR_CHANNEL_FLAG_SHOW_PROTOCOL;
        }
        else
        {
            flags = XR_CHANNEL_FLAG_SHOW_PROTOCOL;
        }

        rv = rdpsnd->mod->server_send_to_channel(rdpsnd->mod,
                                                 rdpsnd->channel_id,
                                                 data + pos, pdu_len,
                                                 data_len, flags);
    }

    return rv;
}

int
ohos_rdpsnd_process_channel_data(struct ohos_rdpsnd *rdpsnd,
                                 tbus param1, tbus param2,
                                 tbus param3, tbus param4)
{
    int chan_id;
    int flags;
    int size;
    int total_size;
    char *data;
    int first;
    int last;
    int rv = 0;

    if (rdpsnd == 0 || !rdpsnd->channel_ready)
    {
        return 0;
    }

    chan_id = (int)(param1 & 0xffff);
    flags = (int)((param1 >> 16) & 0xffff);
    size = (int)param2;
    data = (char *)param3;
    total_size = (int)param4;
    if (chan_id != rdpsnd->channel_id)
    {
        return 0;
    }
    if (data == 0 || size < 0 || total_size < size ||
            total_size > OHOS_RDPSND_MAX_PDU_BYTES)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.rdpsnd: invalid channel chunk size=%d total=%d",
            size, total_size);
        rdpsnd->errors++;
        return 1;
    }

    first = (flags & XR_CHANNEL_FLAG_FIRST) != 0;
    last = (flags & XR_CHANNEL_FLAG_LAST) != 0;
    if (first && rdpsnd->dechunker_s != 0)
    {
        ohos_rdpsnd_channel_reset(rdpsnd);
    }
    if (first && last)
    {
        struct stream packet_s = { 0 };
        packet_s.data = data;
        packet_s.p = data;
        packet_s.end = data + size;
        packet_s.size = size;
        return ohos_rdpsnd_process_pdu(rdpsnd, &packet_s);
    }
    if (first)
    {
        make_stream(rdpsnd->dechunker_s);
        if (rdpsnd->dechunker_s == 0)
        {
            return 1;
        }
        init_stream(rdpsnd->dechunker_s, total_size);
        if (rdpsnd->dechunker_s->data == 0)
        {
            ohos_rdpsnd_channel_reset(rdpsnd);
            return 1;
        }
        out_uint8a(rdpsnd->dechunker_s, data, size);
        return 0;
    }
    if (rdpsnd->dechunker_s == 0)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.rdpsnd: channel chunk without first chunk");
        rdpsnd->errors++;
        return 1;
    }
    if (!s_check_rem_out_and_log(rdpsnd->dechunker_s, size,
                                 "OHOS rdpsnd dechunk"))
    {
        ohos_rdpsnd_channel_reset(rdpsnd);
        return 1;
    }
    out_uint8a(rdpsnd->dechunker_s, data, size);
    if (last)
    {
        s_mark_end(rdpsnd->dechunker_s);
        rdpsnd->dechunker_s->p = rdpsnd->dechunker_s->data;
        rv = ohos_rdpsnd_process_pdu(rdpsnd, rdpsnd->dechunker_s);
        ohos_rdpsnd_channel_reset(rdpsnd);
    }
    return rv;
}
