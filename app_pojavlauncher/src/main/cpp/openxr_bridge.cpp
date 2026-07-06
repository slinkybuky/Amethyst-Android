#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <string>

#define LOG_TAG "AmethystVR"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* gJvm = nullptr;
static bool g_openxr_loader_found = false;
static void* g_loader_handle = nullptr;

extern "C" jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    gJvm = vm;
    ALOGI("JNI_OnLoad: Amethyst VR native loaded");
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeInitOpenXR(JNIEnv* env, jobject /*thiz*/) {
    if (g_openxr_loader_found) return JNI_TRUE;
    // Try to open the platform loader. On Quest, libopenxr_loader.so is usually available.
    g_loader_handle = dlopen("libopenxr_loader.so", RTLD_NOW | RTLD_LOCAL);
    if (!g_loader_handle) {
        // Try the loader name that some systems use
        g_loader_handle = dlopen("libopenxr_loader.so.1", RTLD_NOW | RTLD_LOCAL);
    }
    if (!g_loader_handle) {
        ALOGE("OpenXR loader not found: %s", dlerror());
        g_openxr_loader_found = false;
        return JNI_FALSE;
    }
    ALOGI("OpenXR loader found and opened");
    g_openxr_loader_found = true;
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeStartOpenXRSession(JNIEnv* env, jobject /*thiz*/) {
    if (!g_openxr_loader_found) {
        ALOGE("nativeStartOpenXRSession called but loader not available");
        return JNI_FALSE;
    }
    // Placeholder: a robust implementation would query runtime, create instance, system, session, swapchains, and start render loop.
    ALOGI("nativeStartOpenXRSession: placeholder success (no session created yet)");
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_net_kdt_pojavlaunch_VrLauncherActivity_nativeStopOpenXRSession(JNIEnv* env, jobject /*thiz*/) {
    if (g_loader_handle) {
        dlclose(g_loader_handle);
        g_loader_handle = nullptr;
    }
    g_openxr_loader_found = false;
    ALOGI("nativeStopOpenXRSession: cleaned up");
}
