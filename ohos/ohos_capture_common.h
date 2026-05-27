#ifndef XRDP_OHOS_CAPTURE_COMMON_H
#define XRDP_OHOS_CAPTURE_COMMON_H

#include "ohos/ohos_capture_types.h"

#include <cstdint>
#include <string>

#include <multimedia/player_framework/native_avscreen_capture.h>

namespace xrdp_ohos {

constexpr uint32_t kMaxCaptureDimension = 8192;
constexpr uint32_t kMaxCaptureFrameRate = 60;
constexpr uint32_t kDefaultCaptureFrameRate = 60;

uint64_t NowUs();
void EmitCaptureDebug(const std::string& line);
void EmitCaptureInfo(const std::string& line);
void EmitCaptureError(const std::string& line);
std::string CaptureErrToString(OH_AVSCREEN_CAPTURE_ErrCode code);
CaptureOptions NormalizeCaptureOptions(CaptureOptions options);
std::string DescribeCaptureOptions(const CaptureOptions& options);

} // namespace xrdp_ohos

#endif
