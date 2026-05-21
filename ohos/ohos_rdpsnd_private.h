#ifndef XRDP_OHOS_RDPSND_PRIVATE_H
#define XRDP_OHOS_RDPSND_PRIVATE_H

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_rdpsnd.h"

#include "log.h"
#include "ms-rdpbcgr.h"
#include "os_calls.h"
#include "parse.h"
#include "string_calls.h"
#include "thread_calls.h"
#include "xrdp_constants.h"
#include "xup.h"

#define OHOS_RDPSND_MAX_PDU_BYTES 262144
#define OHOS_RDPSND_MAX_QUEUE_BYTES (44100 * 2 * 2)
#define OHOS_RDPSND_SAMPLE_RATE 44100
#define OHOS_RDPSND_CHANNELS 2
#define OHOS_RDPSND_BITS_PER_SAMPLE 16
#define OHOS_RDPSND_BLOCK_ALIGN \
    ((OHOS_RDPSND_CHANNELS * OHOS_RDPSND_BITS_PER_SAMPLE) / 8)
#define OHOS_RDPSND_AVG_BYTES_PER_SEC \
    (OHOS_RDPSND_SAMPLE_RATE * OHOS_RDPSND_BLOCK_ALIGN)
#define OHOS_RDPSND_CHUNK_BYTES 8192

#define SNDC_CLOSE 0x01
#define SNDC_WAVE 0x02
#define SNDC_WAVECONFIRM 0x05
#define SNDC_TRAINING 0x06
#define SNDC_FORMATS 0x07

int
ohos_rdpsnd_lock(struct ohos_rdpsnd *rdpsnd);

int
ohos_rdpsnd_unlock(struct ohos_rdpsnd *rdpsnd);

void
ohos_rdpsnd_channel_reset(struct ohos_rdpsnd *rdpsnd);

int
ohos_rdpsnd_send_channel_data(struct ohos_rdpsnd *rdpsnd,
                              char *data, int data_len);

int
ohos_rdpsnd_send_server_formats(struct ohos_rdpsnd *rdpsnd);

int
ohos_rdpsnd_process_pdu(struct ohos_rdpsnd *rdpsnd, struct stream *s);

int
ohos_rdpsnd_process_pcm(struct ohos_rdpsnd *rdpsnd,
                        const char *data, int bytes);

#endif
