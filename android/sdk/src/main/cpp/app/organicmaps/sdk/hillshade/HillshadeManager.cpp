#include <jni.h>
#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

extern "C"
{
static void HillshadeStateChanged(HillshadeManager::HillshadeState state, std::shared_ptr<jobject> const & listener)
{
  JNIEnv * env = jni::GetEnv();
  env->CallVoidMethod(*listener, jni::GetMethodID(env, *listener, "onStateChanged", "(I)V"), static_cast<jint>(state));
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_maplayer_hillshade_HillshadeManager_nativeAddListener(JNIEnv * env,
                                                                                                      jobject thiz)
{
  CHECK(g_framework, ("Framework isn't created yet!"));
  g_framework->NativeFramework()->GetHillshadeManager().SetStateListener(
      std::bind(&HillshadeStateChanged, std::placeholders::_1, jni::make_global_ref(thiz)));
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_maplayer_hillshade_HillshadeManager_nativeSetEnabled(JNIEnv * env,
                                                                                                     jclass clazz,
                                                                                                     jboolean enabled)
{
  CHECK(g_framework, ("Framework isn't created yet!"));
  g_framework->NativeFramework()->SetHillshadeLayer(static_cast<bool>(enabled));
}

JNIEXPORT jboolean JNICALL Java_app_organicmaps_sdk_maplayer_hillshade_HillshadeManager_nativeIsEnabled(JNIEnv * env,
                                                                                                        jclass clazz)
{
  CHECK(g_framework, ("Framework isn't created yet!"));
  return static_cast<jboolean>(g_framework->NativeFramework()->GetHillshadeManager().IsEnabled());
}
}
