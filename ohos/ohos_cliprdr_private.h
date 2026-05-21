#ifndef XRDP_OHOS_CLIPRDR_PRIVATE_H
#define XRDP_OHOS_CLIPRDR_PRIVATE_H

#include "ohos_cliprdr.h"

#include "parse.h"

#define OHOS_CLIPRDR_MAX_PDU_BYTES (4 * 1024 * 1024)
#define OHOS_CLIPRDR_ECHO_SUPPRESS_MS 1500U

int
ohos_cliprdr_lock(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_unlock(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_pasteboard_init(struct ohos_cliprdr *cliprdr);

void
ohos_cliprdr_pasteboard_deinit(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_pasteboard_read_plain_text(struct ohos_cliprdr *cliprdr,
                                        char **text);

int
ohos_cliprdr_pasteboard_write_plain_text(struct ohos_cliprdr *cliprdr,
                                         const char *text);

const char *
ohos_cliprdr_pasteboard_status_name(int status);

void
ohos_cliprdr_channel_reset(struct ohos_cliprdr *cliprdr);

void
ohos_cliprdr_out_header(struct stream *s, int msg_type, int msg_flags);

int
ohos_cliprdr_send_stream(struct ohos_cliprdr *cliprdr, struct stream *s);

int
ohos_cliprdr_send_capabilities(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_send_monitor_ready(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_send_format_list_ok(struct ohos_cliprdr *cliprdr);

int
ohos_cliprdr_send_format_data_request(struct ohos_cliprdr *cliprdr,
                                      int format_id);

int
ohos_cliprdr_send_format_data_response(struct ohos_cliprdr *cliprdr,
                                       int format_id, const char *text);

int
ohos_cliprdr_send_local_format_list(struct ohos_cliprdr *cliprdr,
                                    const char *reason, int allow_empty);

int
ohos_cliprdr_process_pdu(struct ohos_cliprdr *cliprdr, struct stream *s);

#endif
