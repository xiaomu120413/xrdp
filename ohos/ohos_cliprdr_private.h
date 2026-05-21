#ifndef XRDP_OHOS_CLIPRDR_PRIVATE_H
#define XRDP_OHOS_CLIPRDR_PRIVATE_H

#include "ohos_cliprdr.h"

#include "parse.h"

typedef struct OH_PixelmapNative OH_PixelmapNative;
typedef struct OH_UdmfData OH_UdmfData;

#define OHOS_CLIPRDR_ECHO_SUPPRESS_MS 1500U
#define OHOS_CLIPRDR_MAX_IMAGE_BYTES (64 * 1024 * 1024)
#define OHOS_CLIPRDR_MAX_PDU_BYTES OHOS_CLIPRDR_MAX_IMAGE_BYTES
#define OHOS_CLIPRDR_SANDBOX_FILES_DIR "/data/storage/el2/base/files"
#define OHOS_CLIPRDR_CACHE_DIR_NAME "clipboard-cache"
#define OHOS_CLIPRDR_FORMAT_HTML 0xC001
#define OHOS_CLIPRDR_FORMAT_URIW 0xC002
#define OHOS_CLIPRDR_FORMAT_URI_LIST 0xC003
#define OHOS_CLIPRDR_FORMAT_IMAGE_BMP 0xC004
#define OHOS_CLIPRDR_FORMAT_IMAGE_PNG 0xC005
#define OHOS_CLIPRDR_FORMAT_IMAGE_JPEG 0xC006
#define OHOS_CLIPRDR_FORMAT_IMAGE_WEBP 0xC007

#ifndef CF_DIBV5
#define CF_DIBV5 17
#endif

enum ohos_cliprdr_request_kind
{
    OHOS_CLIPRDR_REQUEST_NONE = 0,
    OHOS_CLIPRDR_REQUEST_TEXT,
    OHOS_CLIPRDR_REQUEST_HTML,
    OHOS_CLIPRDR_REQUEST_URIW,
    OHOS_CLIPRDR_REQUEST_URI_LIST,
    OHOS_CLIPRDR_REQUEST_DIB,
    OHOS_CLIPRDR_REQUEST_IMAGE_BMP,
    OHOS_CLIPRDR_REQUEST_IMAGE_PNG,
    OHOS_CLIPRDR_REQUEST_IMAGE_JPEG,
    OHOS_CLIPRDR_REQUEST_IMAGE_WEBP
};

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

int
ohos_cliprdr_pasteboard_read_html(struct ohos_cliprdr *cliprdr,
                                  char **html, char **plain);

int
ohos_cliprdr_pasteboard_write_html(struct ohos_cliprdr *cliprdr,
                                   const char *html, const char *plain);

int
ohos_cliprdr_pasteboard_read_uri(struct ohos_cliprdr *cliprdr,
                                 char **uri);

int
ohos_cliprdr_pasteboard_write_uri(struct ohos_cliprdr *cliprdr,
                                  const char *uri);

OH_UdmfData *
ohos_cliprdr_pasteboard_get_data(struct ohos_cliprdr *cliprdr,
                                 const char *reason, int *status);

void
ohos_cliprdr_pasteboard_begin_remote_write(struct ohos_cliprdr *cliprdr);

void
ohos_cliprdr_pasteboard_cancel_remote_write(struct ohos_cliprdr *cliprdr);

const char *
ohos_cliprdr_pasteboard_status_name(int status);

int
ohos_cliprdr_strcasecmp(const char *left, const char *right);

int
ohos_cliprdr_starts_with(const char *value, const char *prefix);

int
ohos_cliprdr_is_uri_text(const char *value);

const char *
ohos_cliprdr_format_name(int format_id);

int
ohos_cliprdr_image_format_from_signature(const char *data, int bytes);

const char *
ohos_cliprdr_image_extension(int format_id);

char *
ohos_cliprdr_uri_to_local_path(const char *uri);

char *
ohos_cliprdr_read_format_name(struct stream *s, int msg_flags);

int
ohos_cliprdr_name_is(const char *name, int local_format);

int
ohos_cliprdr_image_kind_from_format(int format_id);

char *
ohos_cliprdr_utf16le_to_utf8(const char *data, int bytes);

char *
ohos_cliprdr_bytes_to_text(const char *data, int bytes);

char *
ohos_cliprdr_extract_ms_html(const char *data, int bytes);

char *
ohos_cliprdr_extract_uri_list_first(const char *data, int bytes);

char *
ohos_cliprdr_utf8_to_utf16le(const char *text, int *out_bytes);

char *
ohos_cliprdr_html_to_ms_html(const char *html, int *out_bytes);

char *
ohos_cliprdr_uri_to_uri_list(const char *uri, int *out_bytes);

char *
ohos_cliprdr_bgra_to_dib(const char *bgra, unsigned int width,
                         unsigned int height, int *out_bytes);

char *
ohos_cliprdr_dib_to_bmp(const char *dib, int dib_bytes, int *out_bytes);

int
ohos_cliprdr_decode_image_data_to_pixelmap(const char *data, int bytes,
                                           OH_PixelmapNative **pixelmap,
                                           unsigned int *width,
                                           unsigned int *height);

int
ohos_cliprdr_decode_image_data_to_bgra(const char *data, int bytes,
                                       char **bgra, unsigned int *width,
                                       unsigned int *height);

int
ohos_cliprdr_decode_image_uri_to_bgra(const char *uri, char **bgra,
                                      unsigned int *width,
                                      unsigned int *height);

int
ohos_cliprdr_uri_decodes_as_image(const char *uri);

int
ohos_cliprdr_has_local_image(struct ohos_cliprdr *cliprdr, int *format_id);

int
ohos_cliprdr_read_local_image(struct ohos_cliprdr *cliprdr, int format_id,
                              char **data, int *bytes);

char *
ohos_cliprdr_cache_remote_image(int format_id, const char *data, int bytes);

int
ohos_cliprdr_write_remote_image(struct ohos_cliprdr *cliprdr,
                                int request_kind, const char *data,
                                int bytes);

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
                                      int format_id, int request_kind);

int
ohos_cliprdr_send_format_data_response(struct ohos_cliprdr *cliprdr,
                                       const char *data, int bytes);

int
ohos_cliprdr_send_local_format_list(struct ohos_cliprdr *cliprdr,
                                    const char *reason, int allow_empty);

int
ohos_cliprdr_process_pdu(struct ohos_cliprdr *cliprdr, struct stream *s);

#endif
