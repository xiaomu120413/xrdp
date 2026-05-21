#ifndef XRDP_OHOS_CAPTURE_CONTROLLER_H
#define XRDP_OHOS_CAPTURE_CONTROLLER_H

#include "ohos/ohos_capture_types.h"
#include "xrdp_ohos.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace xrdp_ohos {

using CaptureControllerStartFn = bool (*)(const CaptureOptions& options,
    std::string& message, void* userData);
using CaptureControllerStopFn = void (*)(const std::string& reason, void* userData);
using CaptureControllerUpdateTargetFn = void (*)(uint32_t width, uint32_t height, void* userData);
using CaptureControllerInputFn = void (*)(const std::string& reason, void* userData);
using CaptureControllerGeometryFn = std::string (*)(void* userData);

struct CaptureControllerCallbacks {
    CaptureControllerStartFn startCapture = nullptr;
    CaptureControllerStopFn stopCapture = nullptr;
    CaptureControllerUpdateTargetFn updateTarget = nullptr;
    CaptureControllerInputFn primeInputAuthorization = nullptr;
    CaptureControllerInputFn resetInput = nullptr;
    CaptureControllerGeometryFn describeGeometry = nullptr;
    void* userData = nullptr;
};

class CaptureController {
public:
    explicit CaptureController(CaptureControllerCallbacks callbacks);

    void HandleBackendEvent(const xrdp_ohos_backend_event& event);
    void Reset(const std::string& reason);

private:
    void StartForClient(uint32_t width, uint32_t height);
    void StopForClient(const std::string& reason);
    void ResetState(const std::string& reason);
    void PrimeInputAuthorization(const std::string& reason);
    void ResetInput(const std::string& reason);

    CaptureControllerCallbacks callbacks_;
    std::mutex mutex_;
    bool requested_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::chrono::steady_clock::time_point lastFailure_;
    std::atomic<bool> inputAuthorizationPrimed_ { false };
};

} // namespace xrdp_ohos

#endif
