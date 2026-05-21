#ifndef XRDP_OHOS_RDPSND_H
#define XRDP_OHOS_RDPSND_H

#include "arch.h"

#include "xrdp_ohos.h"

struct mod;
struct stream;

struct ohos_rdpsnd_buffer
{
    struct ohos_rdpsnd_buffer *next;
    char *data;
    int bytes;
    uint64_t source_timestamp;
};

struct ohos_rdpsnd
{
    struct mod *mod;
    int channel_id;
    int connected;
    int channel_ready;
    int format_selected;
    int client_format_index;
    int block_no;
    int queued_bytes;
    int dropped_buffers;
    unsigned int training_sent_time;
    unsigned int sent_time[256];
    struct stream *dechunker_s;
    struct ohos_rdpsnd_buffer *queue_head;
    struct ohos_rdpsnd_buffer *queue_tail;
    tbus lock;
    tintptr wake_obj;
    char chunk_buffer[8192];
    int chunk_bytes;
    unsigned int submitted_buffers;
    unsigned int sent_chunks;
    unsigned int sent_bytes;
    unsigned int client_format_lists;
    unsigned int confirms;
    unsigned int errors;
};

void
ohos_rdpsnd_init(struct ohos_rdpsnd *rdpsnd, struct mod *mod,
                 tintptr wake_obj);

void
ohos_rdpsnd_deinit(struct ohos_rdpsnd *rdpsnd);

int
ohos_rdpsnd_connect(struct ohos_rdpsnd *rdpsnd);

void
ohos_rdpsnd_disconnect(struct ohos_rdpsnd *rdpsnd);

int
ohos_rdpsnd_submit_audio(struct ohos_rdpsnd *rdpsnd,
                         const struct xrdp_ohos_audio_frame *frame);

int
ohos_rdpsnd_process_channel_data(struct ohos_rdpsnd *rdpsnd,
                                 tbus param1, tbus param2,
                                 tbus param3, tbus param4);

int
ohos_rdpsnd_check_wait_objs(struct ohos_rdpsnd *rdpsnd);

#endif
