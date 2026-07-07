#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "openxr.h"
#include "openxr_platform.h"

#define LOG_TAG "AmethystVR"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
struct VrRuntimeState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLConfig config = nullptr;
    bool initialized = false;
    bool rendering = false;
    bool xrReady = false;
    void* loaderHandle = nullptr;
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;

    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
    PFN_xrCreateInstance xrCreateInstance = nullptr;
    PFN_xrDestroyInstance xrDestroyInstance = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties xrEnumerateInstanceExtensionProperties = nullptr;
    PFN_xrGetSystem xrGetSystem = nullptr;
    PFN_xrCreateSession xrCreateSession = nullptr;
    PFN_xrDestroySession xrDestroySession = nullptr;
    PFN_xrBeginSession xrBeginSession = nullptr;
    PFN_xrEndSession xrEndSession = nullptr;
};

static VrRuntimeState g_runtime;
static JavaVM* gJvm = nullptr;

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
    g_runtime.config = config;

    const EGLint pbufferAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    g_runtime.surface = eglCreatePbufferSurface(g_runtime.display, config, pbufferAttribs);
    if (g_runtime.surface == EGL_NO_SURFACE) {
        ALOGE("eglCreatePbufferSurface failed: 0x%08x", eglGetError());
        eglTerminate(g_runtime.display);
        g_runtime.display = EGL_NO_DISPLAY;
        g_runtime.config = nullptr;
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
        g_runtime.config = nullptr;
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
        g_runtime.config = nullptr;
        return false;
    }

    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* vendor = glGetString(GL_VENDOR);
    ALOGI("GLES runtime ready: version=%s vendor=%s",
          version ? reinterpret_cast<const char*>(version) : "unknown",
          vendor ? reinterpret_cast<const char*>(vendor) : "unknown");

    g_runtime.initialized = true;
    return true;
}

static bool loadOpenXRLoader() {
    if (g_runtime.loaderHandle) return true;
    g_runtime.loaderHandle = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_runtime.loaderHandle) {
        g_runtime.loaderHandle = dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_LOCAL);
    }
    if (!g_runtime.loaderHandle) {
        ALOGE("OpenXR loader not found: %s", dlerror());
        return false;
    }

    g_runtime.xrGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(dlsym(g_runtime.loaderHandle, "xrGetInstanceProcAddr"));
    g_runtime.xrCreateInstance = reinterpret_cast<PFN_xrCreateInstance>(dlsym(g_runtime.loaderHandle, "xrCreateInstance"));
    g_runtime.xrDestroyInstance = reinterpret_cast<PFN_xrDestroyInstance>(dlsym(g_runtime.loaderHandle, "xrDestroyInstance"));
    g_runtime.xrEnumerateInstanceExtensionProperties = reinterpret_cast<PFN_xrEnumerateInstanceExtensionProperties>(dlsym(g_runtime.loaderHandle, "xrEnumerateInstanceExtensionProperties"));
    g_runtime.xrGetSystem = reinterpret_cast<PFN_xrGetSystem>(dlsym(g_runtime.loaderHandle, "xrGetSystem"));
    g_runtime.xrCreateSession = reinterpret_cast<PFN_xrCreateSession>(dlsym(g_runtime.loaderHandle, "xrCreateSession"));
    g_runtime.xrDestroySession = reinterpret_cast<PFN_xrDestroySession>(dlsym(g_runtime.loaderHandle, "xrDestroySession"));
    g_runtime.xrBeginSession = reinterpret_cast<PFN_xrBeginSession>(dlsym(g_runtime.loaderHandle, "xrBeginSession"));
    g_runtime.xrEndSession = reinterpret_cast<PFN_xrEndSession>(dlsym(g_runtime.loaderHandle, "xrEndSession"));

    return g_runtime.xrGetInstanceProcAddr && g_runtime.xrCreateInstance && g_runtime.xrDestroyInstance &&
           g_runtime.xrGetSystem && g_runtime.xrCreateSession && g_runtime.xrDestroySession &&
           g_runtime.xrBeginSession && g_runtime.xrEndSession;
}

static bool ensureOpenXrSession() {
    if (g_runtime.xrReady) return true;
    if (!ensureGlesContext()) return false;
    if (!loadOpenXRLoader()) return false;

    const char* enabledExtensions[] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME };
    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.createFlags = 0;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    std::snprintf(createInfo.applicationInfo.applicationName, XR_MAX_APPLICATION_NAME_SIZE, "Amethyst");
    createInfo.applicationInfo.applicationVersion = 1;
    std::snprintf(createInfo.applicationInfo.engineName, XR_MAX_ENGINE_NAME_SIZE, "Amethyst");
    createInfo.applicationInfo.engineVersion = 1;

    XrResult result = g_runtime.xrCreateInstance(&createInfo, &g_runtime.instance);
    if (result != XR_SUCCESS) {
        ALOGE("xrCreateInstance failed: %d", static_cast<int>(result));
        return false;
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = g_runtime.xrGetSystem(g_runtime.instance, &systemInfo, &g_runtime.systemId);
    if (result != XR_SUCCESS) {
        ALOGE("xrGetSystem failed: %d", static_cast<int>(result));
        g_runtime.xrDestroyInstance(g_runtime.instance);
        g_runtime.instance = XR_NULL_HANDLE;
        return false;
    }

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = g_runtime.display;
    graphicsBinding.config = g_runtime.config;
    graphicsBinding.context = g_runtime.context;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &graphicsBinding;
    sessionInfo.systemId = g_runtime.systemId;
    result = g_runtime.xrCreateSession(g_runtime.instance, &sessionInfo, &g_runtime.session);
    if (result != XR_SUCCESS) {
        ALOGE("xrCreateSession failed: %d", static_cast<int>(result));
        g_runtime.xrDestroyInstance(g_runtime.instance);
        g_runtime.instance = XR_NULL_HANDLE;
        return false;
    }

    XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
    beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    result = g_runtime.xrBeginSession(g_runtime.session, &beginInfo);
    if (result != XR_SUCCESS) {
        ALOGE("xrBeginSession failed: %d", static_cast<int>(result));
        g_runtime.xrDestroySession(g_runtime.session);
        g_runtime.xrDestroyInstance(g_runtime.instance);
        g_runtime.session = XR_NULL_HANDLE;
        g_runtime.instance = XR_NULL_HANDLE;
        return false;
    }

    g_runtime.xrReady = true;
    ALOGI("OpenXR session created and begun successfully");
    return true;
}

static void cleanupRuntime() {
    if (g_runtime.session != XR_NULL_HANDLE) {
        g_runtime.xrEndSession(g_runtime.session);
        g_runtime.xrDestroySession(g_runtime.session);
        g_runtime.session = XR_NULL_HANDLE;
    }
    if (g_runtime.instance != XR_NULL_HANDLE) {
        g_runtime.xrDestroyInstance(g_runtime.instance);
        g_runtime.instance = XR_NULL_HANDLE;
    }
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
    if (g_runtime.loaderHandle) {
        dlclose(g_runtime.loaderHandle);
        g_runtime.loaderHandle = nullptr;
    }
    g_runtime.config = nullptr;
    g_runtime.initialized = false;
    g_runtime.rendering = false;
    g_runtime.xrReady = false;
}
}  // namespace

extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJvm = vm;
    ALOGI("JNI_OnLoad: Amethyst VR native loaded");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeInitOpenXR(JNIEnv* env, jobject /*thiz*/) {
    if (g_runtime.xrReady) return JNI_TRUE;
    if (!ensureOpenXrSession()) {
        ALOGE("OpenXR init failed; falling back to the launcher shell");
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeStartOpenXRSession(JNIEnv* env, jobject /*thiz*/) {
    if (!ensureOpenXrSession()) {
        ALOGE("nativeStartOpenXRSession failed");
        return JNI_FALSE;
    }
    g_runtime.rendering = true;
    ALOGI("nativeStartOpenXRSession: OpenXR session is active");
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeStopOpenXRSession(JNIEnv* env, jobject /*thiz*/) {
    cleanupRuntime();
    ALOGI("nativeStopOpenXRSession: cleaned up OpenXR runtime");
}
