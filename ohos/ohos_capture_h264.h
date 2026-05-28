#ifndef XRDP_OHOS_CAPTURE_H264_H
#define XRDP_OHOS_CAPTURE_H264_H

#include "ohos/ohos_capture_types.h"
#include "ohos/ohos_capture_h264_gles.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avscreen_capture.h>
#include <native_window/external_window.h>

namespace xrdp_ohos {

class SurfaceH264Capture {
public:
    explicit SurfaceH264Capture(CaptureSubmitCallbacks callbacks);
    ~SurfaceH264Capture();

    bool Start(CaptureOptions options, std::string& message);
    void Stop(const std::string& reason);
    void NotifyFlowControlOpen();
    CaptureDiagnostics Snapshot();

private:
    static void OnCaptureError(OH_AVScreenCapture*, int32_t errorCode);
    static void OnAudioBufferAvailable(OH_AVScreenCapture* capture, bool isReady,
        OH_AudioCaptureSourceType type);

    void HandleCaptureError(int32_t errorCode);
    void HandleAudioReady(OH_AVScreenCapture* capture, bool isReady, OH_AudioCaptureSourceType type);
    bool CreateCapture(const CaptureOptions& options, OH_AVScreenCapture** outCapture,
        std::string& message);
    void OutputLoop();
    bool DrainOneOutput();
    void UpdateOutputDescription(OH_AVCodec* codec);
    void StoreCodecConfig(const uint8_t* data, size_t bytes);
    void AppendOutputPayload(const uint8_t* data, size_t bytes);
    void SubmitEncodedFrame(const CaptureOptions& target, const OH_AVCodecBufferAttr& attr,
        const std::vector<uint8_t>& payload, bool syncFrame);
    void RequestKeyFrame(const char* reason);

    static void Cleanup(OH_AVCodec* codec, OHNativeWindow* surface,
        OH_AVScreenCapture* capture, bool stopCodec);

    CaptureSubmitCallbacks callbacks_;
    std::mutex mutex_;
    std::atomic<bool> running_ { false };
    OH_AVCodec* codec_ = nullptr;
    OHNativeWindow* inputSurface_ = nullptr;
    OHNativeWindow* captureSurface_ = nullptr;
    OH_AVScreenCapture* capture_ = nullptr;
    OH_VideoSourceType captureSource_ = OH_VIDEO_SOURCE_SURFACE_RGBA;
    std::thread outputThread_;
    CaptureOptions target_;
    std::vector<uint8_t> codecConfig_;
    std::vector<uint8_t> pendingPayload_;
    uint64_t captureErrorCount_ = 0;
    uint64_t lastBackpressureKeyFrameRequestUs_ = 0;
    std::atomic<uint64_t> outputCount_ { 0 };
    std::atomic<uint64_t> submittedCount_ { 0 };
    std::atomic<uint64_t> droppedCount_ { 0 };
    AudioCapturePump audioPump_;
    SurfaceH264GlesStage gpuStage_;
};

} // namespace xrdp_ohos

#endif
