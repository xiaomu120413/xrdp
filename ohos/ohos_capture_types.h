#ifndef XRDP_OHOS_CAPTURE_TYPES_H
#define XRDP_OHOS_CAPTURE_TYPES_H

#include "ohos/ohos_audio_capture_bridge.h"
#include "xrdp_ohos.h"

#include <cstdint>
#include <string>

namespace xrdp_ohos {

struct CaptureOptions {
    uint32_t width = 2560;
    uint32_t height = 1440;
    uint32_t frameRate = 60;
    bool showCursor = true;
};

struct CaptureDiagnostics {
    bool running = false;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frameRate = 0;
    bool showCursor = false;
    uint64_t readyCount = 0;
    uint64_t submittedCount = 0;
    uint64_t droppedCount = 0;
    uint64_t audioReadyCount = 0;
    uint64_t audioSubmittedCount = 0;
    uint64_t audioDroppedCount = 0;
    uint64_t audioBytes = 0;
    uint64_t captureErrorCount = 0;
};

using VideoFrameSubmitFn = bool (*)(const xrdp_ohos_frame& frame,
    std::string& message, void* userData);
using EncodedVideoFrameSubmitFn = bool (*)(const xrdp_ohos_encoded_frame& frame,
    std::string& message, void* userData);
using EncodedVideoReadyFn = bool (*)(void* userData);

struct CaptureSubmitCallbacks {
    VideoFrameSubmitFn submitVideo = nullptr;
    EncodedVideoFrameSubmitFn submitEncodedVideo = nullptr;
    EncodedVideoReadyFn canAcceptEncodedVideo = nullptr;
    AudioCaptureSubmitFn submitAudio = nullptr;
    void* userData = nullptr;
};

} // namespace xrdp_ohos

#endif
