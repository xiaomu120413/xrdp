#include "ohos/ohos_capture_common.h"

#include <chrono>

#include <hilog/log.h>

namespace xrdp_ohos {
namespace {

constexpr unsigned int kLogDomain = 0xF3D2;
constexpr const char* kLogTag = "xrdp";
constexpr size_t kMaxHilogLine = 3500;

std::string ClipHilogLine(const std::string& line)
{
    return line.size() > kMaxHilogLine ? line.substr(0, kMaxHilogLine) : line;
}

} // namespace

uint64_t NowUs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void EmitCaptureInfo(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_INFO, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
}

void EmitCaptureError(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
}

std::string CaptureErrToString(OH_AVSCREEN_CAPTURE_ErrCode code)
{
    switch (code) {
        case AV_SCREEN_CAPTURE_ERR_OK:
            return "OK";
        case AV_SCREEN_CAPTURE_ERR_NO_MEMORY:
            return "NO_MEMORY";
        case AV_SCREEN_CAPTURE_ERR_OPERATE_NOT_PERMIT:
            return "OPERATE_NOT_PERMIT";
        case AV_SCREEN_CAPTURE_ERR_INVALID_VAL:
            return "INVALID_VAL";
        case AV_SCREEN_CAPTURE_ERR_IO:
            return "IO";
        case AV_SCREEN_CAPTURE_ERR_TIMEOUT:
            return "TIMEOUT";
        case AV_SCREEN_CAPTURE_ERR_UNKNOWN:
            return "UNKNOWN";
        case AV_SCREEN_CAPTURE_ERR_SERVICE_DIED:
            return "SERVICE_DIED";
        case AV_SCREEN_CAPTURE_ERR_INVALID_STATE:
            return "INVALID_STATE";
        case AV_SCREEN_CAPTURE_ERR_UNSUPPORT:
            return "UNSUPPORT";
        default:
            return "code=" + std::to_string(static_cast<int>(code));
    }
}

CaptureOptions NormalizeCaptureOptions(CaptureOptions options)
{
    if (options.width == 0 || options.width > kMaxCaptureDimension) {
        options.width = 2560;
    }
    if (options.height == 0 || options.height > kMaxCaptureDimension) {
        options.height = 1440;
    }
    if (options.frameRate == 0 || options.frameRate > 60) {
        options.frameRate = kDefaultCaptureFrameRate;
    }
    return options;
}

std::string DescribeCaptureOptions(const CaptureOptions& options)
{
    return std::to_string(options.width) + "x" + std::to_string(options.height) +
        "@" + std::to_string(options.frameRate) +
        "fps cursor=" + (options.showCursor ? "on" : "off");
}

} // namespace xrdp_ohos
