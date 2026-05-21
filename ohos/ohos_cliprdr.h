#ifndef XRDP_OHOS_CLIPRDR_H
#define XRDP_OHOS_CLIPRDR_H

#include "arch.h"

struct mod;
struct stream;
typedef struct OH_Pasteboard OH_Pasteboard;
typedef struct OH_PasteboardObserver OH_PasteboardObserver;

struct ohos_cliprdr
{
    struct mod *mod;
    int channel_id;
    int connected;
    int channel_ready;
    int capability_flags;
    int remote_capability_flags;
    int requested_format;
    int requested_kind;
    int remote_html_format;
    int remote_uriw_format;
    int remote_uri_list_format;
    int remote_image_bmp_format;
    int remote_image_png_format;
    int remote_image_jpeg_format;
    int remote_image_webp_format;
    struct stream *dechunker_s;
    OH_Pasteboard *pasteboard;
    OH_PasteboardObserver *observer;
    int pasteboard_subscribed;
    tbus lock;
    tintptr wake_obj;
    int local_change_pending;
    unsigned int ignore_local_changes;
    unsigned int ignore_local_changes_until;
    unsigned int local_format_lists_sent;
    unsigned int remote_format_lists_received;
    unsigned int local_requests_received;
    unsigned int remote_responses_received;
    unsigned int pasteboard_reads;
    unsigned int pasteboard_writes;
    unsigned int pasteboard_changes;
    unsigned int suppressed_changes;
    unsigned int errors;
};

void
ohos_cliprdr_init(struct ohos_cliprdr *cliprdr, struct mod *mod,
                  tintptr wake_obj);

void
ohos_cliprdr_deinit(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_connect(struct ohos_cliprdr *cliprdr);

void
ohos_cliprdr_disconnect(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_process_channel_data(struct ohos_cliprdr *cliprdr,
                                  tbus param1, tbus param2,
                                  tbus param3, tbus param4);

int
ohos_cliprdr_check_wait_objs(struct ohos_cliprdr *cliprdr);

#endif
