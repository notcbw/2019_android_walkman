#include <jni.h>
#include <string>
#include "UacEventListener.h"

WmUsbAudio::UacEventListener *list;

extern "C" JNIEXPORT jstring JNICALL
Java_com_notcbw_walkmanusbaudio_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_notcbw_walkmanusbaudio_WmUsbAudioService_startTestUeventCapturing(JNIEnv *env, jobject thiz) {
    list = new WmUsbAudio::UacEventListener(new WmUsbAudio::NullEventReceiver());
}
extern "C"
JNIEXPORT void JNICALL
Java_com_notcbw_walkmanusbaudio_WmUsbAudioService_endTestUeventCapturing(JNIEnv *env, jobject thiz) {
    delete list;
}