//
// Created by bowencui on 2023-10-22.
//

#ifndef WALKMAN_USB_AUDIO_UACSTATUSUIUPDATER_H
#define WALKMAN_USB_AUDIO_UACSTATUSUIUPDATER_H

#include <jni.h>
#include "UacEventListener.h"

namespace WmUsbAudio {

    class UacStatusUIUpdater : public UacEventReceiver {
    public:
        UacStatusUIUpdater(JNIEnv *jni_env, jobject service_obj);

        void ProcessUacEvent(const struct UacEvent *event);
    private:
        JNIEnv *jni_env;
        jobject service_obj;
        jmethodID uiupd_callback;
    };

} // WmUsbAudio

#endif //WALKMAN_USB_AUDIO_UACSTATUSUIUPDATER_H
