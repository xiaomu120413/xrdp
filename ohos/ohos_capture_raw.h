#ifndef XRDP_OHOS_CAPTURE_RAW_H
#define XRDP_OHOS_CAPTURE_RAW_H

#include "ohos/ohos_capture_types.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include <multimedia/player_framework/native_avscreen_capture.h>
#include <native_buffer/native_buffer.h>

namespace xrdp_ohos {

class RawScreenCapture {
public:
    explicit RawScreenCapture(CaptureSubmitCallbacks callbacks);
    ~RawScreenCapture();

    bool Start(CaptureOptions options, std::string& message);
    void Stop(const std::string& reason);
    void UpdateTarget(uint32_t width, uint32_t height);
    CaptureDiagnostics Snapshot();

private:
    static void ReleaseFailedCapture(OH_AVScreenCapture* capture);
    static void OnCaptureError(OH_AVScreenCapture*, int32_t errorCode);
    static void OnAudioBufferAvailable(OH_AVScreenCapture* capture, bool isReady,
        OH_AudioCaptureSourceType type);
    static void OnVideoBufferAvailable(OH_AVScreenCapture* capture, bool isReady);

    void HandleCaptureError(int32_t errorCode);
    void HandleAudioReady(OH_AVScreenCapture* capture, bool isReady, OH_AudioCaptureSourceType type);
    void HandleVideoReady(OH_AVScreenCapture* capture, bool isReady);
    void WorkerLoop();
    void ProcessOneVideoBuffer(OH_AVScreenCapture* capture, uint64_t readyCount);
    void QueueMappedFrame(const OH_NativeBuffer_Config& config, const void* mapped,
        int64_t timestamp, uint64_t captureAcquireUs, const OH_Rect& region, uint64_t readyCount);
    void LogSampledError(const std::string& message, uint64_t count);

    CaptureSubmitCallbacks callbacks_;
    // Serialize Start/Stop without blocking AVScreenCapture callbacks on the state lock.
    std::mutex lifecycleMutex_;
    std::mutex mutex_;
    std::condition_variable condition_;
    OH_AVScreenCapture* capture_ = nullptr;
    std::thread worker_;
    CaptureOptions target_;
    bool running_ = false;
    bool videoReady_ = false;
    uint64_t readyCount_ = 0;
    uint64_t captureErrorCount_ = 0;
    std::atomic<uint64_t> submittedCount_ { 0 };
    std::atomic<uint64_t> droppedCount_ { 0 };
    AudioCapturePump audioPump_;
};

} // namespace xrdp_ohos

#endif
