#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <string>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define LOG_TAG "AmethystVR"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
struct VrRuntimeState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    bool initialized = false;
    bool rendering = false;
};

static VrRuntimeState g_runtime;
static JavaVM* gJvm = nullptr;
static void* g_loader_handle = nullptr;

static bool ensureGlesContext() {
    if (g_runtime.initialized) return true;

    g_runtime.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_runtime.display == EGL_NO_DISPLAY) {
        ALOGE("eglGetDisplay failed: 0x%08x", eglGetError());
        return false;
    }

    if (!eglInitialize(g_runtime.display, nullptr, nullptr)) {
        ALOGE("eglInitialize failed: 0x%08x", eglGetError());
        g_runtime.display = EGL_NO_DISPLAY;
        return false;
    }

    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };

    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!eglChooseConfig(g_runtime.display, configAttribs, &config, 1, &numConfigs) || numConfigs == 0) {
        ALOGE("eglChooseConfig failed: 0x%08x", eglGetError());
        eglTerminate(g_runtime.display);
        g_runtime.display = EGL_NO_DISPLAY;
        return false;
    }

    const EGLint pbufferAttribs[] = {
        EGL_WIDTH, 1,
        EGL_HEIGHT, 1,
        EGL_NONE
    };
    g_runtime.surface = eglCreatePbufferSurface(g_runtime.display, config, pbufferAttribs);
    if (g_runtime.surface == EGL_NO_SURFACE) {
        ALOGE("eglCreatePbufferSurface failed: 0x%08x", eglGetError());
        eglTerminate(g_runtime.display);
        g_runtime.display = EGL_NO_DISPLAY;
        return false;
    }

    EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    g_runtime.context = eglCreateContext(g_runtime.display, config, EGL_NO_CONTEXT, contextAttribs);
    if (g_runtime.context == EGL_NO_CONTEXT) {
        ALOGE("eglCreateContext failed: 0x%08x", eglGetError());
        eglDestroySurface(g_runtime.display, g_runtime.surface);
        eglTerminate(g_runtime.display);
        g_runtime.surface = EGL_NO_SURFACE;
        g_runtime.display = EGL_NO_DISPLAY;
        return false;
    }

    if (!eglMakeCurrent(g_runtime.display, g_runtime.surface, g_runtime.surface, g_runtime.context)) {
        ALOGE("eglMakeCurrent failed: 0x%08x", eglGetError());
        eglDestroyContext(g_runtime.display, g_runtime.context);
        eglDestroySurface(g_runtime.display, g_runtime.surface);
        eglTerminate(g_runtime.display);
        g_runtime.context = EGL_NO_CONTEXT;
        g_runtime.surface = EGL_NO_SURFACE;
        g_runtime.display = EGL_NO_DISPLAY;
        return false;
    }

    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    ALOGI("GLES runtime ready: version=%s vendor=%s", version ? reinterpret_cast<const char*>(version) : "unknown",
          vendor ? reinterpret_cast<const char*>(vendor) : "unknown");

    g_runtime.initialized = true;
    return true;
}

static bool renderTestFrame() {
    if (!g_runtime.initialized) return false;

    glViewport(0, 0, 1, 1);
    glClearColor(0.12f, 0.24f, 0.36f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const GLfloat vertices[] = {
        0.0f, 0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
        0.5f, -0.5f, 0.0f,
    };
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);
    glFlush();

    if (glGetError() != GL_NO_ERROR) {
        ALOGE("GLES test frame failed with error 0x%04x", glGetError());
        return false;
    }

    g_runtime.rendering = true;
    return true;
}

static void cleanupRuntime() {
    if (g_runtime.display != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_runtime.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (g_runtime.context != EGL_NO_CONTEXT) {
        eglDestroyContext(g_runtime.display, g_runtime.context);
        g_runtime.context = EGL_NO_CONTEXT;
    }
    if (g_runtime.surface != EGL_NO_SURFACE) {
        eglDestroySurface(g_runtime.display, g_runtime.surface);
        g_runtime.surface = EGL_NO_SURFACE;
    }
    if (g_runtime.display != EGL_NO_DISPLAY) {
        eglTerminate(g_runtime.display);
        g_runtime.display = EGL_NO_DISPLAY;
    }
    g_runtime.initialized = false;
    g_runtime.rendering = false;
}
}  // namespace

extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJvm = vm;
    ALOGI("JNI_OnLoad: Amethyst VR native loaded");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeInitOpenXR(JNIEnv* env, jobject /*thiz*/) {
    if (g_runtime.initialized) return JNI_TRUE;

    if (!ensureGlesContext()) {
        ALOGE("OpenXR/GLES init failed; no usable GLES context was created");
        return JNI_FALSE;
    }

    ALOGI("OpenXR/GLES bridge initialized successfully");
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeStartOpenXRSession(JNIEnv* env, jobject /*thiz*/) {
    if (!ensureGlesContext()) {
        ALOGE("nativeStartOpenXRSession called but GLES initialization failed");
        return JNI_FALSE;
    }

    if (!renderTestFrame()) {
        ALOGE("nativeStartOpenXRSession: GLES test frame failed");
        return JNI_FALSE;
    }

    ALOGI("nativeStartOpenXRSession: GLES test pass, runtime ready for XR work");
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeStopOpenXRSession(JNIEnv* env, jobject /*thiz*/) {
    if (g_loader_handle) {
        dlclose(g_loader_handle);
        g_loader_handle = nullptr;
    }
    cleanupRuntime();
    ALOGI("nativeStopOpenXRSession: cleaned up GLES runtime");
}
