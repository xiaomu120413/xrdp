#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "arch.h"
#define XRDP_OHOS_API EXPORT_CC
#include "xrdp_ohos.h"

#include "ohos/ohos_capture_common.h"
#include "ohos/ohos_capture_controller.h"
#include "ohos/ohos_capture_h264.h"
#include "ohos/ohos_capture_raw.h"
#include "ohos/ohos_frame_submitter.h"

#include <atomic>
#include <cstring>
#include <string>

namespace xrdp_ohos {
namespace {

FrameSubmitter& VideoSubmitter()
{
    static FrameSubmitter submitter;
    return submitter;
}

std::atomic<uint64_t> g_encodedBackpressureCount { 0 };

bool SubmitRawFrame(const xrdp_ohos_frame& frame, std::string& message, void*)
{
    return VideoSubmitter().Enqueue(frame, xrdp_ohos_backend_submit_frame, message);
}

bool SubmitEncodedFrame(const xrdp_ohos_encoded_frame& frame, std::string& message, void*)
{
    if (frame.data == nullptr || frame.bytes <= 0 || frame.width <= 0 || frame.height <= 0 ||
        frame.format != XRDP_OHOS_ENCODED_FRAME_FORMAT_H264_AVC420) {
        message = "invalid xrdp encoded video frame";
        return false;
    }

    const int status = xrdp_ohos_backend_submit_encoded_frame(&frame);
    if (status == XRDP_OHOS_BACKEND_STATUS_OK) {
        message = "xrdp encoded video queued bytes=" + std::to_string(frame.bytes);
        return true;
    }
    if (status == XRDP_OHOS_BACKEND_STATUS_BACKPRESSURE) {
        const uint64_t count = g_encodedBackpressureCount.fetch_add(1) + 1;
        message = "xrdp encoded video backpressure count=" + std::to_string(count);
        if (count <= 3 || (count % 60U) == 0U) {
            EmitCaptureInfo(message);
        }
        return false;
    }
    message = "xrdp encoded video submit status=" + std::to_string(status);
    return false;
}

bool SubmitAudioFrame(const xrdp_ohos_audio_frame& frame, std::string& message, void*)
{
    const int status = xrdp_ohos_backend_submit_audio_frame(&frame);
    if (status == XRDP_OHOS_BACKEND_STATUS_OK) {
        message = "xrdp audio queued bytes=" + std::to_string(frame.bytes);
        return true;
    }
    message = "xrdp audio submit status=" + std::to_string(status);
    return false;
}

RawScreenCapture& RawCapture()
{
    static RawScreenCapture capture({
        SubmitRawFrame,
        nullptr,
        SubmitAudioFrame,
        nullptr,
    });
    return capture;
}

SurfaceH264Capture& SurfaceCapture()
{
    static SurfaceH264Capture capture({
        nullptr,
        SubmitEncodedFrame,
        SubmitAudioFrame,
        nullptr,
    });
    return capture;
}

bool StartScreenCapture(const CaptureOptions& options, std::string& message, void*)
{
    std::string surfaceMessage;
    if (SurfaceCapture().Start(options, surfaceMessage)) {
        message = surfaceMessage;
        return true;
    }

    EmitCaptureError("xrdp surface H264 capture unavailable, falling back to raw path: " +
        surfaceMessage);
    std::string rawMessage;
    const bool rawStarted = RawCapture().Start(options, rawMessage);
    message = rawStarted ? rawMessage : surfaceMessage + "; raw fallback failed: " + rawMessage;
    return rawStarted;
}

void StopScreenCapture(const std::string& reason, void*)
{
    SurfaceCapture().Stop(reason);
    RawCapture().Stop(reason);
}

void UpdateScreenCaptureTarget(uint32_t width, uint32_t height, void*)
{
    RawCapture().UpdateTarget(width, height);
}

std::string DescribeDisplayGeometry(void*)
{
    xrdp_ohos_display_geometry geometry {};
    geometry.size = sizeof(geometry);
    if (xrdp_ohos_query_display_geometry(&geometry) != XRDP_OHOS_BACKEND_STATUS_OK ||
        geometry.valid == 0) {
        return "";
    }

    std::string description = "display=" + std::to_string(geometry.width) +
        "x" + std::to_string(geometry.height) +
        " origin=(" + std::to_string(geometry.origin_x) +
        "," + std::to_string(geometry.origin_y) + ")";
    if (geometry.virtual_pixel_ratio_valid != 0) {
        description += " vpr=" + std::to_string(geometry.virtual_pixel_ratio);
    }
    if (geometry.refresh_rate_valid != 0) {
        description += " refresh=" + std::to_string(geometry.refresh_rate);
    }
    return description;
}

CaptureController& Controller()
{
    static CaptureController controller({
        StartScreenCapture,
        StopScreenCapture,
        UpdateScreenCaptureTarget,
        nullptr,
        nullptr,
        DescribeDisplayGeometry,
        nullptr,
    });
    return controller;
}

CaptureDiagnostics ActiveCaptureDiagnostics()
{
    const CaptureDiagnostics surface = SurfaceCapture().Snapshot();
    if (surface.running) {
        return surface;
    }
    return RawCapture().Snapshot();
}

} // namespace
} // namespace xrdp_ohos

extern "C" void
ohos_capture_handle_backend_event(const struct xrdp_ohos_backend_event *event)
{
    if (event == nullptr) {
        return;
    }
    xrdp_ohos::Controller().HandleBackendEvent(*event);
}

extern "C" int EXPORT_CC
xrdp_ohos_capture_get_diagnostics(struct xrdp_ohos_capture_diagnostics *diagnostics)
{
    if (diagnostics == nullptr) {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    const uint32_t callerSize = diagnostics->size;
    const uint32_t writeSize = callerSize < sizeof(struct xrdp_ohos_capture_diagnostics) ?
        callerSize : sizeof(struct xrdp_ohos_capture_diagnostics);
    if (writeSize > 0) {
        std::memset(diagnostics, 0, writeSize);
    }
    if (callerSize >= sizeof(diagnostics->size)) {
        diagnostics->size = sizeof(struct xrdp_ohos_capture_diagnostics);
    }
    if (callerSize < sizeof(struct xrdp_ohos_capture_diagnostics)) {
        return XRDP_OHOS_BACKEND_STATUS_UNSUPPORTED_FORMAT;
    }

    const xrdp_ohos::CaptureDiagnostics capture = xrdp_ohos::ActiveCaptureDiagnostics();
    const xrdp_ohos::FrameSubmitterStats video = xrdp_ohos::VideoSubmitter().Snapshot();
    diagnostics->running = capture.running ? 1U : 0U;
    diagnostics->width = capture.width;
    diagnostics->height = capture.height;
    diagnostics->frame_rate = capture.frameRate;
    diagnostics->show_cursor = capture.showCursor ? 1U : 0U;
    diagnostics->ready_count = capture.readyCount;
    diagnostics->submitted_count = capture.submittedCount;
    diagnostics->dropped_count = capture.droppedCount;
    diagnostics->audio_ready_count = capture.audioReadyCount;
    diagnostics->audio_submitted_count = capture.audioSubmittedCount;
    diagnostics->audio_dropped_count = capture.audioDroppedCount;
    diagnostics->audio_bytes = capture.audioBytes;
    diagnostics->capture_error_count = capture.captureErrorCount;
    diagnostics->video_submitter_running = video.running ? 1U : 0U;
    diagnostics->video_submitter_has_pending = video.hasPending ? 1U : 0U;
    diagnostics->video_submitter_submitting = video.submitting ? 1U : 0U;
    diagnostics->video_queued_count = video.queuedCount;
    diagnostics->video_submitted_count = video.submittedCount;
    diagnostics->video_failed_count = video.failedCount;
    diagnostics->video_replaced_count = video.replacedCount;
    diagnostics->video_precopy_drop_count = video.preCopyDropCount;
    diagnostics->video_backoff_drop_count = video.backoffDropCount;
    diagnostics->video_buffer_allocated_count = video.bufferAllocatedCount;
    diagnostics->video_buffer_reused_count = video.bufferReusedCount;
    diagnostics->video_free_buffer_count = video.freeBufferCount;
    diagnostics->encoded_backpressure_count = xrdp_ohos::g_encodedBackpressureCount.load();
    diagnostics->video_last_status = video.lastStatus;
    diagnostics->video_last_copy_us = video.lastCopyUs;
    diagnostics->video_last_submit_us = video.lastSubmitUs;
    return XRDP_OHOS_BACKEND_STATUS_OK;
}

extern "C" int EXPORT_CC
xrdp_ohos_capture_submit_frame(const struct xrdp_ohos_frame *frame)
{
    if (frame == nullptr) {
        return XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
    }

    std::string message;
    return xrdp_ohos::VideoSubmitter().Enqueue(
        *frame, xrdp_ohos_backend_submit_frame, message) ?
        XRDP_OHOS_BACKEND_STATUS_OK : XRDP_OHOS_BACKEND_STATUS_BACKPRESSURE;
}

extern "C" void EXPORT_CC
xrdp_ohos_capture_reset(const char *reason)
{
    const std::string text = reason == nullptr ? "reset" : reason;
    xrdp_ohos::Controller().Reset(text);
    xrdp_ohos::VideoSubmitter().Stop(text);
}
