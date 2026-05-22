#include "ohos/ohos_frame_submitter.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include <hilog/log.h>

namespace xrdp_ohos {
namespace {

constexpr unsigned int kLogDomain = 0xF3D2;
constexpr const char* kLogTag = "xrdp";
constexpr size_t kMaxHilogLine = 3500;

uint64_t NowUs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string ClipHilogLine(const std::string& line)
{
    return line.size() > kMaxHilogLine ? line.substr(0, kMaxHilogLine) : line;
}

void EmitSubmitterInfo(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_INFO, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
}

void EmitSubmitterDebug(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_DEBUG, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
}

void EmitSubmitterError(const std::string& line)
{
    const std::string clipped = ClipHilogLine(line);
    OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, kLogTag, "%{public}s", clipped.c_str());
}

uint8_t ClipByte(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

void RgbToYuv709Full(int r, int g, int b, uint8_t& y, uint8_t& u, uint8_t& v)
{
    y = ClipByte((54 * r + 183 * g + 19 * b + 128) / 256);
    u = ClipByte((-29 * r - 99 * g + 128 * b) / 256 + 128);
    v = ClipByte((128 * r - 116 * g - 12 * b) / 256 + 128);
}

size_t Nv12Bytes(uint32_t width, uint32_t height)
{
    return static_cast<size_t>(width) * static_cast<size_t>(height) * 3U / 2U;
}

const char* FrameFormatName(int format)
{
    switch (format) {
        case XRDP_OHOS_FRAME_FORMAT_BGRA_8888:
            return "bgra";
        case XRDP_OHOS_FRAME_FORMAT_RGBA_8888:
            return "rgba";
        case XRDP_OHOS_FRAME_FORMAT_NV12:
            return "nv12";
        default:
            return "unknown";
    }
}

bool ConvertFourByteFrameToNv12(const xrdp_ohos_frame& frame, int32_t sourceStride,
    uint8_t* target)
{
    if (target == nullptr || frame.data == nullptr || (frame.width & 1) != 0 ||
            (frame.height & 1) != 0) {
        return false;
    }

    const bool sourceIsBgra = frame.format == XRDP_OHOS_FRAME_FORMAT_BGRA_8888;
    const auto* sourceBase = static_cast<const uint8_t*>(frame.data);
    const auto width = static_cast<uint32_t>(frame.width);
    const auto height = static_cast<uint32_t>(frame.height);
    auto* yPlane = target;
    auto* uvPlane = target + static_cast<size_t>(width) * static_cast<size_t>(height);

    for (uint32_t y = 0; y < height; y += 2U) {
        const auto* src0 = sourceBase + static_cast<size_t>(y) * static_cast<size_t>(sourceStride);
        const auto* src1 = sourceBase + static_cast<size_t>(y + 1U) * static_cast<size_t>(sourceStride);
        auto* dstY0 = yPlane + static_cast<size_t>(y) * static_cast<size_t>(width);
        auto* dstY1 = dstY0 + width;
        auto* dstUv = uvPlane + static_cast<size_t>(y / 2U) * static_cast<size_t>(width);

        for (uint32_t x = 0; x < width; x += 2U) {
            int sumU = 0;
            int sumV = 0;
            for (uint32_t dy = 0; dy < 2U; ++dy) {
                const auto* row = dy == 0U ? src0 : src1;
                auto* dstY = dy == 0U ? dstY0 : dstY1;
                for (uint32_t dx = 0; dx < 2U; ++dx) {
                    const auto* px = row + static_cast<size_t>(x + dx) * 4U;
                    const int r = sourceIsBgra ? px[2] : px[0];
                    const int g = px[1];
                    const int b = sourceIsBgra ? px[0] : px[2];
                    uint8_t yy = 0;
                    uint8_t uu = 0;
                    uint8_t vv = 0;
                    RgbToYuv709Full(r, g, b, yy, uu, vv);
                    dstY[x + dx] = yy;
                    sumU += uu;
                    sumV += vv;
                }
            }
            dstUv[x] = static_cast<uint8_t>((sumU + 2) / 4);
            dstUv[x + 1U] = static_cast<uint8_t>((sumV + 2) / 4);
        }
    }
    return true;
}

void CopyNv12Frame(const xrdp_ohos_frame& frame, int32_t sourceStride, uint8_t* target)
{
    const auto width = static_cast<uint32_t>(frame.width);
    const auto height = static_cast<uint32_t>(frame.height);
    const auto* source = static_cast<const uint8_t*>(frame.data);
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(target + static_cast<size_t>(y) * width,
            source + static_cast<size_t>(y) * static_cast<size_t>(sourceStride),
            width);
    }

    auto* targetUv = target + static_cast<size_t>(width) * static_cast<size_t>(height);
    const auto* sourceUv = source + static_cast<size_t>(sourceStride) * static_cast<size_t>(height);
    for (uint32_t y = 0; y < height / 2U; ++y) {
        std::memcpy(targetUv + static_cast<size_t>(y) * width,
            sourceUv + static_cast<size_t>(y) * static_cast<size_t>(sourceStride),
            width);
    }
}

} // namespace

bool FrameSubmitter::Enqueue(const xrdp_ohos_frame& frame, FrameSubmitFn submitFn,
    std::string& message)
{
    if (submitFn == nullptr) {
        message = "xrdp video backend is not loaded";
        return false;
    }

    const bool inputFourByte = frame.format == XRDP_OHOS_FRAME_FORMAT_BGRA_8888 ||
        frame.format == XRDP_OHOS_FRAME_FORMAT_RGBA_8888;
    const bool inputNv12 = frame.format == XRDP_OHOS_FRAME_FORMAT_NV12;
    if (!inputFourByte && !inputNv12) {
        message = "unsupported xrdp video frame format=" + std::to_string(frame.format);
        return false;
    }

    const int32_t sourceStride = frame.stride > 0 ? frame.stride :
        static_cast<int32_t>(frame.width * (inputFourByte ? 4U : 1U));
    const int32_t minStride = static_cast<int32_t>(frame.width * (inputFourByte ? 4U : 1U));
    if (frame.data == nullptr || frame.width <= 0 || frame.height <= 0 ||
            frame.width > kMaxFrameDimension || frame.height > kMaxFrameDimension ||
            sourceStride < minStride) {
        message = "invalid xrdp video frame";
        return false;
    }
    if (inputNv12 && ((frame.width & 1) != 0 || (frame.height & 1) != 0)) {
        message = "invalid xrdp nv12 frame dimensions";
        return false;
    }

    const bool useNv12 = inputNv12 ||
        (inputFourByte && (frame.width & 1) == 0 && (frame.height & 1) == 0);
    const size_t rowBytes = static_cast<size_t>(frame.width) * (useNv12 ? 1U : 4U);
    if (frame.height > std::numeric_limits<size_t>::max() / (rowBytes == 0U ? 1U : rowBytes)) {
        message = "xrdp video frame is too large";
        return false;
    }
    if (useNv12 && Nv12Bytes(frame.width, frame.height) < rowBytes * static_cast<size_t>(frame.height)) {
        message = "xrdp nv12 frame is too large";
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (lastStatus_ == XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION &&
                lastStatusAt_ != std::chrono::steady_clock::time_point {} &&
                now - lastStatusAt_ < std::chrono::milliseconds(500)) {
            const uint64_t dropped = ++backoffDropCount_;
            message = "xrdp video backoff: no active mstsc session dropped=" + std::to_string(dropped);
            return false;
        }
    }

    const size_t frameBytes = useNv12 ? Nv12Bytes(frame.width, frame.height) :
        rowBytes * static_cast<size_t>(frame.height);
    PendingFrame next;
    next.width = frame.width;
    next.height = frame.height;
    next.submitFn = submitFn;
    next.format = useNv12 ? XRDP_OHOS_FRAME_FORMAT_NV12 : frame.format;
    next.stride = useNv12 ? frame.width : static_cast<uint32_t>(frame.width * 4U);
    next.sourceSequence = frame.source_sequence;
    next.captureTimestampUs = frame.capture_timestamp_us;
    next.captureAcquireUs = frame.capture_acquire_us;
    next.bridgeQueueUs = frame.bridge_queue_us;
    next.submitterEnqueueUs = NowUs();
    next.pixelBytes = frameBytes;
    bool reusedBuffer = false;
    next.pixels = TakeReusableBuffer(frameBytes, reusedBuffer);
    if (next.pixels == nullptr) {
        next.pixels = AllocateBytes(frameBytes);
        if (next.pixels != nullptr) {
            NoteAllocatedBuffer();
        }
    }
    if (next.pixels == nullptr) {
        message = "xrdp video frame allocation failed";
        return false;
    }
    next.reusedBuffer = reusedBuffer;

    const auto copyStart = std::chrono::steady_clock::now();
    if (useNv12 && inputFourByte) {
        if (!ConvertFourByteFrameToNv12(frame, sourceStride, next.pixels.get())) {
            message = "xrdp video frame NV12 conversion failed";
            return false;
        }
    } else if (useNv12) {
        CopyNv12Frame(frame, sourceStride, next.pixels.get());
    } else {
        for (uint32_t y = 0; y < frame.height; ++y) {
            const auto* source = static_cast<const uint8_t*>(frame.data) +
                static_cast<size_t>(y) * static_cast<size_t>(sourceStride);
            auto* target = next.pixels.get() + static_cast<size_t>(y) * rowBytes;
            std::memcpy(target, source, rowBytes);
        }
    }
    const auto copyEnd = std::chrono::steady_clock::now();
    next.copyUs = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(copyEnd - copyStart).count());
    const uint32_t copyUs = next.copyUs;
    const bool copiedToReusedBuffer = next.reusedBuffer;
    const int queuedFormat = next.format;

    uint64_t queued = 0;
    uint64_t replaced = 0;
    bool replacedPending = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        StartLocked();
        next.sequence = ++sequence_;
        if (hasPending_) {
            replacedPending = true;
            ++replacedCount_;
            RecycleBufferLocked(std::move(pending_.pixels), pending_.pixelBytes);
        }
        pending_ = std::move(next);
        hasPending_ = true;
        queued = ++queuedCount_;
        replaced = replacedCount_;
    }
    condition_.notify_one();

    message = std::to_string(frame.width) + "x" + std::to_string(frame.height) +
        " xrdp-video queued mode=latest-" + FrameFormatName(queuedFormat) +
        " copy=" + std::to_string(copyUs / 1000.0) +
        "ms queued=" + std::to_string(queued) +
        " replaced=" + std::to_string(replaced) +
        " replacedPending=" + std::string(replacedPending ? "true" : "false") +
        " buffer=" + std::string(copiedToReusedBuffer ? "reused" : "new");
    return true;
}

void FrameSubmitter::Stop(const std::string& reason)
{
    std::thread worker;
    bool shouldLog = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !worker_.joinable()) {
            return;
        }
        running_ = false;
        if (hasPending_) {
            RecycleBufferLocked(std::move(pending_.pixels), pending_.pixelBytes);
        }
        hasPending_ = false;
        pending_ = PendingFrame {};
        worker = std::move(worker_);
        shouldLog = true;
    }
    condition_.notify_one();
    if (worker.joinable()) {
        worker.join();
    }
    if (shouldLog) {
        EmitSubmitterInfo("xrdp video submitter stopped after " + reason);
    }
}

FrameSubmitterStats FrameSubmitter::Snapshot()
{
    std::lock_guard<std::mutex> lock(mutex_);
    FrameSubmitterStats stats;
    stats.running = running_;
    stats.hasPending = hasPending_;
    stats.submitting = submitting_;
    stats.queuedCount = queuedCount_;
    stats.replacedCount = replacedCount_;
    stats.submittedCount = submittedCount_;
    stats.failedCount = failedCount_;
    stats.backoffDropCount = backoffDropCount_;
    stats.preCopyDropCount = preCopyDropCount_;
    stats.bufferAllocatedCount = bufferAllocatedCount_;
    stats.bufferReusedCount = bufferReusedCount_;
    stats.lastCopyUs = lastCopyUs_;
    stats.lastSubmitUs = lastSubmitUs_;
    stats.lastStatus = lastStatus_;
    stats.freeBufferCount = freeBuffers_.size();
    return stats;
}

FrameSubmitter::ByteBuffer FrameSubmitter::AllocateBytes(size_t bytes)
{
    return ByteBuffer(new (std::nothrow) uint8_t[bytes]);
}

FrameSubmitter::ByteBuffer FrameSubmitter::TakeReusableBuffer(size_t bytes, bool& reused)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto iter = freeBuffers_.begin(); iter != freeBuffers_.end(); ++iter) {
        if (iter->bytes == bytes && iter->pixels != nullptr) {
            ByteBuffer pixels = std::move(iter->pixels);
            freeBuffers_.erase(iter);
            reused = true;
            ++bufferReusedCount_;
            return pixels;
        }
    }
    reused = false;
    return ByteBuffer();
}

void FrameSubmitter::RecycleBuffer(ByteBuffer pixels, size_t bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    RecycleBufferLocked(std::move(pixels), bytes);
}

void FrameSubmitter::RecycleBufferLocked(ByteBuffer pixels, size_t bytes)
{
    if (pixels == nullptr || bytes == 0U) {
        return;
    }
    if (freeBuffers_.size() >= kMaxReusableBuffers) {
        return;
    }
    freeBuffers_.push_back(ReusableBuffer { std::move(pixels), bytes });
}

void FrameSubmitter::NoteAllocatedBuffer()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++bufferAllocatedCount_;
}

void FrameSubmitter::StartLocked()
{
    if (running_) {
        return;
    }

    running_ = true;
    worker_ = std::thread([this]() { WorkerLoop(); });
    EmitSubmitterInfo("xrdp video submitter started");
}

void FrameSubmitter::WorkerLoop()
{
    for (;;) {
        PendingFrame frame;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() { return !running_ || hasPending_; });
            if (!running_ && !hasPending_) {
                return;
            }
            frame = std::move(pending_);
            hasPending_ = false;
            submitting_ = true;
        }

        xrdp_ohos_frame submittedFrame {};
        submittedFrame.data = frame.pixels.get();
        submittedFrame.width = static_cast<int>(frame.width);
        submittedFrame.height = static_cast<int>(frame.height);
        submittedFrame.stride = static_cast<int>(frame.stride);
        submittedFrame.format = frame.format;
        submittedFrame.source_sequence = frame.sourceSequence;
        submittedFrame.capture_timestamp_us = frame.captureTimestampUs;
        submittedFrame.capture_acquire_us = frame.captureAcquireUs;
        submittedFrame.bridge_queue_us = frame.bridgeQueueUs;
        submittedFrame.submitter_enqueue_us = frame.submitterEnqueueUs;
        submittedFrame.submitter_copy_us = frame.copyUs;
        const auto submitStart = std::chrono::steady_clock::now();
        submittedFrame.submitter_submit_us = NowUs();
        const int status = frame.pixels != nullptr && frame.pixelBytes != 0U &&
            frame.submitFn != nullptr ? frame.submitFn(&submittedFrame) :
            XRDP_OHOS_BACKEND_STATUS_INVALID_FRAME;
        const auto submitEnd = std::chrono::steady_clock::now();
        const uint32_t submitUs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(submitEnd - submitStart).count());

        uint64_t submitted = 0;
        uint64_t failed = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            submitting_ = false;
            lastStatus_ = status;
            lastStatusAt_ = std::chrono::steady_clock::now();
            lastCopyUs_ = frame.copyUs;
            lastSubmitUs_ = submitUs;
            if (status == 0) {
                submitted = ++submittedCount_;
            } else {
                failed = ++failedCount_;
            }
        }

        if (status == 0) {
            if (submitted <= 3 || (submitted % 60U) == 0U) {
                EmitSubmitterDebug("xrdp video frame submitted: seq=" + std::to_string(frame.sequence) +
                    " size=" + std::to_string(frame.width) + "x" + std::to_string(frame.height) +
                    " copy=" + std::to_string(frame.copyUs / 1000.0) +
                    "ms submit=" + std::to_string(submitUs / 1000.0) +
                    "ms pixel=" + std::string(FrameFormatName(frame.format)) +
                    " buffer=" + std::string(frame.reusedBuffer ? "reused" : "new") +
                    " sourceSeq=" + std::to_string(frame.sourceSequence) +
                    " count=" + std::to_string(submitted));
            }
        } else if (status == XRDP_OHOS_BACKEND_STATUS_NO_ACTIVE_SESSION) {
            if (failed <= 3 || (failed % 120U) == 0U) {
                EmitSubmitterDebug("xrdp video frame skipped: no active mstsc session status=-4 count=" +
                    std::to_string(failed));
            }
        } else if (failed <= 3 || (failed % 120U) == 0U) {
            EmitSubmitterError("xrdp video frame submit failed: status=" + std::to_string(status) +
                " size=" + std::to_string(frame.width) + "x" + std::to_string(frame.height) +
                " count=" + std::to_string(failed));
        }
        RecycleBuffer(std::move(frame.pixels), frame.pixelBytes);
    }
}

} // namespace xrdp_ohos
