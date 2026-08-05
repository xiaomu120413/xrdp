/**
 * HarmonyOS audio renderer used by the generic xrdp audio-input channel.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "ohos_audin_renderer.h"

#include "log.h"

#include <ohaudio/native_audiorenderer.h>
#include <ohaudio/native_audiostreambuilder.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>
#include <vector>

struct ohos_audin_renderer
{
    OH_AudioRenderer *handle = nullptr;
    std::mutex mutex;
    std::vector<uint8_t> queue;
    size_t read = 0;
    size_t write = 0;
    size_t size = 0;
    size_t frame_bytes = 0;
    bool started = false;
    bool primed = false;
    uint64_t pushed_bytes = 0;
    uint64_t dropped_bytes = 0;
    uint64_t rendered_bytes = 0;
    uint64_t underrun_bytes = 0;
};

static void
ohos_audin_renderer_clear_locked(struct ohos_audin_renderer *renderer)
{
    renderer->read = 0;
    renderer->write = 0;
    renderer->size = 0;
}

static void
ohos_audin_renderer_drop_locked(struct ohos_audin_renderer *renderer,
                                size_t bytes)
{
    if (renderer->frame_bytes > 1)
    {
        bytes -= bytes % renderer->frame_bytes;
    }
    bytes = std::min(bytes, renderer->size);
    if (bytes == 0)
    {
        return;
    }
    renderer->read = (renderer->read + bytes) % renderer->queue.size();
    renderer->size -= bytes;
    renderer->dropped_bytes += bytes;
}

static void
ohos_audin_renderer_push_locked(struct ohos_audin_renderer *renderer,
                                const uint8_t *data, size_t bytes)
{
    if (renderer->queue.empty() || data == nullptr || bytes == 0)
    {
        return;
    }
    if (renderer->frame_bytes > 1)
    {
        bytes -= bytes % renderer->frame_bytes;
    }
    if (bytes > renderer->queue.size())
    {
        const size_t keep = renderer->queue.size() -
                            renderer->queue.size() % renderer->frame_bytes;
        renderer->dropped_bytes += bytes - keep + renderer->size;
        data += bytes - keep;
        bytes = keep;
        ohos_audin_renderer_clear_locked(renderer);
    }
    else if (renderer->queue.size() - renderer->size < bytes)
    {
        ohos_audin_renderer_drop_locked(
            renderer, bytes - (renderer->queue.size() - renderer->size));
    }
    while (bytes > 0)
    {
        const size_t chunk = std::min(bytes,
                                      renderer->queue.size() - renderer->write);
        std::memcpy(renderer->queue.data() + renderer->write, data, chunk);
        renderer->write = (renderer->write + chunk) % renderer->queue.size();
        renderer->size += chunk;
        data += chunk;
        bytes -= chunk;
    }
}

static size_t
ohos_audin_renderer_pop_locked(struct ohos_audin_renderer *renderer,
                               uint8_t *data, size_t bytes)
{
    size_t copied = 0;
    while (copied < bytes && renderer->size > 0)
    {
        const size_t chunk = std::min(
            std::min(bytes - copied, renderer->size),
            renderer->queue.size() - renderer->read);
        std::memcpy(data + copied, renderer->queue.data() + renderer->read,
                    chunk);
        renderer->read = (renderer->read + chunk) % renderer->queue.size();
        renderer->size -= chunk;
        copied += chunk;
    }
    return copied;
}

static OH_AudioData_Callback_Result
ohos_audin_renderer_on_write(OH_AudioRenderer *handle, void *user_data,
                             void *audio_data, int32_t audio_data_size)
{
    auto *renderer = static_cast<struct ohos_audin_renderer *>(user_data);
    size_t copied;
    (void)handle;
    if (renderer == nullptr || audio_data == nullptr || audio_data_size <= 0)
    {
        return AUDIO_DATA_CALLBACK_RESULT_INVALID;
    }
    std::lock_guard<std::mutex> lock(renderer->mutex);
    copied = ohos_audin_renderer_pop_locked(
        renderer, static_cast<uint8_t *>(audio_data),
        static_cast<size_t>(audio_data_size));
    renderer->rendered_bytes += copied;
    if (copied < static_cast<size_t>(audio_data_size))
    {
        std::memset(static_cast<uint8_t *>(audio_data) + copied, 0,
                    static_cast<size_t>(audio_data_size) - copied);
        renderer->underrun_bytes +=
            static_cast<size_t>(audio_data_size) - copied;
    }
    return AUDIO_DATA_CALLBACK_RESULT_VALID;
}

static int32_t
ohos_audin_renderer_on_event(OH_AudioRenderer *handle, void *user_data,
                             OH_AudioStream_Event event)
{
    (void)handle;
    (void)user_data;
    LOG(LOG_LEVEL_DEBUG, "xrdp.ohos.audin: renderer event=%u",
        static_cast<unsigned int>(event));
    return 0;
}

static int32_t
ohos_audin_renderer_on_interrupt(OH_AudioRenderer *handle, void *user_data,
                                 OH_AudioInterrupt_ForceType type,
                                 OH_AudioInterrupt_Hint hint)
{
    (void)handle;
    (void)user_data;
    LOG(LOG_LEVEL_WARNING,
        "xrdp.ohos.audin: renderer interrupt type=%u hint=%u",
        static_cast<unsigned int>(type), static_cast<unsigned int>(hint));
    return 0;
}

static int32_t
ohos_audin_renderer_on_error(OH_AudioRenderer *handle, void *user_data,
                             OH_AudioStream_Result error)
{
    (void)handle;
    (void)user_data;
    LOG(LOG_LEVEL_ERROR, "xrdp.ohos.audin: renderer error=%u",
        static_cast<unsigned int>(error));
    return 0;
}

extern "C" struct ohos_audin_renderer *
ohos_audin_renderer_create(void)
{
    return new (std::nothrow) struct ohos_audin_renderer();
}

extern "C" void
ohos_audin_renderer_close(struct ohos_audin_renderer *renderer)
{
    OH_AudioRenderer *handle;
    if (renderer == nullptr)
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(renderer->mutex);
        handle = renderer->handle;
        renderer->handle = nullptr;
        renderer->started = false;
        renderer->primed = false;
    }
    if (handle != nullptr)
    {
        (void)OH_AudioRenderer_Stop(handle);
        (void)OH_AudioRenderer_Flush(handle);
        (void)OH_AudioRenderer_Release(handle);
    }
    std::lock_guard<std::mutex> lock(renderer->mutex);
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.audin: renderer closed pushed=%llu rendered=%llu dropped=%llu underrun=%llu",
        static_cast<unsigned long long>(renderer->pushed_bytes),
        static_cast<unsigned long long>(renderer->rendered_bytes),
        static_cast<unsigned long long>(renderer->dropped_bytes),
        static_cast<unsigned long long>(renderer->underrun_bytes));
    renderer->queue.clear();
    renderer->frame_bytes = 0;
    ohos_audin_renderer_clear_locked(renderer);
}

extern "C" void
ohos_audin_renderer_destroy(struct ohos_audin_renderer *renderer)
{
    if (renderer != nullptr)
    {
        ohos_audin_renderer_close(renderer);
        delete renderer;
    }
}

extern "C" int
ohos_audin_renderer_open(struct ohos_audin_renderer *renderer,
                         uint32_t rate, uint16_t channels,
                         uint16_t bits_per_sample)
{
    OH_AudioStreamBuilder *builder = nullptr;
    OH_AudioRenderer *handle = nullptr;
    OH_AudioStream_Result rc;
    OH_AudioRenderer_Callbacks callbacks = {};
    const char *stage = "create";
    size_t capacity;

    if (renderer == nullptr || rate == 0 ||
            (channels != 1 && channels != 2) || bits_per_sample != 16)
    {
        return 1;
    }
    ohos_audin_renderer_close(renderer);
    capacity = static_cast<size_t>(rate) * channels * 2U / 2U;
    capacity = std::max<size_t>(capacity, 32768U);
    {
        std::lock_guard<std::mutex> lock(renderer->mutex);
        renderer->queue.assign(capacity, 0);
        renderer->frame_bytes = static_cast<size_t>(channels) * 2U;
        renderer->pushed_bytes = 0;
        renderer->dropped_bytes = 0;
        renderer->rendered_bytes = 0;
        renderer->underrun_bytes = 0;
        ohos_audin_renderer_clear_locked(renderer);
    }

    rc = OH_AudioStreamBuilder_Create(&builder, AUDIOSTREAM_TYPE_RENDERER);
    if (rc != AUDIOSTREAM_SUCCESS)
    {
        goto fail;
    }
    stage = "sampling-rate";
    if ((rc = OH_AudioStreamBuilder_SetSamplingRate(
             builder, static_cast<int32_t>(rate))) != AUDIOSTREAM_SUCCESS)
    {
        goto fail;
    }
    stage = "channels";
    if ((rc = OH_AudioStreamBuilder_SetChannelCount(
             builder, static_cast<int32_t>(channels))) != AUDIOSTREAM_SUCCESS)
    {
        goto fail;
    }
    stage = "sample-format";
    if ((rc = OH_AudioStreamBuilder_SetSampleFormat(
             builder, AUDIOSTREAM_SAMPLE_S16LE)) != AUDIOSTREAM_SUCCESS)
    {
        goto fail;
    }
    stage = "encoding";
    if ((rc = OH_AudioStreamBuilder_SetEncodingType(
             builder, AUDIOSTREAM_ENCODING_TYPE_RAW)) != AUDIOSTREAM_SUCCESS)
    {
        goto fail;
    }
    (void)OH_AudioStreamBuilder_SetChannelLayout(
        builder, channels == 1 ? CH_LAYOUT_MONO : CH_LAYOUT_STEREO);
    (void)OH_AudioStreamBuilder_SetLatencyMode(
        builder, AUDIOSTREAM_LATENCY_MODE_FAST);
    (void)OH_AudioStreamBuilder_SetRendererInfo(builder,
                                                 AUDIOSTREAM_USAGE_MUSIC);
    (void)OH_AudioStreamBuilder_SetRendererInterruptMode(
        builder, AUDIOSTREAM_INTERRUPT_MODE_INDEPENDENT);
    callbacks.OH_AudioRenderer_OnStreamEvent = ohos_audin_renderer_on_event;
    callbacks.OH_AudioRenderer_OnInterruptEvent =
        ohos_audin_renderer_on_interrupt;
    callbacks.OH_AudioRenderer_OnError = ohos_audin_renderer_on_error;
    stage = "callbacks";
    if ((rc = OH_AudioStreamBuilder_SetRendererCallback(
             builder, callbacks, renderer)) != AUDIOSTREAM_SUCCESS ||
            (rc = OH_AudioStreamBuilder_SetRendererWriteDataCallback(
             builder, ohos_audin_renderer_on_write,
             renderer)) != AUDIOSTREAM_SUCCESS)
    {
        goto fail;
    }
    stage = "generate";
    if ((rc = OH_AudioStreamBuilder_GenerateRenderer(
             builder, &handle)) != AUDIOSTREAM_SUCCESS)
    {
        goto fail;
    }
    OH_AudioStreamBuilder_Destroy(builder);
    {
        std::lock_guard<std::mutex> lock(renderer->mutex);
        renderer->handle = handle;
    }
    LOG(LOG_LEVEL_INFO,
        "xrdp.ohos.audin: renderer ready rate=%u channels=%u bits=%u queue=%u",
        rate, channels, bits_per_sample, static_cast<unsigned int>(capacity));
    return 0;

fail:
    LOG(LOG_LEVEL_ERROR,
        "xrdp.ohos.audin: renderer open failed stage=%s result=%u",
        stage, static_cast<unsigned int>(rc));
    if (builder != nullptr)
    {
        OH_AudioStreamBuilder_Destroy(builder);
    }
    if (handle != nullptr)
    {
        (void)OH_AudioRenderer_Release(handle);
    }
    ohos_audin_renderer_close(renderer);
    return 1;
}

extern "C" int
ohos_audin_renderer_push(struct ohos_audin_renderer *renderer,
                         const void *data, size_t bytes)
{
    OH_AudioRenderer *handle = nullptr;
    bool start = false;
    if (renderer == nullptr || data == nullptr || bytes == 0)
    {
        return 1;
    }
    {
        std::lock_guard<std::mutex> lock(renderer->mutex);
        if (renderer->handle == nullptr || renderer->queue.empty())
        {
            return 1;
        }
        if (!renderer->primed)
        {
            std::vector<uint8_t> silence(
                std::min(renderer->queue.size() / 4U,
                         renderer->frame_bytes * 4096U), 0);
            ohos_audin_renderer_push_locked(renderer, silence.data(),
                                            silence.size());
            renderer->primed = true;
        }
        ohos_audin_renderer_push_locked(
            renderer, static_cast<const uint8_t *>(data), bytes);
        renderer->pushed_bytes += bytes;
        if (!renderer->started)
        {
            renderer->started = true;
            handle = renderer->handle;
            start = true;
        }
    }
    if (start && OH_AudioRenderer_Start(handle) != AUDIOSTREAM_SUCCESS)
    {
        std::lock_guard<std::mutex> lock(renderer->mutex);
        renderer->started = false;
        LOG(LOG_LEVEL_ERROR, "xrdp.ohos.audin: renderer start failed");
        return 1;
    }
    return 0;
}
