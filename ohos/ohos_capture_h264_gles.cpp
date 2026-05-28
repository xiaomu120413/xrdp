#include "ohos/ohos_capture_h264_gles.h"

#include "ohos/ohos_capture_common.h"

#include <array>
#include <cstdint>
#include <sstream>

#include <native_buffer/buffer_common.h>
#include <native_image/native_image.h>
#include <native_window/external_window.h>

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace xrdp_ohos {
namespace {

constexpr uint64_t kInitialRenderLogCount = 3;
constexpr uint64_t kRenderLogInterval = 300;

std::string Hex(uint32_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

GLuint CompileShader(GLenum type, const char* source, std::string& message)
{
    const GLuint shader = glCreateShader(type);
    if (shader == 0) {
        message = "glCreateShader failed type=" + std::to_string(type);
        return 0;
    }
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) {
        return shader;
    }

    GLint logLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::string log;
    if (logLength > 1) {
        log.resize(static_cast<size_t>(logLength));
        glGetShaderInfoLog(shader, logLength, nullptr, &log[0]);
    }
    glDeleteShader(shader);
    message = "shader compile failed type=" + std::to_string(type) + " log=" + log;
    return 0;
}

GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader, std::string& message)
{
    const GLuint program = glCreateProgram();
    if (program == 0) {
        message = "glCreateProgram failed";
        return 0;
    }
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) {
        return program;
    }

    GLint logLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::string log;
    if (logLength > 1) {
        log.resize(static_cast<size_t>(logLength));
        glGetProgramInfoLog(program, logLength, nullptr, &log[0]);
    }
    glDeleteProgram(program);
    message = "program link failed log=" + log;
    return 0;
}

bool SetWindowInt(OHNativeWindow* window, NativeWindowOperation op, int32_t value,
    const char* name, std::string& message)
{
    const int32_t rc = OH_NativeWindow_NativeWindowHandleOpt(window, op, value);
    EmitCaptureInfo("xrdp surface H264 GLES " + std::string(name) +
        "=" + std::to_string(value) + " rc=" + std::to_string(rc));
    if (rc != 0) {
        message = std::string(name) + " failed rc=" + std::to_string(rc);
        return false;
    }
    return true;
}

bool SetWindowGeometry(OHNativeWindow* window, uint32_t width, uint32_t height,
    const char* name, std::string& message)
{
    const int32_t rc = OH_NativeWindow_NativeWindowHandleOpt(window, SET_BUFFER_GEOMETRY,
        static_cast<int32_t>(width), static_cast<int32_t>(height));
    EmitCaptureInfo("xrdp surface H264 GLES " + std::string(name) +
        " geometry=" + std::to_string(width) + "x" + std::to_string(height) +
        " rc=" + std::to_string(rc));
    if (rc != 0) {
        message = std::string(name) + " geometry failed rc=" + std::to_string(rc);
        return false;
    }
    return true;
}

bool SetWindowColorSpace(OHNativeWindow* window, OH_NativeBuffer_ColorSpace colorSpace,
    const char* name, std::string& message)
{
    const int32_t rc = OH_NativeWindow_SetColorSpace(window, colorSpace);
    EmitCaptureInfo("xrdp surface H264 GLES " + std::string(name) +
        " colorspace=" + std::to_string(static_cast<int32_t>(colorSpace)) +
        " rc=" + std::to_string(rc));
    if (rc != 0) {
        message = std::string(name) + " colorspace failed rc=" + std::to_string(rc);
        return false;
    }
    return true;
}

} // namespace

SurfaceH264GlesStage::~SurfaceH264GlesStage()
{
    Stop("destroy");
}

bool SurfaceH264GlesStage::Start(OHNativeWindow* encoderSurface, uint32_t width,
    uint32_t height, OHNativeWindow** captureSurface, bool (*canRender)(void*),
    void* canRenderUserData, std::string& message)
{
    if (encoderSurface == nullptr || captureSurface == nullptr || width == 0 || height == 0) {
        message = "invalid GLES stage input";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            *captureSurface = inputWindow_;
            message = "xrdp surface H264 GLES stage already running";
            return inputWindow_ != nullptr;
        }
    }

    if (!InitEgl(encoderSurface, width, height, message) ||
        !InitProgram(message) ||
        !InitInputSurface(width, height, captureSurface, message)) {
        DestroyNativeImage();
        DestroyGl();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        encoderSurface_ = encoderSurface;
        width_ = width;
        height_ = height;
        pendingFrames_ = 0;
        renderedFrames_ = 0;
        skippedFrames_ = 0;
        flowControlWakeCount_ = 0;
        deferredRender_ = false;
        canRender_ = canRender;
        canRenderUserData_ = canRenderUserData;
        running_ = true;
    }
    renderThread_ = std::thread([this]() { RenderLoop(); });
    message = "xrdp surface H264 GLES RGBA input to recordable encoder surface started " +
        std::to_string(width) + "x" + std::to_string(height);
    EmitCaptureInfo(message);
    return true;
}

void SurfaceH264GlesStage::Stop(const std::string& reason)
{
    std::thread renderThread;
    uint64_t rendered = 0;
    uint64_t skipped = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !renderThread_.joinable() && display_ == EGL_NO_DISPLAY &&
            nativeImage_ == nullptr) {
            return;
        }
        running_ = false;
        rendered = renderedFrames_;
        skipped = skippedFrames_;
        renderThread = std::move(renderThread_);
    }
    condition_.notify_one();
    if (renderThread.joinable()) {
        renderThread.join();
    }
    DestroyNativeImage();
    DestroyGl();
    EmitCaptureInfo("xrdp surface H264 GLES stage stopped after " + reason +
        " rendered=" + std::to_string(rendered) +
        " skipped=" + std::to_string(skipped));
}

bool SurfaceH264GlesStage::running() const
{
    return running_.load();
}

void SurfaceH264GlesStage::NotifyFlowControlOpen()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || !deferredRender_) {
            return;
        }
        flowControlWakeCount_++;
    }
    condition_.notify_one();
}

void SurfaceH264GlesStage::OnFrameAvailable(void* context)
{
    if (context == nullptr) {
        return;
    }
    static_cast<SurfaceH264GlesStage*>(context)->NotifyFrameAvailable();
}

bool SurfaceH264GlesStage::InitEgl(OHNativeWindow* encoderSurface, uint32_t width,
    uint32_t height, std::string& message)
{
    if (!SetWindowGeometry(encoderSurface, width, height, "encoder-output", message) ||
        !SetWindowColorSpace(encoderSurface, OH_COLORSPACE_BT709_FULL, "encoder-output",
            message)) {
        return false;
    }

    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY) {
        message = "eglGetDisplay failed: " + Hex(static_cast<uint32_t>(eglGetError()));
        return false;
    }
    if (!eglInitialize(display_, nullptr, nullptr)) {
        message = "eglInitialize failed: " + Hex(static_cast<uint32_t>(eglGetError()));
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        message = "eglBindAPI failed: " + Hex(static_cast<uint32_t>(eglGetError()));
        return false;
    }

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLint configCount = 0;
    if (!eglChooseConfig(display_, configAttribs, &config_, 1, &configCount) ||
        configCount <= 0) {
        message = "eglChooseConfig failed: " + Hex(static_cast<uint32_t>(eglGetError()));
        return false;
    }

    EGLint nativeVisualId = 0;
    if (eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &nativeVisualId) &&
        nativeVisualId > 0) {
        if (!SetWindowInt(encoderSurface, SET_FORMAT, nativeVisualId,
            "encoder-output native-visual", message)) {
            return false;
        }
    } else {
        EmitCaptureInfo("xrdp surface H264 GLES encoder-output native-visual unavailable egl=" +
            Hex(static_cast<uint32_t>(eglGetError())));
    }

    outputSurface_ = eglCreateWindowSurface(display_, config_,
        reinterpret_cast<EGLNativeWindowType>(encoderSurface), nullptr);
    if (outputSurface_ == EGL_NO_SURFACE) {
        message = "eglCreateWindowSurface encoder failed: " +
            Hex(static_cast<uint32_t>(eglGetError()));
        return false;
    }
    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttribs);
    if (context_ == EGL_NO_CONTEXT) {
        message = "eglCreateContext failed: " + Hex(static_cast<uint32_t>(eglGetError()));
        return false;
    }
    if (!eglMakeCurrent(display_, outputSurface_, outputSurface_, context_)) {
        message = "eglMakeCurrent failed: " + Hex(static_cast<uint32_t>(eglGetError()));
        return false;
    }
    return true;
}

bool SurfaceH264GlesStage::InitProgram(std::string& message)
{
    const char* vertexShaderSource =
        "attribute vec2 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "uniform mat4 uTexMatrix;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
        "  vec4 tex = uTexMatrix * vec4(aTexCoord, 0.0, 1.0);\n"
        "  vTexCoord = tex.xy;\n"
        "}\n";
    const char* fragmentShaderSource =
        "#extension GL_OES_EGL_image_external : require\n"
        "precision mediump float;\n"
        "varying vec2 vTexCoord;\n"
        "uniform samplerExternalOES uTexture;\n"
        "void main() {\n"
        "  gl_FragColor = texture2D(uTexture, vTexCoord);\n"
        "}\n";

    const GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, vertexShaderSource, message);
    if (vertexShader == 0) {
        return false;
    }
    const GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentShaderSource, message);
    if (fragmentShader == 0) {
        glDeleteShader(vertexShader);
        return false;
    }
    program_ = LinkProgram(vertexShader, fragmentShader, message);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    if (program_ == 0) {
        return false;
    }

    positionLoc_ = glGetAttribLocation(program_, "aPosition");
    texCoordLoc_ = glGetAttribLocation(program_, "aTexCoord");
    texMatrixLoc_ = glGetUniformLocation(program_, "uTexMatrix");
    textureLoc_ = glGetUniformLocation(program_, "uTexture");
    if (positionLoc_ < 0 || texCoordLoc_ < 0 || texMatrixLoc_ < 0 || textureLoc_ < 0) {
        message = "GLES shader locations missing";
        return false;
    }
    return true;
}

bool SurfaceH264GlesStage::InitInputSurface(uint32_t width, uint32_t height,
    OHNativeWindow** captureSurface, std::string& message)
{
    glGenTextures(1, &oesTexture_);
    if (oesTexture_ == 0) {
        message = "glGenTextures failed";
        return false;
    }
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexture_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    nativeImage_ = OH_NativeImage_Create(oesTexture_, GL_TEXTURE_EXTERNAL_OES);
    if (nativeImage_ == nullptr) {
        message = "OH_NativeImage_Create failed";
        return false;
    }
    inputWindow_ = OH_NativeImage_AcquireNativeWindow(nativeImage_);
    if (inputWindow_ == nullptr) {
        message = "OH_NativeImage_AcquireNativeWindow failed";
        return false;
    }

    if (!SetWindowGeometry(inputWindow_, width, height, "capture-input", message) ||
        !SetWindowInt(inputWindow_, SET_FORMAT, static_cast<int32_t>(NATIVEBUFFER_PIXEL_FMT_RGBA_8888),
            "capture-input format-rgba", message) ||
        !SetWindowColorSpace(inputWindow_, OH_COLORSPACE_SRGB_FULL, "capture-input",
            message)) {
        return false;
    }

    OH_OnFrameAvailableListener listener {};
    listener.context = this;
    listener.onFrameAvailable = &SurfaceH264GlesStage::OnFrameAvailable;
    const int32_t listenerRc = OH_NativeImage_SetOnFrameAvailableListener(nativeImage_, listener);
    if (listenerRc != 0) {
        message = "OH_NativeImage_SetOnFrameAvailableListener failed: " +
            std::to_string(listenerRc);
        return false;
    }
    const int32_t dropRc = OH_NativeImage_SetDropBufferMode(nativeImage_, true);
    EmitCaptureInfo("xrdp surface H264 GLES capture-input drop-old-frames rc=" +
        std::to_string(dropRc));
    *captureSurface = inputWindow_;
    return true;
}

void SurfaceH264GlesStage::RenderLoop()
{
    for (;;) {
        uint64_t frameId = 0;
        bool renderToEncoder = true;
        bool updateSurface = false;
        bool deferredOnly = false;
        uint64_t skipped = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this]() {
                return !running_.load() || pendingFrames_ > 0 ||
                    (deferredRender_ && flowControlWakeCount_ > 0);
            });
            if (!running_.load()) {
                return;
            }
            updateSurface = pendingFrames_ > 0;
            deferredOnly = !updateSurface && deferredRender_;
            if (deferredOnly && flowControlWakeCount_ > 0) {
                flowControlWakeCount_--;
            }
            renderToEncoder = canRender_ == nullptr || canRender_(canRenderUserData_);
            if (!renderToEncoder) {
                skippedFrames_ += updateSurface ? pendingFrames_ : 0;
                skipped = skippedFrames_;
            }
            frameId = renderedFrames_ + 1;
            pendingFrames_ = 0;
        }
        if (!renderToEncoder) {
            if (updateSurface && RenderOneFrame(frameId, false, true)) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    deferredRender_ = true;
                }
                if (skipped <= kInitialRenderLogCount ||
                    (skipped % kRenderLogInterval) == 0U) {
                    EmitCaptureDebug("xrdp surface H264 GLES drained frame before encoder skipped=" +
                        std::to_string(skipped) +
                        " reason=xrdp-backpressure");
                }
            }
            continue;
        }
        if (RenderOneFrame(frameId, true, updateSurface)) {
            std::lock_guard<std::mutex> lock(mutex_);
            renderedFrames_++;
            if (updateSurface || deferredOnly) {
                deferredRender_ = false;
            }
        }
    }
}

void SurfaceH264GlesStage::NotifyFrameAvailable()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFrames_++;
    }
    condition_.notify_one();
}

bool SurfaceH264GlesStage::RenderOneFrame(uint64_t frameId, bool renderToEncoder, bool updateSurface)
{
    if (display_ == EGL_NO_DISPLAY || outputSurface_ == EGL_NO_SURFACE ||
        context_ == EGL_NO_CONTEXT || nativeImage_ == nullptr || program_ == 0) {
        return false;
    }
    if (!eglMakeCurrent(display_, outputSurface_, outputSurface_, context_)) {
        EmitCaptureError("xrdp surface H264 GLES render make current failed: " +
            Hex(static_cast<uint32_t>(eglGetError())));
        return false;
    }
    if (updateSurface) {
        const int32_t updateRc = OH_NativeImage_UpdateSurfaceImage(nativeImage_);
        if (updateRc != 0) {
            EmitCaptureError("xrdp surface H264 GLES update native image failed rc=" +
                std::to_string(updateRc));
            return false;
        }
    }
    if (!renderToEncoder) {
        return true;
    }

    std::array<float, 16> matrix {
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F,
    };
    OH_NativeImage_GetTransformMatrixV2(nativeImage_, matrix.data());

    const GLfloat vertices[] = {
        -1.0F, -1.0F, 0.0F, 0.0F,
         1.0F, -1.0F, 1.0F, 0.0F,
        -1.0F,  1.0F, 0.0F, 1.0F,
         1.0F,  1.0F, 1.0F, 1.0F,
    };

    glViewport(0, 0, static_cast<GLsizei>(width_), static_cast<GLsizei>(height_));
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, oesTexture_);
    glUniform1i(textureLoc_, 0);
    glUniformMatrix4fv(texMatrixLoc_, 1, GL_FALSE, matrix.data());
    glVertexAttribPointer(static_cast<GLuint>(positionLoc_), 2, GL_FLOAT, GL_FALSE,
        4 * static_cast<GLsizei>(sizeof(GLfloat)), vertices);
    glEnableVertexAttribArray(static_cast<GLuint>(positionLoc_));
    glVertexAttribPointer(static_cast<GLuint>(texCoordLoc_), 2, GL_FLOAT, GL_FALSE,
        4 * static_cast<GLsizei>(sizeof(GLfloat)), vertices + 2);
    glEnableVertexAttribArray(static_cast<GLuint>(texCoordLoc_));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(static_cast<GLuint>(positionLoc_));
    glDisableVertexAttribArray(static_cast<GLuint>(texCoordLoc_));
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        EmitCaptureError("xrdp surface H264 GLES draw failed gl=" +
            Hex(static_cast<uint32_t>(glError)));
        return false;
    }
    if (!eglSwapBuffers(display_, outputSurface_)) {
        EmitCaptureError("xrdp surface H264 GLES swap failed egl=" +
            Hex(static_cast<uint32_t>(eglGetError())));
        return false;
    }
    if (frameId <= kInitialRenderLogCount || (frameId % kRenderLogInterval) == 0U) {
        EmitCaptureDebug("xrdp surface H264 GLES rendered frame=" + std::to_string(frameId) +
            " size=" + std::to_string(width_) + "x" + std::to_string(height_) +
            " path=surface-rgba-to-recordable-encoder-full");
    }
    return true;
}

void SurfaceH264GlesStage::DestroyGl()
{
    if (display_ != EGL_NO_DISPLAY) {
        if (context_ != EGL_NO_CONTEXT && outputSurface_ != EGL_NO_SURFACE) {
            eglMakeCurrent(display_, outputSurface_, outputSurface_, context_);
        }
        if (program_ != 0) {
            glDeleteProgram(program_);
            program_ = 0;
        }
        if (oesTexture_ != 0) {
            glDeleteTextures(1, &oesTexture_);
            oesTexture_ = 0;
        }
        if (outputSurface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, outputSurface_);
            outputSurface_ = EGL_NO_SURFACE;
        }
        if (context_ != EGL_NO_CONTEXT) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display_, context_);
            context_ = EGL_NO_CONTEXT;
        }
        eglTerminate(display_);
        display_ = EGL_NO_DISPLAY;
    }
    config_ = nullptr;
    positionLoc_ = -1;
    texCoordLoc_ = -1;
    texMatrixLoc_ = -1;
    textureLoc_ = -1;
    encoderSurface_ = nullptr;
    width_ = 0;
    height_ = 0;
}

void SurfaceH264GlesStage::DestroyNativeImage()
{
    if (nativeImage_ != nullptr) {
        OH_NativeImage_UnsetOnFrameAvailableListener(nativeImage_);
        OH_NativeImage_Destroy(&nativeImage_);
    }
    inputWindow_ = nullptr;
}

} // namespace xrdp_ohos
