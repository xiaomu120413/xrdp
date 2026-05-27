#ifndef XRDP_OHOS_PRIVATE_H
#define XRDP_OHOS_PRIVATE_H

#include "arch.h"
#include "xup.h"

#include <stddef.h>
#include <stdint.h>

#include "ohos_cliprdr.h"
#include "ohos_cursor.h"
#include "ohos_desktop_size.h"
#include "ohos_gfx_avc420.h"
#include "ohos_input.h"
#include "ohos_rdpsnd.h"

#ifndef XRDP_OHOS_API
#define XRDP_OHOS_API EXPORT_CC
#endif
#include "xrdp_ohos.h"

#define OHOS_MOUSE_LOG_SAMPLE 64
#define OHOS_FRAME_TRACE_SLOTS 256
#define OHOS_H264_QUEUE_LIMIT 30
#define OHOS_DEFAULT_MAX_DESKTOP_WIDTH 2560
#define OHOS_DEFAULT_MAX_DESKTOP_HEIGHT 1440

struct ohos_frame_trace
{
    int valid;
    int frame_id;
    int format;
    uint64_t source_sequence;
    uint64_t capture_timestamp_us;
    uint64_t capture_acquire_us;
    uint64_t bridge_queue_us;
    uint64_t submitter_enqueue_us;
    uint64_t submitter_submit_us;
    uint64_t submitter_copy_us;
    uint64_t backend_submit_us;
    uint64_t backend_copy_done_us;
    uint64_t backend_pending_us;
    uint64_t draw_start_us;
    uint64_t gfx_convert_done_us;
    uint64_t gfx_enqueue_done_us;
    uint32_t gfx_convert_us;
    uint32_t gfx_enqueue_us;
};

struct ohos_queued_h264_frame
{
    char *data;
    size_t data_bytes;
    int width;
    int height;
    int sequence;
    uint64_t source_sequence;
    uint64_t capture_timestamp_us;
    uint64_t capture_acquire_us;
    uint64_t bridge_queue_us;
    uint64_t submitter_enqueue_us;
    uint64_t submitter_submit_us;
    uint64_t submitter_copy_us;
    uint64_t backend_submit_us;
    uint64_t backend_copy_done_us;
    uint64_t backend_pending_us;
    unsigned int flags;
    struct ohos_queued_h264_frame *next;
};

struct ohos_mod
{
    struct mod mod;
    int width;
    int height;
    int requested_width;
    int requested_height;
    int max_desktop_width;
    int max_desktop_height;
    int bpp;
    int connected;
    struct ohos_desktop_size desktop_size;
    uint64_t session_start_us;
    uint64_t key_event_count;
    uint64_t key_sync_event_count;
    uint64_t mouse_move_event_count;
    uint64_t mouse_button_event_count;
    uint64_t input_trace_count;
    uint64_t last_mouse_move_trace_id;
    long last_mouse_move_x;
    long last_mouse_move_y;
    uint64_t last_mouse_move_us;
    uint64_t input_forwarded_count;
    uint64_t channel_data_event_count;
    uint64_t frame_ack_count;
    uint64_t suppress_output_count;
    uint64_t monitor_resize_count;
    uint64_t monitor_full_invalidate_count;
    uint64_t raw_frame_submit_count;
    uint64_t h264_frame_submit_count;
    uint64_t audio_frame_submit_count;
    uint64_t audio_bytes_submitted;
    int mouse_move_count;
    int frame_draw_count;
    int frame_sequence;
    int frame_format;
    int frame_stride;
    size_t frame_data_bytes;
    uint64_t frame_source_sequence;
    uint64_t frame_capture_timestamp_us;
    uint64_t frame_capture_acquire_us;
    uint64_t frame_bridge_queue_us;
    uint64_t frame_submitter_enqueue_us;
    uint64_t frame_submitter_submit_us;
    uint64_t frame_submitter_copy_us;
    uint64_t frame_backend_submit_us;
    uint64_t frame_backend_copy_done_us;
    uint64_t frame_backend_pending_us;
    int frame_width;
    int frame_height;
    int frame_pending;
    char *frame_data;
    tintptr frame_wait_obj;
    struct ohos_queued_h264_frame *h264_head;
    struct ohos_queued_h264_frame *h264_tail;
    int h264_queue_count;
    int h264_drop_count;
    int h264_waiting_for_sync;
    char client_name[256];
    char access_code[64];
    char login_password[256];
    struct ohos_cursor_state cursor;
    struct ohos_input_context input;
    struct ohos_cliprdr cliprdr;
    struct ohos_rdpsnd rdpsnd;
    struct ohos_frame_trace frame_traces[OHOS_FRAME_TRACE_SLOTS];
};

extern tbus g_ohos_frame_mutex;
extern struct ohos_mod *g_ohos_active_mod;
extern int g_ohos_frame_sequence;

uint64_t
ohos_now_us(void);

int
ohos_init_frame_state(void);

int
ohos_lock_frame_state(void);

int
ohos_unlock_frame_state(void);

const char *
ohos_frame_format_name(int format);

uint64_t
ohos_delta_us(uint64_t later, uint64_t earlier);

void
ohos_reset_session_stats(struct ohos_mod *self);

void
ohos_log_session_summary(struct ohos_mod *self, const char *reason);

void
ohos_bind_mod_callbacks(struct ohos_mod *self);

void
ohos_free_h264_frame(struct ohos_queued_h264_frame *frame);

void
ohos_clear_h264_queue_locked(struct ohos_mod *self);

void
ohos_discard_pending_frame(struct ohos_mod *self);

int
ohos_draw_external_frame(struct ohos_mod *self, int *painted);

int
ohos_clear_frame(struct ohos_mod *self, const char *reason);

void
ohos_store_frame_trace_locked(struct ohos_mod *self);

int
ohos_lookup_frame_trace(struct ohos_mod *self, int frame_id,
                        struct ohos_frame_trace *out_trace);

void
ohos_fill_input_event(struct ohos_mod *self, int msg, tbus param1,
                      tbus param2, tbus param3, tbus param4,
                      uint64_t trace_id,
                      struct xrdp_ohos_input_event *event);

void
ohos_forward_input_event(struct ohos_mod *self, int msg, tbus param1,
                         tbus param2, tbus param3, tbus param4);

void
ohos_forward_backend_event(struct ohos_mod *self, int type, int suppress,
                           int left, int top, int right, int bottom,
                           int frame_id, int flags);

void
ohos_capture_handle_backend_event(
    const struct xrdp_ohos_backend_event *event);

#endif
