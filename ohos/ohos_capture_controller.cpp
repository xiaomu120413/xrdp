#include "ohos/ohos_capture_controller.h"

#include "ohos/ohos_capture_common.h"

#include <thread>

namespace xrdp_ohos {
namespace {

uint32_t ResolveCaptureFrameRate(uint32_t displayRefreshRate)
{
    if (displayRefreshRate == 0) {
        return kDefaultCaptureFrameRate;
    }
    return displayRefreshRate > kMaxCaptureFrameRate ? kMaxCaptureFrameRate :
        displayRefreshRate;
}

void ResolveContentRect(const xrdp_ohos_backend_event& event,
    uint32_t& width, uint32_t& height, uint32_t& left, uint32_t& top)
{
    width = event.width > 0 ? static_cast<uint32_t>(event.width) : 0;
    height = event.height > 0 ? static_cast<uint32_t>(event.height) : 0;
    left = 0;
    top = 0;
    if (event.right > event.left && event.bottom > event.top) {
        left = event.left > 0 ? static_cast<uint32_t>(event.left) : 0;
        top = event.top > 0 ? static_cast<uint32_t>(event.top) : 0;
        width = static_cast<uint32_t>(event.right - event.left);
        height = static_cast<uint32_t>(event.bottom - event.top);
    }
}

} // namespace

CaptureController::CaptureController(CaptureControllerCallbacks callbacks)
    : callbacks_(callbacks)
{
}

void CaptureController::HandleBackendEvent(const xrdp_ohos_backend_event& event)
{
    uint32_t contentWidth = 0;
    uint32_t contentHeight = 0;
    uint32_t contentLeft = 0;
    uint32_t contentTop = 0;

    ResolveContentRect(event, contentWidth, contentHeight, contentLeft, contentTop);
    switch (event.type) {
        case XRDP_OHOS_BACKEND_EVENT_SESSION_CONNECT:
            if (event.width > 0 && event.height > 0 &&
                contentWidth > 0 && contentHeight > 0) {
                if (callbacks_.updateTarget != nullptr) {
                    callbacks_.updateTarget(contentWidth, contentHeight,
                        callbacks_.userData);
                }
                StartForClient(contentWidth, contentHeight,
                    static_cast<uint32_t>(event.width),
                    static_cast<uint32_t>(event.height), contentLeft, contentTop);
            }
            PrimeInputAuthorization("xrdp client connected");
            break;
        case XRDP_OHOS_BACKEND_EVENT_SESSION_DISCONNECT:
            inputAuthorizationPrimed_.store(false);
            ResetState("xrdp client disconnected");
            break;
        case XRDP_OHOS_BACKEND_EVENT_MONITOR_RESIZE:
        case XRDP_OHOS_BACKEND_EVENT_MONITOR_FULL_INVALIDATE:
            if (event.connected != 0 && event.width > 0 && event.height > 0 &&
                contentWidth > 0 && contentHeight > 0) {
                if (callbacks_.updateTarget != nullptr) {
                    callbacks_.updateTarget(contentWidth, contentHeight,
                        callbacks_.userData);
                }
                StartForClient(contentWidth, contentHeight,
                    static_cast<uint32_t>(event.width),
                    static_cast<uint32_t>(event.height), contentLeft, contentTop);
            }
            break;
        case XRDP_OHOS_BACKEND_EVENT_SUPPRESS_OUTPUT:
            if (event.suppress != 0) {
                EmitCaptureInfo("xrdp output suppressed by client; keep capture running to avoid stale MSTSC video");
            } else {
                EmitCaptureInfo("xrdp output unsuppressed by client; keep current capture geometry");
            }
            break;
        case XRDP_OHOS_BACKEND_EVENT_FRAME_ACK:
            if (callbacks_.notifyFlowControlOpen != nullptr) {
                callbacks_.notifyFlowControlOpen(callbacks_.userData);
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

void CaptureController::StartForClient(uint32_t width, uint32_t height,
    uint32_t desktopWidth, uint32_t desktopHeight, uint32_t contentLeft,
    uint32_t contentTop)
{
    if (width == 0 || height == 0 || width > kMaxCaptureDimension || height > kMaxCaptureDimension ||
        callbacks_.startCapture == nullptr) {
        return;
    }

    CaptureOptions options {};
    options.width = width;
    options.height = height;
    const uint32_t displayRefreshRate = callbacks_.queryDisplayRefreshRate != nullptr ?
        callbacks_.queryDisplayRefreshRate(callbacks_.userData) : 0;
    options.frameRate = ResolveCaptureFrameRate(displayRefreshRate);
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
        std::to_string(desktopWidth) + "x" + std::to_string(desktopHeight) +
        " content=(" + std::to_string(contentLeft) + "," +
        std::to_string(contentTop) + " " + std::to_string(width) + "x" +
        std::to_string(height) + ")" + geometry +
        " fps=" + std::to_string(options.frameRate) +
        " fpsSource=" + (displayRefreshRate == 0 ? std::string("default") :
            std::string("display-refresh")) +
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
