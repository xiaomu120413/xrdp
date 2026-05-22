#include "ohos/ohos_audio_capture_bridge.h"

#include <algorithm>

#include <hilog/log.h>

namespace xrdp_ohos {
namespace {

constexpr unsigned int kLogDomain = 0xF3D2;
constexpr const char* kLogTag = "xrdp";
constexpr size_t kMaxHilogLine = 3500;
constexpr int32_t kAudioSampleRate = 44100;
constexpr int32_t kAudioChannels = 2;
constexpr int32_t kAudioBitsPerSample = 16;
constexpr uint64_t kMaxAudioBuffersPerWake = 32;

std::string ClipHilogLine(const std::string& line)
{
    return line.size() > kMaxHilogLine ? line.substr(0, kMaxHilogLine) : line;
}

void EmitInfo(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_INFO, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
}

void EmitDebug(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_DEBUG, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
}

void EmitError(const std::string& line)
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

} // namespace

void ConfigurePlaybackAudioCapture(OH_AVScreenCaptureConfig& config)
{
    config.audioInfo.micCapInfo.audioSource = OH_SOURCE_INVALID;
    config.audioInfo.innerCapInfo.audioSampleRate = kAudioSampleRate;
    config.audioInfo.innerCapInfo.audioChannels = kAudioChannels;
    config.audioInfo.innerCapInfo.audioSource = OH_ALL_PLAYBACK;
    config.audioInfo.audioEncInfo.audioBitrate = 0;
    config.audioInfo.audioEncInfo.audioCodecformat = OH_AUDIO_DEFAULT;
}

AudioCapturePump::AudioCapturePump(AudioCaptureSubmitFn submit, void* userData)
    : submit_(submit), userData_(userData)
{
}

AudioCapturePump::~AudioCapturePump()
{
    Stop("destroy");
}

bool AudioCapturePump::Start(OH_AVScreenCapture* capture, const std::string& label)
{
    if (capture == nullptr) {
        EmitError("xrdp audio capture pump start failed: capture is null label=" + label);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) {
        return true;
    }
    capture_ = capture;
    label_ = label;
    running_ = true;
    audioReadyType_ = OH_SOURCE_INVALID;
    audioReadyCount_ = 0;
    pendingAudioReadyCount_ = 0;
    audioDroppedCount_.store(0);
    audioSubmittedCount_.store(0);
    audioBytes_.store(0);
    worker_ = std::thread([this]() { WorkerLoop(); });
    EmitInfo("xrdp audio capture pump started label=" + label_);
    return true;
}

void AudioCapturePump::Stop(const std::string& reason)
{
    std::thread worker;
    std::string label;
    uint64_t dropped = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && capture_ == nullptr && !worker_.joinable()) {
            return;
        }
        running_ = false;
        capture_ = nullptr;
        pendingAudioReadyCount_ = 0;
        audioReadyType_ = OH_SOURCE_INVALID;
        worker = std::move(worker_);
        label = label_;
        dropped = audioDroppedCount_.load();
    }

    condition_.notify_one();
    if (worker.joinable()) {
        worker.join();
    }
    EmitInfo("xrdp audio capture pump stopped after " + reason +
        " label=" + label +
        " submitted=" + std::to_string(audioSubmittedCount_.load()) +
        " dropped=" + std::to_string(dropped) +
        " bytes=" + std::to_string(audioBytes_.load()));
}

void AudioCapturePump::HandleAudioReady(OH_AVScreenCapture* capture, bool isReady,
    OH_AudioCaptureSourceType type)
{
    uint64_t audioReadyCount = 0;
    std::string label;
    if (!isReady) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || capture_ != capture) {
            return;
        }
        ++pendingAudioReadyCount_;
        audioReadyType_ = type;
        audioReadyCount = ++audioReadyCount_;
        label = label_;
    }
    if (type != OH_ALL_PLAYBACK && (audioReadyCount <= 3 || (audioReadyCount % 120U) == 0U)) {
        EmitInfo("xrdp audio capture ready type=" + std::to_string(static_cast<int>(type)) +
            " label=" + label +
            " count=" + std::to_string(audioReadyCount));
    }
    condition_.notify_one();
}

AudioCaptureStats AudioCapturePump::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    AudioCaptureStats stats;
    stats.readyCount = audioReadyCount_;
    stats.submittedCount = audioSubmittedCount_.load();
    stats.droppedCount = audioDroppedCount_.load();
    stats.bytes = audioBytes_.load();
    return stats;
}

void AudioCapturePump::WorkerLoop()
{
    for (;;) {
        OH_AVScreenCapture* capture = nullptr;
        OH_AudioCaptureSourceType audioType = OH_SOURCE_INVALID;
        uint64_t audioReadyCount = 0;
        uint64_t audioDrainCount = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() {
                return !running_ || pendingAudioReadyCount_ > 0;
            });
            if (!running_) {
                return;
            }
            capture = capture_;
            audioType = audioReadyType_;
            audioDrainCount = std::min(pendingAudioReadyCount_, kMaxAudioBuffersPerWake);
            pendingAudioReadyCount_ -= audioDrainCount;
            if (pendingAudioReadyCount_ == 0) {
                audioReadyType_ = OH_SOURCE_INVALID;
            }
            audioReadyCount = audioReadyCount_;
        }

        if (capture != nullptr && audioDrainCount > 0) {
            ProcessAudioBuffers(capture, audioType, audioReadyCount, audioDrainCount);
        }
    }
}

void AudioCapturePump::ProcessAudioBuffers(OH_AVScreenCapture* capture,
    OH_AudioCaptureSourceType type, uint64_t audioReadyCount, uint64_t drainCount)
{
    for (uint64_t i = 0; i < drainCount; ++i) {
        if (!ProcessOneAudioBuffer(capture, type, audioReadyCount)) {
            break;
        }
    }
}

bool AudioCapturePump::ProcessOneAudioBuffer(OH_AVScreenCapture* capture,
    OH_AudioCaptureSourceType type, uint64_t audioReadyCount)
{
    OH_AudioBuffer audioBufferStorage {};
    OH_AudioBuffer* audioBuffer = &audioBufferStorage;
    const OH_AVSCREEN_CAPTURE_ErrCode acquireRc =
        OH_AVScreenCapture_AcquireAudioBuffer(capture, &audioBuffer, type);
    if (acquireRc != AV_SCREEN_CAPTURE_ERR_OK) {
        const uint64_t dropped = ++audioDroppedCount_;
        LogSampledError("xrdp audio capture acquire buffer failed rc=" +
                CaptureErrToString(acquireRc) +
                " type=" + std::to_string(static_cast<int>(type)) +
                " dropped=" + std::to_string(dropped),
            audioReadyCount);
        return false;
    }

    const OH_AudioCaptureSourceType releaseType = audioBuffer != nullptr ? audioBuffer->type : type;
    if (audioBuffer == nullptr || audioBuffer->buf == nullptr || audioBuffer->size <= 0) {
        const uint64_t dropped = ++audioDroppedCount_;
        OH_AVScreenCapture_ReleaseAudioBuffer(capture, releaseType);
        LogSampledError("xrdp audio capture invalid buffer type=" +
                std::to_string(static_cast<int>(releaseType)) +
                " dropped=" + std::to_string(dropped),
            audioReadyCount);
        return false;
    }

    xrdp_ohos_audio_frame frame {};
    frame.data = audioBuffer->buf;
    frame.bytes = audioBuffer->size;
    frame.sample_rate = kAudioSampleRate;
    frame.channels = kAudioChannels;
    frame.bits_per_sample = kAudioBitsPerSample;
    frame.format = XRDP_OHOS_AUDIO_FORMAT_PCM_S16LE;
    frame.source_timestamp = audioBuffer->timestamp > 0 ?
        static_cast<uint64_t>(audioBuffer->timestamp) : 0;

    std::string message;
    const bool queued = submit_ != nullptr && submit_(frame, message, userData_);
    const OH_AVSCREEN_CAPTURE_ErrCode releaseRc =
        OH_AVScreenCapture_ReleaseAudioBuffer(capture, releaseType);
    if (releaseRc != AV_SCREEN_CAPTURE_ERR_OK) {
        LogSampledError("xrdp audio capture release buffer failed rc=" +
                CaptureErrToString(releaseRc) +
                " type=" + std::to_string(static_cast<int>(releaseType)),
            audioReadyCount);
    }

    if (queued) {
        const uint64_t submitted = audioSubmittedCount_.fetch_add(1) + 1;
        const uint64_t totalBytes = audioBytes_.fetch_add(static_cast<uint64_t>(frame.bytes)) +
            static_cast<uint64_t>(frame.bytes);
        if (submitted <= 3 || (submitted % 120U) == 0U) {
            EmitDebug("xrdp audio capture queued: seq=" +
                std::to_string(audioReadyCount) +
                " bytes=" + std::to_string(frame.bytes) +
                " type=" + std::to_string(static_cast<int>(releaseType)) +
                " ts=" + std::to_string(frame.source_timestamp) +
                " submitted=" + std::to_string(submitted) +
                " totalBytes=" + std::to_string(totalBytes));
        }
    } else if (message != "xrdp server is not running") {
        const uint64_t dropped = ++audioDroppedCount_;
        if (dropped <= 3 || (dropped % 120U) == 0U) {
            EmitDebug("xrdp audio capture not queued: " + message +
                " bytes=" + std::to_string(frame.bytes) +
                " dropped=" + std::to_string(dropped));
        }
    }
    return true;
}

void AudioCapturePump::LogSampledError(const std::string& message, uint64_t count)
{
    std::string label;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        label = label_;
    }
    if (count <= 3 || (count % 120U) == 0U) {
        EmitError(message + " count=" + std::to_string(count) +
            " label=" + label);
    }
}

} // namespace xrdp_ohos
