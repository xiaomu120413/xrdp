#ifndef XRDP_OHOS_CAPTURE_H264_ENCODER_H
#define XRDP_OHOS_CAPTURE_H264_ENCODER_H

#include "ohos/ohos_capture_types.h"

#include <string>

#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_averrors.h>
#include <native_window/external_window.h>

namespace xrdp_ohos {

std::string VideoEncoderErrToString(OH_AVErrCode code);
bool CreateSurfaceH264Encoder(const CaptureOptions& options, OH_AVCodec** outCodec,
    OHNativeWindow** outSurface, std::string& message);
void RequestSurfaceH264KeyFrame(OH_AVCodec* codec, const char* reason);
void CleanupSurfaceH264Encoder(OH_AVCodec* codec, OHNativeWindow* surface, bool stopCodec);

} // namespace xrdp_ohos

#endif
