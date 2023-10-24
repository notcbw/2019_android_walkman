#include <jni.h>
#include <string>
#include "android_log.h"
#include "UacEventListener.h"
#include "UacSink.h"
#include "UacStatusUIUpdater.h"
#include "NullPlaybackInterface.h"

#ifdef ENABLE_IF_AAUDIO
#include "AaudioPlaybackInterface.h"
#endif

#ifdef ENABLE_IF_SPAUDIO
#include "SpAudioPlaybackInterface.h"
#endif

#define TAG "walkmanusbaudio native_lib"

#define IFTYPE_NULL     0
#define IFTYPE_AAUDIO   1
#define IFTYPE_SPAUDIO  2

extern "C" JNIEXPORT jstring JNICALL
Java_com_notcbw_walkmanusbaudio_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_notcbw_walkmanusbaudio_WmUsbAudioService_nativeStartUsbAudioPipeline(JNIEnv *env,
                                                                              jobject thiz,
                                                                              jint if_type) {
    // TODO: implement nativeStartUacAudioPipeline()
    jclass cls = env->GetObjectClass(thiz);

    // find all field id
    jfieldID fid_uevent_listener_ptr, fid_uac_sink_ptr, fid_interface_ptr, fid_ui_updater_ptr, fid_iftype;
    // see this for type signatures: https://docs.oracle.com/javase/8/docs/technotes/guides/jni/spec/types.html
    fid_uevent_listener_ptr = env->GetFieldID(cls, "mUeventListenerPtr", "J");
    if (fid_uevent_listener_ptr == 0)
        return -1;
    fid_uac_sink_ptr = env->GetFieldID(cls, "mUacSinkPtr", "J");
    if (fid_uac_sink_ptr == 0)
        return -1;
    fid_interface_ptr = env->GetFieldID(cls, "mInterfacePtr", "J");
    if (fid_interface_ptr == 0)
        return -1;
    fid_ui_updater_ptr = env->GetFieldID(cls, "mUIUpdaterPtr", "J");
    if (fid_ui_updater_ptr == 0)
        return -1;
    fid_iftype = env->GetFieldID(cls, "mCurIfType", "I");
    if (fid_iftype == 0)
        return -1;

    // instantiate objects
    WmUsbAudio::IPlaybackInterface *pb;
    switch (if_type) {
        case IFTYPE_NULL:
            pb = new WmUsbAudio::NullPlaybackInterface();
            break;
#ifdef ENABLE_IF_AAUDIO
        case IFTYPE_AAUDIO:
#endif
#ifdef ENABLE_IF_SPAUDIO
        case IFTYPE_SPAUDIO:
#endif
        default:
            LOGE("Interface type %d is not valid!", if_type);
            return 1;
    }
    WmUsbAudio::UacStatusUIUpdater *ui_upd;
    try {
        ui_upd = new WmUsbAudio::UacStatusUIUpdater(env, thiz);
    } catch (const std::exception &e) {
        LOGE("Failed to instantiate UacStatusUIUpdater: %s", e.what());
        return 3;
    }

    WmUsbAudio::UacSink *sink;
    try {
        sink = new WmUsbAudio::UacSink(pb);
    } catch (const std::exception &e) {
        LOGE("Failed to instantiate UacSink: %s", e.what());
        return 4;
    }

    WmUsbAudio::UacEventListener *lis;
    try {
        lis = new WmUsbAudio::UacEventListener();
    } catch (const std::exception &e) {
        LOGE("Failed to instantiate UacEventListener: %s", e.what());
        return 5;
    }

    // set class variables
    env->SetLongField(thiz, fid_interface_ptr, (jlong) pb);
    env->SetLongField(thiz, fid_ui_updater_ptr, (jlong) ui_upd);
    env->SetLongField(thiz, fid_uac_sink_ptr, (jlong) sink);
    env->SetLongField(thiz, fid_uevent_listener_ptr, (jlong) lis);

    // connect the pipeline up
    lis->SetReceivers({pb, sink, ui_upd});

    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_notcbw_walkmanusbaudio_WmUsbAudioService_nativeEndUsbAudioPipeline(JNIEnv *env,
                                                                            jobject thiz) {
    // TODO: implement nativeEndUacAudioPipeline()
    jclass cls = env->GetObjectClass(thiz);

    // find all field id
    jfieldID fid_uevent_listener_ptr, fid_uac_sink_ptr, fid_interface_ptr, fid_ui_updater_ptr, fid_iftype;
    // see this for type signatures: https://docs.oracle.com/javase/8/docs/technotes/guides/jni/spec/types.html
    fid_uevent_listener_ptr = env->GetFieldID(cls, "mUeventListenerPtr", "J");
    fid_uac_sink_ptr = env->GetFieldID(cls, "mUacSinkPtr", "J");
    fid_interface_ptr = env->GetFieldID(cls, "mInterfacePtr", "J");
    fid_ui_updater_ptr = env->GetFieldID(cls, "mUIUpdaterPtr", "J");
    fid_iftype = env->GetFieldID(cls, "mCurIfType", "I");

    try {
        WmUsbAudio::UacEventListener *lis = (WmUsbAudio::UacEventListener *) env->GetLongField(thiz,
                                                                                               fid_uevent_listener_ptr);
        delete lis;
    } catch (const std::exception &e) {
        LOGE("Failed to destroy UacEventListener: %s", e.what());
    }

    try {
        WmUsbAudio::UacStatusUIUpdater *ui_upd = (WmUsbAudio::UacStatusUIUpdater *) env->GetLongField(
                thiz, fid_ui_updater_ptr);
        delete ui_upd;
    } catch (const std::exception &e) {
        LOGE("Failed to destroy UacStatusUpdater: %s", e.what());
    }

    try {
        WmUsbAudio::UacSink *sink = (WmUsbAudio::UacSink *) env->GetLongField(thiz,
                                                                              fid_uac_sink_ptr);
        delete sink;
    } catch (const std::exception &e) {
        LOGE("Failed to destroy UacSink: %s", e.what());
    }

    int if_type = env->GetIntField(thiz, fid_iftype);
    switch (if_type) {
        case IFTYPE_NULL: {
            WmUsbAudio::NullPlaybackInterface *pb =
                    (WmUsbAudio::NullPlaybackInterface *) env->GetLongField(thiz,
                                                                            fid_interface_ptr);
            delete pb;
            break;
        }
#ifdef ENABLE_IF_AAUDIO
            case IFTYPE_AAUDIO: {
            }
#endif
#ifdef ENABLE_IF_SPAUDIO
            case IFTYPE_SPAUDIO: {
            }
#endif
        default:
            LOGE("Interface type %d is not valid!");
    }

    // set class variables
    env->SetLongField(thiz, fid_interface_ptr, 0L);
    env->SetLongField(thiz, fid_ui_updater_ptr, 0L);
    env->SetLongField(thiz, fid_uac_sink_ptr, 0L);
    env->SetLongField(thiz, fid_uevent_listener_ptr, 0L);
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_notcbw_walkmanusbaudio_WmUsbAudioService_nativeGetSupportedInterfaces(JNIEnv *env,
                                                                            jobject thiz) {
    // TODO: implement nativeGetUsableInterfaces()
    int rtn = 0b001;
#ifdef ENABLE_IF_AAUDIO
    rtn |= 0b010;
#endif
#ifdef ENABLE_IF_SPAUDIO
    rtn |= 0b100;
#endif

    return rtn;
}