#ifndef XRDP_OHOS_FRAME_SUBMITTER_H
#define XRDP_OHOS_FRAME_SUBMITTER_H

#include "xrdp_ohos.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <condition_variable>
#include <chrono>

namespace xrdp_ohos {

using FrameSubmitFn = int (*)(const xrdp_ohos_frame*);

struct FrameSubmitterStats {
    bool running = false;
    bool hasPending = false;
    bool submitting = false;
    uint64_t queuedCount = 0;
    uint64_t replacedCount = 0;
    uint64_t submittedCount = 0;
    uint64_t failedCount = 0;
    uint64_t backoffDropCount = 0;
    uint64_t preCopyDropCount = 0;
    uint64_t bufferAllocatedCount = 0;
    uint64_t bufferReusedCount = 0;
    uint32_t lastCopyUs = 0;
    uint32_t lastSubmitUs = 0;
    int lastStatus = 0;
    size_t freeBufferCount = 0;
};

class FrameSubmitter {
public:
    bool Enqueue(const xrdp_ohos_frame& frame, FrameSubmitFn submitFn,
        std::string& message);
    void Stop(const std::string& reason);
    FrameSubmitterStats Snapshot();

private:
    static constexpr uint32_t kMaxFrameDimension = 8192;
    static constexpr size_t kMaxReusableBuffers = 2;
    using ByteBuffer = std::unique_ptr<uint8_t[]>;

    struct ReusableBuffer {
        ByteBuffer pixels;
        size_t bytes = 0;
    };

    struct PendingFrame {
        ByteBuffer pixels;
        size_t pixelBytes = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t stride = 0;
        uint32_t copyUs = 0;
        uint64_t sequence = 0;
        uint64_t sourceSequence = 0;
        uint64_t captureTimestampUs = 0;
        uint64_t captureAcquireUs = 0;
        uint64_t bridgeQueueUs = 0;
        uint64_t submitterEnqueueUs = 0;
        int format = XRDP_OHOS_FRAME_FORMAT_RGBA_8888;
        bool reusedBuffer = false;
        FrameSubmitFn submitFn = nullptr;
    };

    static ByteBuffer AllocateBytes(size_t bytes);
    ByteBuffer TakeReusableBuffer(size_t bytes, bool& reused);
    void RecycleBuffer(ByteBuffer pixels, size_t bytes);
    void RecycleBufferLocked(ByteBuffer pixels, size_t bytes);
    void NoteAllocatedBuffer();
    void StartLocked();
    void WorkerLoop();

    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    PendingFrame pending_;
    bool running_ = false;
    bool hasPending_ = false;
    bool submitting_ = false;
    uint64_t sequence_ = 0;
    uint64_t queuedCount_ = 0;
    uint64_t replacedCount_ = 0;
    uint64_t submittedCount_ = 0;
    uint64_t failedCount_ = 0;
    uint64_t backoffDropCount_ = 0;
    uint64_t preCopyDropCount_ = 0;
    uint64_t bufferAllocatedCount_ = 0;
    uint64_t bufferReusedCount_ = 0;
    uint32_t lastCopyUs_ = 0;
    uint32_t lastSubmitUs_ = 0;
    int lastStatus_ = 0;
    std::chrono::steady_clock::time_point lastStatusAt_;
    std::vector<ReusableBuffer> freeBuffers_;
};

} // namespace xrdp_ohos

#endif
