#include "ohos/ohos_capture_controller.h"

#include "ohos/ohos_capture_common.h"

#include <thread>

namespace xrdp_ohos {

CaptureController::CaptureController(CaptureControllerCallbacks callbacks)
    : callbacks_(callbacks)
{
}

void CaptureController::HandleBackendEvent(const xrdp_ohos_backend_event& event)
{
    if (event.width > 0 && event.height > 0 && callbacks_.updateTarget != nullptr) {
        callbacks_.updateTarget(static_cast<uint32_t>(event.width),
            static_cast<uint32_t>(event.height), callbacks_.userData);
    }

    switch (event.type) {
        case XRDP_OHOS_BACKEND_EVENT_SESSION_CONNECT:
            if (event.width > 0 && event.height > 0) {
                StartForClient(static_cast<uint32_t>(event.width),
                    static_cast<uint32_t>(event.height));
            }
            PrimeInputAuthorization("xrdp client connected");
            break;
        case XRDP_OHOS_BACKEND_EVENT_SESSION_DISCONNECT:
            inputAuthorizationPrimed_.store(false);
            ResetState("xrdp client disconnected");
            break;
        case XRDP_OHOS_BACKEND_EVENT_MONITOR_RESIZE:
        case XRDP_OHOS_BACKEND_EVENT_MONITOR_FULL_INVALIDATE:
            if (event.connected != 0 && event.width > 0 && event.height > 0) {
                StartForClient(static_cast<uint32_t>(event.width),
                    static_cast<uint32_t>(event.height));
            }
            break;
        case XRDP_OHOS_BACKEND_EVENT_SUPPRESS_OUTPUT:
            if (event.suppress != 0) {
                StopForClient("xrdp output suppressed");
            } else if (event.connected != 0 && event.width > 0 && event.height > 0) {
                StartForClient(static_cast<uint32_t>(event.width),
                    static_cast<uint32_t>(event.height));
            }
            break;
        default:
            break;
    }
}

void CaptureController::Reset(const std::string& reason)
{
    inputAuthorizationPrimed_.store(false);
    ResetState(reason);
}

void CaptureController::StartForClient(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 || width > kMaxCaptureDimension || height > kMaxCaptureDimension ||
        callbacks_.startCapture == nullptr) {
        return;
    }

    CaptureOptions options {};
    options.width = width;
    options.height = height;
    options.frameRate = kDefaultCaptureFrameRate;
    options.showCursor = false;
    bool restartCapture = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (requested_ && width_ == width && height_ == height) {
            return;
        }
        restartCapture = requested_ && (width_ != width || height_ != height);
        const auto now = std::chrono::steady_clock::now();
        if (lastFailure_ != std::chrono::steady_clock::time_point {} &&
            now - lastFailure_ < std::chrono::seconds(3)) {
            return;
        }
        requested_ = true;
        width_ = width;
        height_ = height;
    }

    std::string geometry = callbacks_.describeGeometry != nullptr ?
        callbacks_.describeGeometry(callbacks_.userData) : "";
    if (!geometry.empty()) {
        geometry = " " + geometry;
    }
    EmitCaptureInfo("xrdp active mstsc session detected; scheduling screen capture desktop=" +
        std::to_string(width) + "x" + std::to_string(height) + geometry +
        " inputMapping=desktop-aspect-to-display" +
        (restartCapture ? " restartCapture=1" : " restartCapture=0"));

    std::thread([this, options, restartCapture]() {
        if (restartCapture && callbacks_.stopCapture != nullptr) {
            callbacks_.stopCapture("xrdp desktop size changed to " +
                std::to_string(options.width) + "x" + std::to_string(options.height),
                callbacks_.userData);
        }

        std::string message;
        if (callbacks_.startCapture != nullptr &&
            callbacks_.startCapture(options, message, callbacks_.userData)) {
            EmitCaptureInfo("xrdp client screen capture active: " + message);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            requested_ = false;
            lastFailure_ = std::chrono::steady_clock::now();
        }
        EmitCaptureError("xrdp client screen capture failed: " + message);
    }).detach();
}

void CaptureController::StopForClient(const std::string& reason)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requested_ = false;
        width_ = 0;
        height_ = 0;
    }
    if (callbacks_.stopCapture != nullptr) {
        callbacks_.stopCapture(reason, callbacks_.userData);
    }
}

void CaptureController::ResetState(const std::string& reason)
{
    StopForClient(reason);
    ResetInput(reason);
}

void CaptureController::PrimeInputAuthorization(const std::string& reason)
{
    if (!inputAuthorizationPrimed_.exchange(true) &&
        callbacks_.primeInputAuthorization != nullptr) {
        callbacks_.primeInputAuthorization(reason, callbacks_.userData);
    }
}

void CaptureController::ResetInput(const std::string& reason)
{
    if (callbacks_.resetInput != nullptr) {
        callbacks_.resetInput(reason, callbacks_.userData);
    }
}

} // namespace xrdp_ohos
