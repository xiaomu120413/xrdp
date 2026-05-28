#include "ohos/ohos_capture_raw.h"

#include "ohos/ohos_capture_common.h"

#include <atomic>
#include <chrono>
#include <cstdint>

namespace xrdp_ohos {

namespace {

std::atomic<RawScreenCapture*> g_currentCapture { nullptr };

bool IsFourByteCaptureFormat(int32_t format)
{
    return format == NATIVEBUFFER_PIXEL_FMT_RGBA_8888 ||
        format == NATIVEBUFFER_PIXEL_FMT_RGBX_8888 ||
        format == NATIVEBUFFER_PIXEL_FMT_BGRA_8888 ||
        format == NATIVEBUFFER_PIXEL_FMT_BGRX_8888;
}

int32_t ResolveCaptureRowBytes(const OH_NativeBuffer_Config& config)
{
    if (config.width <= 0 || config.height <= 0) {
        return 0;
    }

    const int64_t tightRowBytes = static_cast<int64_t>(config.width) * 4;
    const int64_t stride = config.stride > 0 ? config.stride : config.width;
    if (stride >= tightRowBytes) {
        return static_cast<int32_t>(stride);
    }
    if (stride * 4 >= tightRowBytes) {
        return static_cast<int32_t>(stride * 4);
    }
    return static_cast<int32_t>(tightRowBytes);
}

OH_AVScreenCaptureConfig BuildRawScreenCaptureConfig(const CaptureOptions& options)
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

RawScreenCapture::RawScreenCapture(CaptureSubmitCallbacks callbacks)
    : callbacks_(callbacks), audioPump_(callbacks.submitAudio, callbacks.userData)
{
}

RawScreenCapture::~RawScreenCapture()
{
    Stop("destroy");
    RawScreenCapture* expected = this;
    g_currentCapture.compare_exchange_strong(expected, nullptr);
}

bool RawScreenCapture::Start(CaptureOptions options, std::string& message)
{
    options = NormalizeCaptureOptions(options);
    if (options.width == 0 || options.height == 0 ||
        options.width > kMaxCaptureDimension || options.height > kMaxCaptureDimension) {
        message = "invalid xrdp screen capture size " +
            DescribeCaptureOptions(options);
        EmitCaptureError("xrdp screen capture start failed: " + message);
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    target_ = options;
    if (running_) {
        message = "xrdp screen capture already running " + DescribeCaptureOptions(target_);
        return true;
    }

    OH_AVScreenCapture* capture = OH_AVScreenCapture_Create();
    if (capture == nullptr) {
        message = "OH_AVScreenCapture_Create returned null";
        EmitCaptureError("xrdp screen capture start failed: " + message);
        return false;
    }

    g_currentCapture.store(this);
    OH_AVScreenCaptureCallback callback {};
    callback.onError = &RawScreenCapture::OnCaptureError;
    callback.onAudioBufferAvailable = &RawScreenCapture::OnAudioBufferAvailable;
    callback.onVideoBufferAvailable = &RawScreenCapture::OnVideoBufferAvailable;
    OH_AVSCREEN_CAPTURE_ErrCode rc = OH_AVScreenCapture_SetCallback(capture, callback);
    if (rc != AV_SCREEN_CAPTURE_ERR_OK) {
        ReleaseFailedCapture(capture);
        message = "OH_AVScreenCapture_SetCallback failed: " + CaptureErrToString(rc);
        EmitCaptureError("xrdp screen capture start failed: " + message);
        return false;
    }

    OH_AVScreenCaptureConfig config = BuildRawScreenCaptureConfig(options);
    rc = OH_AVScreenCapture_Init(capture, config);
    if (rc != AV_SCREEN_CAPTURE_ERR_OK) {
        ReleaseFailedCapture(capture);
        message = "OH_AVScreenCapture_Init failed: " + CaptureErrToString(rc);
        EmitCaptureError("xrdp screen capture start failed: " + message);
        return false;
    }

    OH_AVScreenCapture_SetMicrophoneEnabled(capture, false);
    OH_AVScreenCapture_SetMaxVideoFrameRate(capture, static_cast<int32_t>(options.frameRate));
    OH_AVScreenCapture_ShowCursor(capture, options.showCursor);

    running_ = true;
    capture_ = capture;
    videoReady_ = false;
    readyCount_ = 0;
    captureErrorCount_ = 0;
    submittedCount_.store(0);
    droppedCount_.store(0);
    audioPump_.Start(capture, "raw");
    worker_ = std::thread([this]() { WorkerLoop(); });

    rc = OH_AVScreenCapture_StartScreenCapture(capture);
    if (rc != AV_SCREEN_CAPTURE_ERR_OK) {
        running_ = false;
        videoReady_ = false;
        condition_.notify_one();
        std::thread worker = std::move(worker_);
        OH_AVScreenCapture* failedCapture = capture_;
        capture_ = nullptr;
        if (worker.joinable()) {
            lock.unlock();
            worker.join();
            lock.lock();
        }
        audioPump_.Stop("raw start failed");
        ReleaseFailedCapture(failedCapture);
        message = "OH_AVScreenCapture_StartScreenCapture failed: " + CaptureErrToString(rc);
        EmitCaptureError("xrdp screen capture start failed: " + message);
        return false;
    }

    message = "xrdp raw screen capture started " + DescribeCaptureOptions(options);
    EmitCaptureInfo(message);
    return true;
}

void RawScreenCapture::Stop(const std::string& reason)
{
    OH_AVScreenCapture* capture = nullptr;
    std::thread worker;
    CaptureOptions stoppedOptions;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && capture_ == nullptr && !worker_.joinable()) {
            return;
        }
        running_ = false;
        videoReady_ = false;
        capture = capture_;
        capture_ = nullptr;
        worker = std::move(worker_);
        stoppedOptions = target_;
    }

    condition_.notify_one();
    if (worker.joinable()) {
        worker.join();
    }
    audioPump_.Stop(reason);
    if (capture != nullptr) {
        const OH_AVSCREEN_CAPTURE_ErrCode stopRc = OH_AVScreenCapture_StopScreenCapture(capture);
        const OH_AVSCREEN_CAPTURE_ErrCode releaseRc = OH_AVScreenCapture_Release(capture);
        EmitCaptureInfo("xrdp screen capture stopped after " + reason +
            " target=" + DescribeCaptureOptions(stoppedOptions) +
            " stop=" + CaptureErrToString(stopRc) +
            " release=" + CaptureErrToString(releaseRc));
    }
}

void RawScreenCapture::UpdateTarget(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0 ||
        width > kMaxCaptureDimension || height > kMaxCaptureDimension) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (target_.width == width && target_.height == height) {
        return;
    }
    target_.width = width;
    target_.height = height;
    EmitCaptureInfo("xrdp screen capture target updated for next start: " +
        std::to_string(width) + "x" + std::to_string(height));
}

CaptureDiagnostics RawScreenCapture::Snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    CaptureDiagnostics diagnostics;
    diagnostics.running = running_;
    diagnostics.width = target_.width;
    diagnostics.height = target_.height;
    diagnostics.frameRate = target_.frameRate;
    diagnostics.showCursor = target_.showCursor;
    diagnostics.readyCount = readyCount_;
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

void RawScreenCapture::ReleaseFailedCapture(OH_AVScreenCapture* capture)
{
    if (capture != nullptr) {
        OH_AVScreenCapture_Release(capture);
    }
}

void RawScreenCapture::OnCaptureError(OH_AVScreenCapture*, int32_t errorCode)
{
    RawScreenCapture* capture = g_currentCapture.load();
    if (capture != nullptr) {
        capture->HandleCaptureError(errorCode);
    }
}

void RawScreenCapture::OnAudioBufferAvailable(OH_AVScreenCapture* capture, bool isReady,
    OH_AudioCaptureSourceType type)
{
    RawScreenCapture* rawCapture = g_currentCapture.load();
    if (rawCapture != nullptr) {
        rawCapture->HandleAudioReady(capture, isReady, type);
    }
}

void RawScreenCapture::OnVideoBufferAvailable(OH_AVScreenCapture* capture, bool isReady)
{
    RawScreenCapture* rawCapture = g_currentCapture.load();
    if (rawCapture != nullptr) {
        rawCapture->HandleVideoReady(capture, isReady);
    }
}

void RawScreenCapture::HandleCaptureError(int32_t errorCode)
{
    CaptureOptions target;
    bool running = false;
    uint64_t errorCount = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target = target_;
        running = running_;
        errorCount = ++captureErrorCount_;
    }
    const auto code = static_cast<OH_AVSCREEN_CAPTURE_ErrCode>(errorCode);
    EmitCaptureError("xrdp screen capture callback error=" + CaptureErrToString(code) +
        " raw=" + std::to_string(errorCode) +
        " running=" + std::string(running ? "true" : "false") +
        " target=" + DescribeCaptureOptions(target) +
        " count=" + std::to_string(errorCount));
}

void RawScreenCapture::HandleAudioReady(OH_AVScreenCapture* capture, bool isReady,
    OH_AudioCaptureSourceType type)
{
    audioPump_.HandleAudioReady(capture, isReady, type);
}

void RawScreenCapture::HandleVideoReady(OH_AVScreenCapture* capture, bool isReady)
{
    if (!isReady) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || capture_ != capture) {
            return;
        }
        videoReady_ = true;
        ++readyCount_;
    }
    condition_.notify_one();
}

void RawScreenCapture::WorkerLoop()
{
    for (;;) {
        OH_AVScreenCapture* capture = nullptr;
        uint64_t readyCount = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return !running_ || videoReady_; });
            if (!running_) {
                return;
            }
            capture = capture_;
            videoReady_ = false;
            readyCount = readyCount_;
        }

        if (capture != nullptr) {
            ProcessOneVideoBuffer(capture, readyCount);
        }
    }
}

void RawScreenCapture::ProcessOneVideoBuffer(OH_AVScreenCapture* capture, uint64_t readyCount)
{
    int32_t fence = -1;
    int64_t timestamp = 0;
    OH_Rect region {};
    OH_NativeBuffer* buffer = OH_AVScreenCapture_AcquireVideoBuffer(capture, &fence, &timestamp, &region);
    const uint64_t captureAcquireUs = NowUs();
    if (buffer == nullptr) {
        LogSampledError("xrdp screen capture acquire video buffer returned null", readyCount);
        return;
    }

    OH_NativeBuffer_Config config {};
    OH_NativeBuffer_GetConfig(buffer, &config);

    void* mapped = nullptr;
    const int32_t mapRc = OH_NativeBuffer_Map(buffer, &mapped);
    if (mapRc != 0 || mapped == nullptr) {
        OH_AVScreenCapture_ReleaseVideoBuffer(capture);
        LogSampledError("xrdp screen capture native buffer map failed rc=" + std::to_string(mapRc),
            readyCount);
        return;
    }

    QueueMappedFrame(config, mapped, timestamp, captureAcquireUs, region, readyCount);

    OH_NativeBuffer_Unmap(buffer);
    OH_AVScreenCapture_ReleaseVideoBuffer(capture);
}

void RawScreenCapture::QueueMappedFrame(const OH_NativeBuffer_Config& config, const void* mapped,
    int64_t timestamp, uint64_t captureAcquireUs, const OH_Rect& region, uint64_t readyCount)
{
    CaptureOptions target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target = target_;
    }

    if (config.width <= 0 || config.height <= 0 ||
        static_cast<uint32_t>(config.width) > kMaxCaptureDimension ||
        static_cast<uint32_t>(config.height) > kMaxCaptureDimension ||
        !IsFourByteCaptureFormat(config.format)) {
        LogSampledError("xrdp screen capture unsupported buffer: " +
                std::to_string(config.width) + "x" + std::to_string(config.height) +
                " format=" + std::to_string(config.format),
            readyCount);
        return;
    }

    const int32_t rowBytes = ResolveCaptureRowBytes(config);
    if (rowBytes <= 0 || rowBytes < config.width * 4) {
        LogSampledError("xrdp screen capture invalid stride=" + std::to_string(config.stride) +
                " rowBytes=" + std::to_string(rowBytes),
            readyCount);
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto* source = static_cast<const uint8_t*>(mapped);
    xrdp_ohos_frame frame {};
    if (config.format == NATIVEBUFFER_PIXEL_FMT_BGRA_8888 ||
        config.format == NATIVEBUFFER_PIXEL_FMT_BGRX_8888) {
        frame.format = XRDP_OHOS_FRAME_FORMAT_BGRA_8888;
    } else {
        frame.format = XRDP_OHOS_FRAME_FORMAT_RGBA_8888;
    }
    frame.data = source;
    frame.stride = rowBytes;
    frame.width = config.width;
    frame.height = config.height;
    frame.source_sequence = readyCount;
    frame.capture_timestamp_us = timestamp > 0 ? static_cast<uint64_t>(timestamp / 1000) : 0;
    frame.capture_acquire_us = captureAcquireUs;
    frame.bridge_queue_us = NowUs();

    std::string message;
    const bool queued = callbacks_.submitVideo != nullptr &&
        callbacks_.submitVideo(frame, message, callbacks_.userData);
    const auto end = std::chrono::steady_clock::now();
    const uint32_t queueUs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count());

    if (queued) {
        const uint64_t submitted = submittedCount_.fetch_add(1) + 1;
        if (submitted <= 3 || (submitted % 60U) == 0U) {
            EmitCaptureDebug("xrdp screen capture frame queued: seq=" + std::to_string(readyCount) +
                " size=" + std::to_string(config.width) + "x" + std::to_string(config.height) +
                " target=" + DescribeCaptureOptions(target) +
                " stride=" + std::to_string(rowBytes) +
                " format=" + std::to_string(config.format) +
                " pixel=" + std::string(frame.format == XRDP_OHOS_FRAME_FORMAT_BGRA_8888 ? "bgra" : "rgba") +
                " ts=" + std::to_string(timestamp) +
                " region=(" + std::to_string(region.x) + "," + std::to_string(region.y) +
                "," + std::to_string(region.width) + "," + std::to_string(region.height) + ")" +
                " queue=" + std::to_string(queueUs / 1000.0) +
                "ms submitted=" + std::to_string(submitted));
        }
    } else if (message != "xrdp server is not running") {
        const uint64_t dropped = droppedCount_.fetch_add(1) + 1;
        if (dropped <= 3 || (dropped % 120U) == 0U) {
            EmitCaptureDebug("xrdp screen capture frame not queued: " + message +
                " dropped=" + std::to_string(dropped));
        }
    }
}

void RawScreenCapture::LogSampledError(const std::string& message, uint64_t count)
{
    if (count <= 3 || (count % 120U) == 0U) {
        EmitCaptureError(message + " count=" + std::to_string(count));
    }
}

} // namespace xrdp_ohos
