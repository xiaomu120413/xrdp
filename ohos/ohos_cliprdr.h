#ifndef XRDP_OHOS_CLIPRDR_H
#define XRDP_OHOS_CLIPRDR_H

#include "arch.h"

struct mod;
struct stream;
typedef struct OH_Pasteboard OH_Pasteboard;
typedef struct OH_PasteboardObserver OH_PasteboardObserver;

struct ohos_cliprdr_remote_file
{
    char *name;
    char *path;
    char *uri;
    int list_index;
    int image_kind;
    int size;
};

struct ohos_cliprdr
{
    struct mod *mod;
    int channel_id;
    int connected;
    int channel_ready;
    int capability_flags;
    int remote_capability_flags;
    int remote_caps_received;
    int requested_format;
    int requested_kind;
    int pending_remote_format;
    int pending_remote_kind;
    int remote_text_format;
    int remote_html_format;
    int remote_uriw_format;
    int remote_uri_list_format;
    int remote_dib_format;
    int remote_dibv5_format;
    int remote_image_bmp_format;
    int remote_image_png_format;
    int remote_image_jpeg_format;
    int remote_image_webp_format;
    int remote_file_group_descriptor_format;
    int remote_file_contents_format;
    struct ohos_cliprdr_remote_file *remote_files;
    int remote_file_count;
    int remote_file_index;
    int remote_file_offset;
    int remote_file_stream_id;
    int remote_file_pending;
    int remote_file_fd;
    struct stream *dechunker_s;
    OH_Pasteboard *pasteboard;
    OH_PasteboardObserver *observer;
    int pasteboard_subscribed;
    tbus lock;
    tintptr wake_obj;
    unsigned int instance_id;
    int local_change_pending;
    unsigned int ignore_local_changes;
    unsigned int ignore_local_changes_until;
    unsigned int remote_write_generation;
    unsigned int seen_remote_write_generation;
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
