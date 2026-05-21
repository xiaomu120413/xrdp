#include "ohos/ohos_capture_h264_encoder.h"

#include "ohos/ohos_capture_common.h"

#include <cstdint>

#include <multimedia/player_framework/native_avcapability.h>
#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <multimedia/player_framework/native_averrors.h>
#include <multimedia/player_framework/native_avformat.h>

namespace xrdp_ohos {
namespace {

constexpr int32_t kDefaultBitrate = 20000000;
constexpr int32_t kDefaultIFrameInterval = 1000;

} // namespace

std::string VideoEncoderErrToString(OH_AVErrCode code)
{
    switch (code) {
        case AV_ERR_OK:
            return "OK";
        case AV_ERR_NO_MEMORY:
            return "NO_MEMORY";
        case AV_ERR_OPERATE_NOT_PERMIT:
            return "OPERATE_NOT_PERMIT";
        case AV_ERR_INVALID_VAL:
            return "INVALID_VAL";
        case AV_ERR_IO:
            return "IO";
        case AV_ERR_TIMEOUT:
            return "TIMEOUT";
        case AV_ERR_UNKNOWN:
            return "UNKNOWN";
        case AV_ERR_SERVICE_DIED:
            return "SERVICE_DIED";
        case AV_ERR_INVALID_STATE:
            return "INVALID_STATE";
        case AV_ERR_UNSUPPORT:
            return "UNSUPPORT";
        case AV_ERR_TRY_AGAIN_LATER:
            return "TRY_AGAIN_LATER";
        case AV_ERR_STREAM_CHANGED:
            return "STREAM_CHANGED";
        default:
            return "code=" + std::to_string(static_cast<int>(code));
    }
}

namespace {

void ConfigureSurfaceH264Format(OH_AVFormat* format, const CaptureOptions& options)
{
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_SURFACE_FORMAT);
    OH_AVFormat_SetLongValue(format, OH_MD_KEY_BITRATE, kDefaultBitrate);
    OH_AVFormat_SetDoubleValue(format, OH_MD_KEY_FRAME_RATE, static_cast<double>(options.frameRate));
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE, BITRATE_MODE_CBR);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_PROFILE, AVC_PROFILE_BASELINE);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_I_FRAME_INTERVAL, kDefaultIFrameInterval);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_ENABLE_SYNC_MODE, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENABLE_LOW_LATENCY, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODER_ENABLE_B_FRAME, 0);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODER_ENABLE_PTS_BASED_RATECONTROL, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_RANGE_FLAG, 1);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_COLOR_PRIMARIES, COLOR_PRIMARY_BT709);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_TRANSFER_CHARACTERISTICS, TRANSFER_CHARACTERISTIC_BT709);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_MATRIX_COEFFICIENTS, MATRIX_COEFFICIENT_BT709);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODER_REPEAT_PREVIOUS_FRAME_AFTER,
        1000 / options.frameRate);
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_VIDEO_ENCODER_REPEAT_PREVIOUS_MAX_COUNT, 1);
}

} // namespace

bool CreateSurfaceH264Encoder(const CaptureOptions& options, OH_AVCodec** outCodec,
    OHNativeWindow** outSurface, std::string& message)
{
    OH_AVCapability* capability = OH_AVCodec_GetCapabilityByCategory(
        OH_AVCODEC_MIMETYPE_VIDEO_AVC, true, HARDWARE);
    const char* codecName = capability == nullptr ? nullptr : OH_AVCapability_GetName(capability);
    if (codecName == nullptr || codecName[0] == '\0') {
        message = "no hardware AVC encoder capability";
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    OH_AVCodec* codec = OH_VideoEncoder_CreateByName(codecName);
    if (codec == nullptr) {
        message = "OH_VideoEncoder_CreateByName failed name=" + std::string(codecName);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    OH_AVFormat* format = OH_AVFormat_CreateVideoFormat(OH_AVCODEC_MIMETYPE_VIDEO_AVC,
        static_cast<int32_t>(options.width), static_cast<int32_t>(options.height));
    if (format == nullptr) {
        OH_VideoEncoder_Destroy(codec);
        message = "OH_AVFormat_CreateVideoFormat failed";
        return false;
    }

    ConfigureSurfaceH264Format(format, options);
    OH_AVErrCode rc = OH_VideoEncoder_Configure(codec, format);
    OH_AVFormat_Destroy(format);
    if (rc != AV_ERR_OK) {
        OH_VideoEncoder_Destroy(codec);
        message = "OH_VideoEncoder_Configure surface failed: " + VideoEncoderErrToString(rc);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    OHNativeWindow* surface = nullptr;
    rc = OH_VideoEncoder_GetSurface(codec, &surface);
    if (rc != AV_ERR_OK || surface == nullptr) {
        OH_VideoEncoder_Destroy(codec);
        message = "OH_VideoEncoder_GetSurface failed: " + VideoEncoderErrToString(rc);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    rc = OH_VideoEncoder_Prepare(codec);
    if (rc != AV_ERR_OK) {
        OH_NativeWindow_DestroyNativeWindow(surface);
        OH_VideoEncoder_Destroy(codec);
        message = "OH_VideoEncoder_Prepare surface failed: " + VideoEncoderErrToString(rc);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    rc = OH_VideoEncoder_Start(codec);
    if (rc != AV_ERR_OK) {
        OH_NativeWindow_DestroyNativeWindow(surface);
        OH_VideoEncoder_Destroy(codec);
        message = "OH_VideoEncoder_Start surface failed: " + VideoEncoderErrToString(rc);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    *outCodec = codec;
    *outSurface = surface;
    EmitCaptureInfo("xrdp surface H264 encoder ready name=" + std::string(codecName) +
        " size=" + std::to_string(options.width) + "x" + std::to_string(options.height) +
        " fps=" + std::to_string(options.frameRate) +
        " bitrate=" + std::to_string(kDefaultBitrate));
    return true;
}

void RequestSurfaceH264KeyFrame(OH_AVCodec* codec, const char* reason)
{
    if (codec == nullptr) {
        return;
    }

    OH_AVFormat* format = OH_AVFormat_Create();
    if (format == nullptr) {
        return;
    }
    OH_AVFormat_SetIntValue(format, OH_MD_KEY_REQUEST_I_FRAME, 1);
    const OH_AVErrCode rc = OH_VideoEncoder_SetParameter(codec, format);
    OH_AVFormat_Destroy(format);
    if (rc != AV_ERR_OK) {
        EmitCaptureError("xrdp surface H264 request key frame failed reason=" +
            std::string(reason == nullptr ? "unknown" : reason) +
            " rc=" + VideoEncoderErrToString(rc));
        return;
    }
    EmitCaptureInfo("xrdp surface H264 requested key frame reason=" +
        std::string(reason == nullptr ? "unknown" : reason));
}

void CleanupSurfaceH264Encoder(OH_AVCodec* codec, OHNativeWindow* surface, bool stopCodec)
{
    if (codec != nullptr) {
        if (stopCodec) {
            OH_VideoEncoder_Stop(codec);
        }
        OH_VideoEncoder_Destroy(codec);
    }
    if (surface != nullptr) {
        OH_NativeWindow_DestroyNativeWindow(surface);
    }
}

} // namespace xrdp_ohos
