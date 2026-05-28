#include "ohos/ohos_capture_h264.h"

#include "ohos/ohos_capture_common.h"
#include "ohos/ohos_capture_h264_encoder.h"
#include "ohos/ohos_h264_payload.h"

#include <atomic>
#include <cstdint>

#include <multimedia/player_framework/native_avbuffer.h>
#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <multimedia/player_framework/native_averrors.h>
#include <multimedia/player_framework/native_avformat.h>

namespace xrdp_ohos {
namespace {

constexpr int64_t kOutputTimeoutUs = 8000;
constexpr uint64_t kInitialFrameLogCount = 5;
constexpr uint64_t kFrameLogInterval = 300;

std::atomic<SurfaceH264Capture*> g_currentCapture { nullptr };

const char* VideoSourceName(OH_VideoSourceType source)
{
    switch (source) {
        case OH_VIDEO_SOURCE_SURFACE_ES:
            return "surface-es";
        case OH_VIDEO_SOURCE_SURFACE_RGBA:
            return "surface-rgba";
        default:
            return "unknown";
    }
}

void AppendIntFormatField(OH_AVFormat* format, const char* key, const char* name,
    std::string& line)
{
    int32_t value = 0;
    if (OH_AVFormat_GetIntValue(format, key, &value)) {
        line += " ";
        line += name;
        line += "=";
        line += std::to_string(value);
    } else {
        line += " ";
        line += name;
        line += "=missing";
    }
}

OH_AVScreenCaptureConfig BuildSurfaceScreenCaptureConfig(const CaptureOptions& options)
{
    OH_AVScreenCaptureConfig config {};
    config.captureMode = OH_CAPTURE_HOME_SCREEN;
    config.dataType = OH_ORIGINAL_STREAM;
    ConfigurePlaybackAudioCapture(config);

    config.videoInfo.videoCapInfo.displayId = 0;
    config.videoInfo.videoCapInfo.missionIDs = nullptr;
    config.videoInfo.videoCapInfo.missionIDsLen = 0;
    config.videoInfo.videoCapInfo.videoFrameWidth = static_cast<int32_t>(options.width);
    config.videoInfo.videoCapInfo.videoFrameHeight = static_cast<int32_t>(options.height);
    config.videoInfo.videoCapInfo.videoSource = OH_VIDEO_SOURCE_SURFACE_RGBA;
    config.videoInfo.videoEncInfo.videoCodec = OH_VIDEO_DEFAULT;
    config.videoInfo.videoEncInfo.videoBitrate = 0;
    config.videoInfo.videoEncInfo.videoFrameRate = static_cast<int32_t>(options.frameRate);
    return config;
}

} // namespace

SurfaceH264Capture::SurfaceH264Capture(CaptureSubmitCallbacks callbacks)
    : callbacks_(callbacks), audioPump_(callbacks.submitAudio, callbacks.userData)
{
}

SurfaceH264Capture::~SurfaceH264Capture()
{
    Stop("destroy");
    SurfaceH264Capture* expected = this;
    g_currentCapture.compare_exchange_strong(expected, nullptr);
}

bool SurfaceH264Capture::Start(CaptureOptions options, std::string& message)
{
    options = NormalizeCaptureOptions(options);
    std::unique_lock<std::mutex> lock(mutex_);
    if (running_.load()) {
        message = "xrdp surface H264 capture already running " + DescribeCaptureOptions(target_);
        return true;
    }

    OH_AVCodec* codec = nullptr;
    OHNativeWindow* surface = nullptr;
    OHNativeWindow* captureSurface = nullptr;
    OH_AVScreenCapture* capture = nullptr;
    if (!CreateSurfaceH264Encoder(options, &codec, &surface, message)) {
        Cleanup(codec, surface, capture, false);
        return false;
    }
    std::string gpuMessage;
    if (!gpuStage_.Start(surface, options.width, options.height, &captureSurface,
        callbacks_.canAcceptEncodedVideo, callbacks_.userData, gpuMessage)) {
        Cleanup(codec, surface, capture, true);
        message = "xrdp surface H264 GPU converter unavailable: " + gpuMessage;
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }
    g_currentCapture.store(this);
    if (!CreateCapture(options, &capture, message)) {
        g_currentCapture.store(nullptr);
        gpuStage_.Stop("surface-h264 capture create failed");
        Cleanup(codec, surface, capture, true);
        return false;
    }

    target_ = options;
    codec_ = codec;
    inputSurface_ = surface;
    captureSurface_ = captureSurface;
    capture_ = capture;
    running_.store(true);
    outputCount_.store(0);
    submittedCount_.store(0);
    droppedCount_.store(0);
    captureErrorCount_ = 0;
    lastBackpressureKeyFrameRequestUs_ = 0;
    codecConfig_.clear();
    pendingPayload_.clear();
    audioPump_.Start(capture, "surface-h264");
    outputThread_ = std::thread([this]() { OutputLoop(); });

    const OH_AVSCREEN_CAPTURE_ErrCode startRc =
        OH_AVScreenCapture_StartScreenCaptureWithSurface(capture_, captureSurface_);
    if (startRc != AV_SCREEN_CAPTURE_ERR_OK) {
        running_.store(false);
        g_currentCapture.store(nullptr);
        gpuStage_.Stop("surface-h264 start failed");
        if (codec_ != nullptr) {
            OH_VideoEncoder_NotifyEndOfStream(codec_);
        }
        std::thread outputThread = std::move(outputThread_);
        if (outputThread.joinable()) {
            lock.unlock();
            outputThread.join();
            lock.lock();
        }
        codec_ = nullptr;
        inputSurface_ = nullptr;
        captureSurface_ = nullptr;
        capture_ = nullptr;
        audioPump_.Stop("surface-h264 start failed");
        Cleanup(codec, surface, capture, true);
        message = "OH_AVScreenCapture_StartScreenCaptureWithSurface failed: " +
            CaptureErrToString(startRc);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    RequestSurfaceH264KeyFrame(codec_, "start");
    message = "xrdp surface H264 capture started " + DescribeCaptureOptions(options) +
        " source=" + VideoSourceName(captureSource_) +
        " gpu=1 path=surface-rgba-to-recordable-encoder-full";
    EmitCaptureInfo(message);
    return true;
}

void SurfaceH264Capture::Stop(const std::string& reason)
{
    OH_AVCodec* codec = nullptr;
    OHNativeWindow* surface = nullptr;
    OH_AVScreenCapture* capture = nullptr;
    std::thread outputThread;
    CaptureOptions stoppedOptions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load() && codec_ == nullptr && capture_ == nullptr && !outputThread_.joinable()) {
            return;
        }
        running_.store(false);
        codec = codec_;
        surface = inputSurface_;
        capture = capture_;
        codec_ = nullptr;
        inputSurface_ = nullptr;
        captureSurface_ = nullptr;
        capture_ = nullptr;
        outputThread = std::move(outputThread_);
        stoppedOptions = target_;
    }

    OH_AVSCREEN_CAPTURE_ErrCode stopRc = AV_SCREEN_CAPTURE_ERR_OK;
    OH_AVSCREEN_CAPTURE_ErrCode releaseRc = AV_SCREEN_CAPTURE_ERR_OK;
    audioPump_.Stop(reason);
    if (capture != nullptr) {
        stopRc = OH_AVScreenCapture_StopScreenCapture(capture);
    }
    gpuStage_.Stop(reason);
    if (codec != nullptr) {
        OH_VideoEncoder_NotifyEndOfStream(codec);
    }
    if (outputThread.joinable()) {
        outputThread.join();
    }
    if (capture != nullptr) {
        releaseRc = OH_AVScreenCapture_Release(capture);
    }
    Cleanup(codec, surface, nullptr, true);
    EmitCaptureInfo("xrdp surface H264 capture stopped after " + reason +
        " target=" + DescribeCaptureOptions(stoppedOptions) +
        " stop=" + CaptureErrToString(stopRc) +
        " release=" + CaptureErrToString(releaseRc));
}

void SurfaceH264Capture::NotifyFlowControlOpen()
{
    gpuStage_.NotifyFlowControlOpen();
}

CaptureDiagnostics SurfaceH264Capture::Snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    CaptureDiagnostics diagnostics;
    diagnostics.running = running_.load();
    diagnostics.width = target_.width;
    diagnostics.height = target_.height;
    diagnostics.frameRate = target_.frameRate;
    diagnostics.showCursor = target_.showCursor;
    diagnostics.readyCount = outputCount_.load();
    diagnostics.submittedCount = submittedCount_.load();
    diagnostics.droppedCount = droppedCount_.load();
    diagnostics.captureErrorCount = captureErrorCount_;
    const AudioCaptureStats audioStats = audioPump_.Snapshot();
    diagnostics.audioReadyCount = audioStats.readyCount;
    diagnostics.audioSubmittedCount = audioStats.submittedCount;
    diagnostics.audioDroppedCount = audioStats.droppedCount;
    diagnostics.audioBytes = audioStats.bytes;
    return diagnostics;
}

void SurfaceH264Capture::OnCaptureError(OH_AVScreenCapture*, int32_t errorCode)
{
    SurfaceH264Capture* capture = g_currentCapture.load();
    if (capture != nullptr) {
        capture->HandleCaptureError(errorCode);
    }
}

void SurfaceH264Capture::OnAudioBufferAvailable(OH_AVScreenCapture* capture, bool isReady,
    OH_AudioCaptureSourceType type)
{
    SurfaceH264Capture* h264Capture = g_currentCapture.load();
    if (h264Capture != nullptr) {
        h264Capture->HandleAudioReady(capture, isReady, type);
    }
}

void SurfaceH264Capture::HandleCaptureError(int32_t errorCode)
{
    uint64_t count = 0;
    CaptureOptions target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target = target_;
        count = ++captureErrorCount_;
    }
    EmitCaptureError("xrdp surface H264 capture callback error=" +
        CaptureErrToString(static_cast<OH_AVSCREEN_CAPTURE_ErrCode>(errorCode)) +
        " raw=" + std::to_string(errorCode) +
        " target=" + DescribeCaptureOptions(target) +
        " count=" + std::to_string(count));
}

void SurfaceH264Capture::HandleAudioReady(OH_AVScreenCapture* capture, bool isReady,
    OH_AudioCaptureSourceType type)
{
    audioPump_.HandleAudioReady(capture, isReady, type);
}

bool SurfaceH264Capture::CreateCapture(const CaptureOptions& options,
    OH_AVScreenCapture** outCapture, std::string& message)
{
    OH_AVScreenCapture* capture = OH_AVScreenCapture_Create();
    if (capture == nullptr) {
        message = "OH_AVScreenCapture_Create returned null";
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    OH_AVScreenCaptureCallback callback {};
    callback.onError = &SurfaceH264Capture::OnCaptureError;
    callback.onAudioBufferAvailable = &SurfaceH264Capture::OnAudioBufferAvailable;
    OH_AVSCREEN_CAPTURE_ErrCode captureRc = OH_AVScreenCapture_SetCallback(capture, callback);
    if (captureRc != AV_SCREEN_CAPTURE_ERR_OK) {
        OH_AVScreenCapture_Release(capture);
        message = "OH_AVScreenCapture_SetCallback failed: " + CaptureErrToString(captureRc);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    OH_AVScreenCaptureConfig config = BuildSurfaceScreenCaptureConfig(options);
    EmitCaptureInfo("xrdp surface H264 capture init probe dataType=original source=surface-rgba" +
        std::string(" size=") + std::to_string(options.width) + "x" +
        std::to_string(options.height) +
        " fps=" + std::to_string(options.frameRate) +
        " audio=playback");
    captureRc = OH_AVScreenCapture_Init(capture, config);
    if (captureRc != AV_SCREEN_CAPTURE_ERR_OK) {
        OH_AVScreenCapture_Release(capture);
        message = "OH_AVScreenCapture_Init SURFACE_RGBA failed: " +
            CaptureErrToString(captureRc);
        EmitCaptureError("xrdp surface H264 capture start failed: " + message);
        return false;
    }

    OH_AVScreenCapture_SetMicrophoneEnabled(capture, false);
    OH_AVScreenCapture_SetMaxVideoFrameRate(capture, static_cast<int32_t>(options.frameRate));
    OH_AVScreenCapture_ShowCursor(capture, options.showCursor);
    captureSource_ = OH_VIDEO_SOURCE_SURFACE_RGBA;
    *outCapture = capture;
    EmitCaptureInfo("xrdp surface H264 capture configured source=surface-rgba path=gpu-recordable-encoder-full");
    return true;
}

void SurfaceH264Capture::OutputLoop()
{
    while (running_.load()) {
        DrainOneOutput();
    }
    for (int drain = 0; drain < 8; ++drain) {
        if (!DrainOneOutput()) {
            break;
        }
    }
}

bool SurfaceH264Capture::DrainOneOutput()
{
    OH_AVCodec* codec = nullptr;
    CaptureOptions target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        codec = codec_;
        target = target_;
    }
    if (codec == nullptr) {
        return false;
    }

    uint32_t outputIndex = 0;
    OH_AVErrCode rc = OH_VideoEncoder_QueryOutputBuffer(codec, &outputIndex, kOutputTimeoutUs);
    if (rc == AV_ERR_TRY_AGAIN_LATER) {
        return false;
    }
    if (rc == AV_ERR_STREAM_CHANGED) {
        UpdateOutputDescription(codec);
        return true;
    }
    if (rc != AV_ERR_OK) {
        const uint64_t dropped = droppedCount_.fetch_add(1) + 1;
        if (dropped <= 3 || (dropped % 120U) == 0U) {
            EmitCaptureError("xrdp surface H264 query output failed rc=" + VideoEncoderErrToString(rc) +
                " dropped=" + std::to_string(dropped));
        }
        return false;
    }

    OH_AVBuffer* output = OH_VideoEncoder_GetOutputBuffer(codec, outputIndex);
    OH_AVCodecBufferAttr attr {};
    if (output == nullptr || OH_AVBuffer_GetBufferAttr(output, &attr) != AV_ERR_OK) {
        OH_VideoEncoder_FreeOutputBuffer(codec, outputIndex);
        const uint64_t dropped = droppedCount_.fetch_add(1) + 1;
        EmitCaptureError("xrdp surface H264 output buffer invalid dropped=" + std::to_string(dropped));
        return false;
    }

    uint8_t* addr = OH_AVBuffer_GetAddr(output);
    const int capacity = OH_AVBuffer_GetCapacity(output);
    const bool payloadValid = addr != nullptr && attr.offset >= 0 && attr.size >= 0 &&
        capacity >= 0 && attr.offset + attr.size <= capacity;
    if (!payloadValid) {
        OH_VideoEncoder_FreeOutputBuffer(codec, outputIndex);
        const uint64_t dropped = droppedCount_.fetch_add(1) + 1;
        EmitCaptureError("xrdp surface H264 output payload invalid capacity=" +
            std::to_string(capacity) + " offset=" + std::to_string(attr.offset) +
            " size=" + std::to_string(attr.size) +
            " dropped=" + std::to_string(dropped));
        return false;
    }

    const uint8_t* payload = addr + attr.offset;
    const size_t payloadBytes = static_cast<size_t>(attr.size);
    if ((attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) != 0) {
        StoreCodecConfig(payload, payloadBytes);
        OH_VideoEncoder_FreeOutputBuffer(codec, outputIndex);
        return true;
    }

    const bool codecFlagSync = (attr.flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME) != 0;
    const bool incomplete = (attr.flags & AVCODEC_BUFFER_FLAGS_INCOMPLETE_FRAME) != 0;
    if (codecFlagSync) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!codecConfig_.empty() && pendingPayload_.empty()) {
            pendingPayload_.insert(pendingPayload_.end(), codecConfig_.begin(), codecConfig_.end());
        }
    }
    AppendOutputPayload(payload, payloadBytes);
    OH_VideoEncoder_FreeOutputBuffer(codec, outputIndex);
    if (incomplete) {
        return true;
    }

    std::vector<uint8_t> framePayload;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        framePayload.swap(pendingPayload_);
    }
    if (framePayload.empty()) {
        return true;
    }

    bool syncFrame = codecFlagSync || H264PayloadHasIdr(framePayload.data(), framePayload.size());
    if (syncFrame && !H264PayloadHasParameterSet(framePayload.data(), framePayload.size())) {
        std::vector<uint8_t> codecConfig;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            codecConfig = codecConfig_;
        }
        if (!codecConfig.empty()) {
            framePayload.insert(framePayload.begin(), codecConfig.begin(), codecConfig.end());
        }
    }

    SubmitEncodedFrame(target, attr, framePayload, syncFrame);
    return true;
}

void SurfaceH264Capture::UpdateOutputDescription(OH_AVCodec* codec)
{
    OH_AVFormat* description = OH_VideoEncoder_GetOutputDescription(codec);
    if (description == nullptr) {
        return;
    }
    std::string line = "xrdp surface H264 output description source=";
    line += VideoSourceName(captureSource_);
    AppendIntFormatField(description, OH_MD_KEY_RANGE_FLAG, "range", line);
    AppendIntFormatField(description, OH_MD_KEY_COLOR_PRIMARIES, "color", line);
    AppendIntFormatField(description, OH_MD_KEY_TRANSFER_CHARACTERISTICS, "transfer", line);
    AppendIntFormatField(description, OH_MD_KEY_MATRIX_COEFFICIENTS, "matrix", line);
    EmitCaptureInfo(line);

    uint8_t* codecConfig = nullptr;
    size_t codecConfigBytes = 0;
    if (OH_AVFormat_GetBuffer(description, OH_MD_KEY_CODEC_CONFIG, &codecConfig, &codecConfigBytes) &&
        codecConfig != nullptr && codecConfigBytes > 0U) {
        StoreCodecConfig(codecConfig, codecConfigBytes);
    }
    OH_AVFormat_Destroy(description);
}

void SurfaceH264Capture::StoreCodecConfig(const uint8_t* data, size_t bytes)
{
    std::vector<uint8_t> normalized;
    AppendH264Payload(normalized, data, bytes);
    const size_t normalizedBytes = normalized.size();
    const std::string originalDescription = DescribeH264ParameterSets(data, bytes);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        codecConfig_ = std::move(normalized);
    }
    std::string line = "xrdp surface H264 stored codec config bytes=" +
        std::to_string(bytes) +
        " normalized=" + std::to_string(normalizedBytes);
    if (!originalDescription.empty()) {
        line += " ";
        line += originalDescription;
    } else {
        line += " sps=missing";
    }
    EmitCaptureInfo(line);
}

void SurfaceH264Capture::AppendOutputPayload(const uint8_t* data, size_t bytes)
{
    std::vector<uint8_t> normalized;
    AppendH264Payload(normalized, data, bytes);
    if (normalized.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    pendingPayload_.insert(pendingPayload_.end(), normalized.begin(), normalized.end());
}

void SurfaceH264Capture::SubmitEncodedFrame(const CaptureOptions& target,
    const OH_AVCodecBufferAttr& attr, const std::vector<uint8_t>& payload, bool syncFrame)
{
    const uint64_t sequence = outputCount_.fetch_add(1) + 1;
    const uint64_t outputUs = NowUs();
    const uint64_t captureUs = attr.pts > 0 ? static_cast<uint64_t>(attr.pts / 1000) : outputUs;
    xrdp_ohos_encoded_frame frame {};
    frame.data = payload.data();
    frame.bytes = static_cast<int>(payload.size());
    frame.width = static_cast<int>(target.width);
    frame.height = static_cast<int>(target.height);
    frame.format = XRDP_OHOS_ENCODED_FRAME_FORMAT_H264_AVC420;
    frame.flags = syncFrame ? XRDP_OHOS_ENCODED_FRAME_FLAG_SYNC : 0U;
    frame.source_sequence = sequence;
    frame.capture_timestamp_us = captureUs;
    frame.capture_acquire_us = captureUs;
    frame.bridge_queue_us = outputUs;
    frame.encoder_output_us = outputUs;

    std::string message;
    const bool queued = callbacks_.submitEncodedVideo != nullptr &&
        callbacks_.submitEncodedVideo(frame, message, callbacks_.userData);
    if (queued) {
        const uint64_t submitted = submittedCount_.fetch_add(1) + 1;
        if (submitted <= kInitialFrameLogCount || (submitted % kFrameLogInterval) == 0U) {
            EmitCaptureDebug("xrdp surface H264 frame queued: seq=" + std::to_string(sequence) +
                " size=" + std::to_string(target.width) + "x" + std::to_string(target.height) +
                " bytes=" + std::to_string(payload.size()) +
                " flags=0x" + std::to_string(static_cast<uint32_t>(attr.flags)) +
                " sync=" + std::to_string(syncFrame ? 1 : 0) +
                " pts=" + std::to_string(attr.pts) +
                " submitted=" + std::to_string(submitted));
        }
        return;
    }

    if (message != "xrdp server is not running") {
        const uint64_t dropped = droppedCount_.fetch_add(1) + 1;
        if (message.find("backpressure") != std::string::npos ||
            message == "xrdp encoded video submit status=-6") {
            RequestKeyFrame("xrdp h264 backpressure");
        }
        if (dropped <= 5 || (dropped % 120U) == 0U) {
            EmitCaptureDebug("xrdp surface H264 frame not queued: " + message +
                " bytes=" + std::to_string(payload.size()) +
                " dropped=" + std::to_string(dropped));
        }
    }
}

void SurfaceH264Capture::RequestKeyFrame(const char* reason)
{
    OH_AVCodec* codec = nullptr;
    const std::string reasonText = reason == nullptr ? "unknown" : reason;
    const bool backpressure = reasonText.find("backpressure") != std::string::npos;
    const uint64_t nowUs = NowUs();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (backpressure && lastBackpressureKeyFrameRequestUs_ != 0 &&
            nowUs - lastBackpressureKeyFrameRequestUs_ < 500000U) {
            return;
        }
        if (backpressure) {
            lastBackpressureKeyFrameRequestUs_ = nowUs;
        }
        codec = codec_;
    }
    RequestSurfaceH264KeyFrame(codec, reason);
}

void SurfaceH264Capture::Cleanup(OH_AVCodec* codec, OHNativeWindow* surface,
    OH_AVScreenCapture* capture, bool stopCodec)
{
    if (capture != nullptr) {
        OH_AVScreenCapture_Release(capture);
    }
    CleanupSurfaceH264Encoder(codec, surface, stopCodec);
}

} // namespace xrdp_ohos
