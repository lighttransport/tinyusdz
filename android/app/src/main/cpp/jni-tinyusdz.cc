#include <jni.h>
#include <android/log.h>
#include <android/bitmap.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "tinyusdz.hh"

// global
tinyusdz::Scene g_scene;

#ifndef TINYUSDZ_ANDROID_LOAD_FROM_ASSETS
#error "This demo requires to load .usd file from Android Assets"
#else
#endif




extern "C" {
    /* 
     * Returns:  0 - success
     *          -1 - failed
     */
    JNIEXPORT jint JNICALL
    Java_com_example_hellotinyusdz_MainActivity_createStream(
            JNIEnv *env,
            jobject obj,
            jobject assetManager) {

        tinyusdz::asset_manager = AAssetManager_fromJava(env, assetManager);

        tinyusdz::USDLoadOptions options;

        // load from Android asset folder
        std::string warn, err;
        bool ret = LoadUSDCFromFile("suzanne.usdc", &g_scene, &warn, &err, options);

        if (warn.size()) {
            __android_log_print(ANDROID_LOG_WARN, "tinyusdz", "USD load warning: %s", warn.c_str());
        }

        if (!ret) {
            if (err.size()) {
                __android_log_print(ANDROID_LOG_ERROR, "tinyusdz", "USD load error: %s", err.c_str());
            }
        }

        if (ret) {
            __android_log_print(ANDROID_LOG_INFO, "tinyusdz", "USD loaded. #of geom_meshes: %d", int(g_scene.geom_meshes.size()));
            return int(g_scene.geom_meshes.size());
        }

        // err
        return -1;
    }
}
