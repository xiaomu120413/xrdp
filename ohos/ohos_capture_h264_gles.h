#ifndef XRDP_OHOS_CAPTURE_H264_GLES_H
#define XRDP_OHOS_CAPTURE_H264_GLES_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <multimedia/player_framework/native_avscreen_capture_base.h>

struct OH_NativeImage;
typedef struct NativeWindow OHNativeWindow;

namespace xrdp_ohos {

class SurfaceH264GlesStage {
public:
    SurfaceH264GlesStage() = default;
    ~SurfaceH264GlesStage();

    SurfaceH264GlesStage(const SurfaceH264GlesStage&) = delete;
    SurfaceH264GlesStage& operator=(const SurfaceH264GlesStage&) = delete;

    bool Start(OHNativeWindow* encoderSurface, uint32_t width, uint32_t height,
        OHNativeWindow** captureSurface, bool (*canRender)(void*), void* canRenderUserData,
        std::string& message);
    void Stop(const std::string& reason);
    bool running() const;

private:
    static void OnFrameAvailable(void* context);

    bool InitEgl(OHNativeWindow* encoderSurface, uint32_t width, uint32_t height,
        std::string& message);
    bool InitProgram(std::string& message);
    bool InitInputSurface(uint32_t width, uint32_t height, OHNativeWindow** captureSurface,
        std::string& message);
    void RenderLoop();
    void NotifyFrameAvailable();
    bool RenderOneFrame(uint64_t frameId, bool renderToEncoder);
    void DestroyGl();
    void DestroyNativeImage();

    std::atomic<bool> running_ { false };
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    uint64_t pendingFrames_ = 0;
    uint64_t renderedFrames_ = 0;
    uint64_t skippedFrames_ = 0;
    bool (*canRender_)(void*) = nullptr;
    void* canRenderUserData_ = nullptr;

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLConfig config_ = nullptr;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface outputSurface_ = EGL_NO_SURFACE;
    GLuint oesTexture_ = 0;
    GLuint program_ = 0;
    GLint positionLoc_ = -1;
    GLint texCoordLoc_ = -1;
    GLint texMatrixLoc_ = -1;
    GLint textureLoc_ = -1;

    OH_NativeImage* nativeImage_ = nullptr;
    OHNativeWindow* inputWindow_ = nullptr;
    OHNativeWindow* encoderSurface_ = nullptr;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::thread renderThread_;
};

} // namespace xrdp_ohos

#endif // XRDP_OHOS_CAPTURE_H264_GLES_H
