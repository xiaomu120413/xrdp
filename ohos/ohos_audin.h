#ifndef XRDP_OHOS_AUDIN_H
#define XRDP_OHOS_AUDIN_H

#include "arch.h"
#include "xup.h"

#include <stddef.h>
#include <stdint.h>

struct stream;
struct ohos_audin_renderer;

#define OHOS_AUDIN_MAX_FORMATS 64

struct ohos_audin_format
{
    int tag;
    int channels;
    int rate;
    int average_bytes;
    int block_align;
    int bits;
};

struct ohos_audin
{
    struct mod *mod;
    struct xrdp_mod_drdynvc_procs dvc_procs;
    struct ohos_audin_renderer *renderer;
    struct stream *fragment;
    struct ohos_audin_format client_formats[OHOS_AUDIN_MAX_FORMATS];
    int client_format_count;
    int selected_format;
    int channel_id;
    int enabled;
    int connected;
    int dvc_ready;
    int channel_open;
    uint64_t open_count;
    uint64_t data_count;
    uint64_t data_bytes;
    uint64_t dropped_bytes;
    uint64_t error_count;
};

void
ohos_audin_init(struct ohos_audin *audin, struct mod *mod);

void
ohos_audin_deinit(struct ohos_audin *audin);

void
ohos_audin_set_enabled(struct ohos_audin *audin, int enabled);

int
ohos_audin_connect(struct ohos_audin *audin);

void
ohos_audin_disconnect(struct ohos_audin *audin, const char *reason);

int
ohos_audin_drdynvc_ready(struct ohos_audin *audin);

#endif
