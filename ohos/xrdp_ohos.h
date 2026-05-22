#ifndef XRDP_OHOS_H
#define XRDP_OHOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef XRDP_OHOS_API
#define XRDP_OHOS_API
#endif

#define XRDP_OHOS_API_VERSION 1
#define XRDP_OHOS_MOD_VERSION 4
#define XRDP_OHOS_INPUT_EVENT_VERSION 1
#define XRDP_OHOS_BACKEND_EVENT_VERSION 2
#define XRDP_OHOS_FRAME_MAX_DIMENSION 8192
#define XRDP_OHOS_AUDIO_MAX_BYTES 131072
#define XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC 0x00000001U
#define XRDP_OHOS_FEATURE_RAW_FRAME_SUBMIT 0x00000001U
#define XRDP_OHOS_FEATURE_ENCODED_H264_SUBMIT 0x00000002U
#define XRDP_OHOS_FEATURE_AUDIO_SUBMIT 0x00000004U
#define XRDP_OHOS_FEATURE_INPUT_CALLBACK 0x00000008U
#define XRDP_OHOS_FEATURE_BACKEND_EVENT_CALLBACK 0x00000010U
#define XRDP_OHOS_FEATURE_CLIPRDR 0x00000020U
#define XRDP_OHOS_FEATURE_RDPSND 0x00000040U
#define XRDP_OHOS_FEATURE_DISPLAY_GEOMETRY 0x00000080U
#define XRDP_OHOS_FEATURE_DIRECT_INPUT 0x00000100U
#define XRDP_OHOS_FEATURE_INTERNAL_CAPTURE 0x00000200U
#define XRDP_OHOS_FEATURE_CAPTURE_DIAGNOSTICS 0x00000400U
#define XRDP_OHOS_FEATURE_INPUT_AUTHORIZATION 0x00000800U
#define XRDP_OHOS_INPUT_SESSION_CONNECT 0
#define XRDP_OHOS_INPUT_SESSION_DISCONNECT -1
#define XRDP_OHOS_WM_KEYDOWN 15
#define XRDP_OHOS_WM_KEYUP 16
#define XRDP_OHOS_WM_MOUSEMOVE 100
#define XRDP_OHOS_WM_LBUTTONUP 101
#define XRDP_OHOS_WM_LBUTTONDOWN 102
#define XRDP_OHOS_WM_RBUTTONUP 103
#define XRDP_OHOS_WM_RBUTTONDOWN 104
#define XRDP_OHOS_WM_MBUTTONUP 105
#define XRDP_OHOS_WM_MBUTTONDOWN 106
#define XRDP_OHOS_WM_WHEELUPUP 107
#define XRDP_OHOS_WM_WHEELUPDOWN 108
#define XRDP_OHOS_WM_WHEELDOWNUP 109
#define XRDP_OHOS_WM_WHEELDOWNDOWN 110
#define XRDP_OHOS_WM_HWHEELLEFTUP 111
#define XRDP_OHOS_WM_HWHEELLEFTDOWN 112
#define XRDP_OHOS_WM_HWHEELRIGHTUP 113
#define XRDP_OHOS_WM_HWHEELRIGHTDOWN 114
#define XRDP_OHOS_WM_XBUTTON1UP 115
#define XRDP_OHOS_WM_XBUTTON1DOWN 116
#define XRDP_OHOS_WM_XBUTTON2UP 117
#define XRDP_OHOS_WM_XBUTTON2DOWN 118
#define XRDP_OHOS_WM_TOUCH_VSCROLL 140
#define XRDP_OHOS_WM_TOUCH_HSCROLL 141

enum xrdp_ohos_backend_status
{
    XRDP_OHOS_BACKEND_STATUS_OK = 0,
    XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME = -1,
    XRDP_OHOS_BACKEND_STATUS_NO_MEMORY = -2,
    XRDP_OHOS_BACKEND_STATUS_LOCK_FAILED = -3,
    XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION = -4,
    XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT = -5,
    XRDP_OHOS_BACKEND_STATUS_BACKPRESSURE = -6
};

enum xrdp_ohos_frame_format
{
    XRDP_OHOS_FRAME_FORMAT_BGRA_8888 = 1,
    XRDP_OHOS_FRAME_FORMAT_RGBA_8888 = 2,
    XRDP_OHOS_FRAME_FORMAT_NV12 = 3,
    XRDP_OHOS_FRAME_FORMAT_H264_AVC420 = 4
};

enum xrdp_ohos_encoded_frame_format
{
    XRDP_OHOS_ENCODED_FRAME_FORMAT_H264_AVC420 = 1
};

enum xrdp_ohos_audio_format
{
    XRDP_OHOS_AUDIO_FORMAT_PCM_S16LE = 1
};

enum xrdp_ohos_backend_event_type
{
    XRDP_OHOS_BACKEND_EVENT_SESSION_CONNECT = 1,
    XRDP_OHOS_BACKEND_EVENT_SESSION_DISCONNECT = 2,
    XRDP_OHOS_BACKEND_EVENT_FRAME_ACK = 3,
    XRDP_OHOS_BACKEND_EVENT_SUPPRESS_OUTPUT = 4,
    XRDP_OHOS_BACKEND_EVENT_MONITOR_RESIZE = 5,
    XRDP_OHOS_BACKEND_EVENT_MONITOR_FULL_INVALIDATE = 6
};

struct xrdp_ohos_abi_info
{
    uint32_t size;
    uint32_t api_version;
    uint32_t mod_version;
    uint32_t input_event_version;
    uint32_t backend_event_version;
    uint32_t feature_flags;
    uint32_t status_flags;
    uint32_t reserved;
};

struct xrdp_ohos_display_geometry
{
    uint32_t size;
    uint32_t valid;
    uint64_t display_id;
    int32_t width;
    int32_t height;
    int32_t origin_x;
    int32_t origin_y;
    uint32_t virtual_pixel_ratio_valid;
    float virtual_pixel_ratio;
    uint32_t refresh_rate_valid;
    uint32_t refresh_rate;
    uint32_t source_mode_valid;
    int32_t source_mode;
    uint32_t reserved;
};

struct xrdp_ohos_capture_diagnostics
{
    uint32_t size;
    uint32_t running;
    uint32_t width;
    uint32_t height;
    uint32_t frame_rate;
    uint32_t show_cursor;
    uint64_t ready_count;
    uint64_t submitted_count;
    uint64_t dropped_count;
    uint64_t audio_ready_count;
    uint64_t audio_submitted_count;
    uint64_t audio_dropped_count;
    uint64_t audio_bytes;
    uint64_t capture_error_count;
    uint32_t video_submitter_running;
    uint32_t video_submitter_has_pending;
    uint32_t video_submitter_submitting;
    uint64_t video_queued_count;
    uint64_t video_submitted_count;
    uint64_t video_failed_count;
    uint64_t video_replaced_count;
    uint64_t video_precopy_drop_count;
    uint64_t video_backoff_drop_count;
    uint64_t video_buffer_allocated_count;
    uint64_t video_buffer_reused_count;
    uint64_t video_free_buffer_count;
    uint64_t encoded_backpressure_count;
    int32_t video_last_status;
    uint32_t video_last_copy_us;
    uint32_t video_last_submit_us;
    uint32_t reserved;
};

struct xrdp_ohos_frame
{
    const void *data;
    int width;
    int height;
    int stride;
    int format;
    uint64_t source_sequence;
    uint64_t capture_timestamp_us;
    uint64_t capture_acquire_us;
    uint64_t bridge_queue_us;
    uint64_t submitter_enqueue_us;
    uint64_t submitter_submit_us;
    uint64_t submitter_copy_us;
};

struct xrdp_ohos_audio_frame
{
    const void *data;
    int bytes;
    int sample_rate;
    int channels;
    int bits_per_sample;
    int format;
    uint64_t source_timestamp;
};

struct xrdp_ohos_encoded_frame
{
    const void *data;
    int bytes;
    int width;
    int height;
    int format;
    uint32_t flags;
    uint64_t source_sequence;
    uint64_t capture_timestamp_us;
    uint64_t capture_acquire_us;
    uint64_t bridge_queue_us;
    uint64_t encoder_output_us;
};

struct xrdp_ohos_input_event
{
    int version;
    int msg;
    long param1;
    long param2;
    long param3;
    long param4;
    int width;
    int height;
    int bpp;
    int connected;
    uint64_t trace_id;
};

struct xrdp_ohos_backend_event
{
    int version;
    int type;
    int width;
    int height;
    int bpp;
    int connected;
    int suppress;
    int left;
    int top;
    int right;
    int bottom;
    int frame_id;
    int flags;
    uint64_t source_sequence;
    uint64_t capture_acquire_us;
    uint64_t ack_us;
};

typedef void (*xrdp_ohos_input_event_fn)(
    const struct xrdp_ohos_input_event *event, void *user_data);
typedef void (*xrdp_ohos_backend_event_fn)(
    const struct xrdp_ohos_backend_event *event, void *user_data);

XRDP_OHOS_API int
xrdp_ohos_backend_get_abi_info(struct xrdp_ohos_abi_info *info);

XRDP_OHOS_API int
xrdp_ohos_query_display_geometry(
    struct xrdp_ohos_display_geometry *geometry);

XRDP_OHOS_API int
xrdp_ohos_capture_get_diagnostics(
    struct xrdp_ohos_capture_diagnostics *diagnostics);

XRDP_OHOS_API int
xrdp_ohos_backend_prime_input_authorization(const char *reason);

XRDP_OHOS_API int
xrdp_ohos_capture_submit_frame(const struct xrdp_ohos_frame *frame);

XRDP_OHOS_API void
xrdp_ohos_capture_reset(const char *reason);

XRDP_OHOS_API int
xrdp_ohos_backend_submit_frame(const struct xrdp_ohos_frame *frame);

XRDP_OHOS_API int
xrdp_ohos_backend_submit_encoded_frame(
    const struct xrdp_ohos_encoded_frame *frame);

XRDP_OHOS_API int
xrdp_ohos_backend_submit_audio_frame(
    const struct xrdp_ohos_audio_frame *frame);

XRDP_OHOS_API int
xrdp_ohos_backend_submit_bgra_frame(const void *data, int width, int height,
                                    int stride);

XRDP_OHOS_API int
xrdp_ohos_backend_set_input_callback(xrdp_ohos_input_event_fn callback,
                                     void *user_data);

XRDP_OHOS_API int
xrdp_ohos_backend_set_event_callback(xrdp_ohos_backend_event_fn callback,
                                     void *user_data);

#ifdef __cplusplus
}
#endif

#endif
