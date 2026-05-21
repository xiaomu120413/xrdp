/*
 * Static virtual channel transport for the xrdp OHOS cliprdr backend.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_cliprdr_private.h"

#include "log.h"
#include "ms-rdpbcgr.h"
#include "os_calls.h"
#include "parse.h"
#include "xup.h"

void
ohos_cliprdr_channel_reset(struct ohos_cliprdr *cliprdr)
{
    if (cliprdr == 0)
    {
        return;
    }
    free_stream(cliprdr->dechunker_s);
    cliprdr->dechunker_s = 0;
}

void
ohos_cliprdr_out_header(struct stream *s, int msg_type, int msg_flags)
{
    out_uint16_le(s, msg_type);
    out_uint16_le(s, msg_flags);
    s_push_layer(s, channel_hdr, 4);
}

int
ohos_cliprdr_send_stream(struct ohos_cliprdr *cliprdr, struct stream *s)
{
    int rv = 0;
    int datalen;
    int pos = 0;
    int pdu_len = 0;
    int total_data_len;
    int flags;

    if (cliprdr == 0 || cliprdr->mod == 0 ||
            cliprdr->mod->server_send_to_channel == 0 ||
            cliprdr->channel_id < 0 || s == 0)
    {
        return 1;
    }

    s_pop_layer(s, channel_hdr);
    datalen = (int)(s->end - s->p) - 4;
    out_uint32_le(s, datalen);
    total_data_len = (int)(s->end - s->data);

    for (pos = 0; rv == 0 && pos < total_data_len; pos += pdu_len)
    {
        pdu_len = total_data_len - pos;
        if (pdu_len > CHANNEL_CHUNK_LENGTH)
        {
            pdu_len = CHANNEL_CHUNK_LENGTH;
        }

        if (pos == 0)
        {
            flags = ((pos + pdu_len) == total_data_len) ?
                    (XR_CHANNEL_FLAG_FIRST | XR_CHANNEL_FLAG_LAST) :
                    (XR_CHANNEL_FLAG_FIRST | XR_CHANNEL_FLAG_SHOW_PROTOCOL);
        }
        else if ((pos + pdu_len) == total_data_len)
        {
            flags = XR_CHANNEL_FLAG_LAST | XR_CHANNEL_FLAG_SHOW_PROTOCOL;
        }
        else
        {
            flags = XR_CHANNEL_FLAG_SHOW_PROTOCOL;
        }

        rv = cliprdr->mod->server_send_to_channel(cliprdr->mod,
                                                  cliprdr->channel_id,
                                                  s->data + pos, pdu_len,
                                                  total_data_len, flags);
    }

    return rv;
}

int
ohos_cliprdr_process_channel_data(struct ohos_cliprdr *cliprdr,
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

    if (cliprdr == 0 || !cliprdr->channel_ready)
    {
        return 0;
    }

    chan_id = (int)(param1 & 0xffff);
    flags = (int)((param1 >> 16) & 0xffff);
    size = (int)param2;
    data = (char *)param3;
    total_size = (int)param4;
    if (chan_id != cliprdr->channel_id)
    {
        LOG(LOG_LEVEL_DEBUG,
            "xrdp.ohos.cliprdr: ignored channel id=%d cliprdr=%d",
            chan_id, cliprdr->channel_id);
        return 0;
    }
    if (data == 0 || size < 0 || total_size < size ||
            total_size > OHOS_CLIPRDR_MAX_PDU_BYTES)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: invalid channel chunk size=%d total=%d",
            size, total_size);
        cliprdr->errors++;
        return 1;
    }

    first = (flags & XR_CHANNEL_FLAG_FIRST) != 0;
    last = (flags & XR_CHANNEL_FLAG_LAST) != 0;
    if (first && cliprdr->dechunker_s != 0)
    {
        ohos_cliprdr_channel_reset(cliprdr);
    }
    if (first && last)
    {
        struct stream packet_s = { 0 };
        packet_s.data = data;
        packet_s.p = data;
        packet_s.end = data + size;
        packet_s.size = size;
        return ohos_cliprdr_process_pdu(cliprdr, &packet_s);
    }
    if (first)
    {
        make_stream(cliprdr->dechunker_s);
        if (cliprdr->dechunker_s == 0)
        {
            return 1;
        }
        init_stream(cliprdr->dechunker_s, total_size);
        if (cliprdr->dechunker_s->data == 0)
        {
            ohos_cliprdr_channel_reset(cliprdr);
            return 1;
        }
        out_uint8a(cliprdr->dechunker_s, data, size);
        return 0;
    }
    if (cliprdr->dechunker_s == 0)
    {
        LOG(LOG_LEVEL_ERROR,
            "xrdp.ohos.cliprdr: channel chunk without first chunk");
        cliprdr->errors++;
        return 1;
    }
    if (!s_check_rem_out_and_log(cliprdr->dechunker_s, size,
                                 "OHOS cliprdr dechunk"))
    {
        ohos_cliprdr_channel_reset(cliprdr);
        return 1;
    }
    out_uint8a(cliprdr->dechunker_s, data, size);
    if (last)
    {
        s_mark_end(cliprdr->dechunker_s);
        cliprdr->dechunker_s->p = cliprdr->dechunker_s->data;
        rv = ohos_cliprdr_process_pdu(cliprdr, cliprdr->dechunker_s);
        ohos_cliprdr_channel_reset(cliprdr);
    }
    return rv;
}
