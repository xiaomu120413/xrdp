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

bool QueryDefaultCaptureSize(uint32_t& width, uint32_t& height)
{
    xrdp_ohos_display_geometry geometry {};
    geometry.size = sizeof(geometry);
    if (xrdp_ohos_query_display_geometry(&geometry) != XRDP_OHOS_BACKEND_STATUS_OK ||
        geometry.valid == 0 || geometry.width <= 0 || geometry.height <= 0 ||
        static_cast<uint32_t>(geometry.width) > kMaxCaptureDimension ||
        static_cast<uint32_t>(geometry.height) > kMaxCaptureDimension) {
        return false;
    }

    width = static_cast<uint32_t>(geometry.width);
    height = static_cast<uint32_t>(geometry.height);
    return true;
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

void EmitCaptureDebug(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_DEBUG, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
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
    uint32_t defaultWidth = 0;
    uint32_t defaultHeight = 0;

    if (options.width == 0 || options.width > kMaxCaptureDimension) {
        if (options.contentWidth > 0 && options.contentWidth <= kMaxCaptureDimension) {
            options.width = options.contentWidth;
        } else if (QueryDefaultCaptureSize(defaultWidth, defaultHeight)) {
            options.width = defaultWidth;
        } else {
            options.width = 0;
        }
    }
    if (options.height == 0 || options.height > kMaxCaptureDimension) {
        if (options.contentHeight > 0 && options.contentHeight <= kMaxCaptureDimension) {
            options.height = options.contentHeight;
        } else if (defaultHeight > 0 ||
            QueryDefaultCaptureSize(defaultWidth, defaultHeight)) {
            options.height = defaultHeight;
            if (options.width == 0) {
                options.width = defaultWidth;
            }
        } else {
            options.height = 0;
        }
    }
    if (options.frameRate == 0 || options.frameRate > kMaxCaptureFrameRate) {
        options.frameRate = kDefaultCaptureFrameRate;
    }
    if (options.width == 0 || options.height == 0) {
        options.contentLeft = 0;
        options.contentTop = 0;
        options.contentWidth = 0;
        options.contentHeight = 0;
        return options;
    }
    if (options.contentWidth == 0 || options.contentWidth > options.width) {
        options.contentWidth = options.width;
    }
    if (options.contentHeight == 0 || options.contentHeight > options.height) {
        options.contentHeight = options.height;
    }
    if (options.contentLeft > options.width - options.contentWidth) {
        options.contentLeft = 0;
    }
    if (options.contentTop > options.height - options.contentHeight) {
        options.contentTop = 0;
    }
    return options;
}

std::string DescribeCaptureOptions(const CaptureOptions& options)
{
    return std::to_string(options.width) + "x" + std::to_string(options.height) +
        " content=(" + std::to_string(options.contentLeft) + "," +
        std::to_string(options.contentTop) + " " +
        std::to_string(options.contentWidth) + "x" +
        std::to_string(options.contentHeight) + ")" +
        "@" + std::to_string(options.frameRate) +
        "fps cursor=" + (options.showCursor ? "on" : "off");
}

} // namespace xrdp_ohos
