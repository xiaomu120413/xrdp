#ifndef XRDP_OHOS_AUDIO_CAPTURE_BRIDGE_H
#define XRDP_OHOS_AUDIO_CAPTURE_BRIDGE_H

#include "xrdp_ohos.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <multimedia/player_framework/native_avscreen_capture.h>

namespace xrdp_ohos {

struct AudioCaptureStats {
    uint64_t readyCount = 0;
    uint64_t submittedCount = 0;
    uint64_t droppedCount = 0;
    uint64_t bytes = 0;
};

using AudioCaptureSubmitFn = bool (*)(
    const xrdp_ohos_audio_frame& frame, std::string& message, void* userData);

void ConfigurePlaybackAudioCapture(OH_AVScreenCaptureConfig& config);

class AudioCapturePump {
public:
    AudioCapturePump(AudioCaptureSubmitFn submit, void* userData);
    ~AudioCapturePump();

    bool Start(OH_AVScreenCapture* capture, const std::string& label);
    void Stop(const std::string& reason);
    void HandleAudioReady(OH_AVScreenCapture* capture, bool isReady, OH_AudioCaptureSourceType type);
    AudioCaptureStats Snapshot() const;

private:
    void WorkerLoop();
    void ProcessAudioBuffers(OH_AVScreenCapture* capture, OH_AudioCaptureSourceType type,
        uint64_t audioReadyCount, uint64_t drainCount);
    bool ProcessOneAudioBuffer(OH_AVScreenCapture* capture, OH_AudioCaptureSourceType type,
        uint64_t audioReadyCount);
    void LogSampledError(const std::string& message, uint64_t count);

    AudioCaptureSubmitFn submit_ = nullptr;
    void* userData_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    OH_AVScreenCapture* capture_ = nullptr;
    std::thread worker_;
    std::string label_;
    bool running_ = false;
    OH_AudioCaptureSourceType audioReadyType_ = OH_SOURCE_INVALID;
    uint64_t audioReadyCount_ = 0;
    uint64_t pendingAudioReadyCount_ = 0;
    std::atomic<uint64_t> audioDroppedCount_ { 0 };
    std::atomic<uint64_t> audioSubmittedCount_ { 0 };
    std::atomic<uint64_t> audioBytes_ { 0 };
};

} // namespace xrdp_ohos

#endif
