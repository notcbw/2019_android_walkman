//
// Created by bowencui on 2023-10-22.
//

#include "UacStatusUIUpdater.h"

namespace WmUsbAudio {

    UacStatusUIUpdater::UacStatusUIUpdater(JNIEnv *jni_env, jobject service_obj) {
        this->jni_env = jni_env;
        this->service_obj = service_obj;

        jclass cls = jni_env->GetObjectClass(service_obj);
        this->uiupd_callback = jni_env->GetMethodID(cls, "callbackUIUpdate", "(IIII)V");
        if (this->uiupd_callback == 0)
            throw std::runtime_error("Failed to get the callback method ID.");

    }

    void UacStatusUIUpdater::ProcessUacEvent(const struct UacEvent *event) {
        /** void callbackUIUpdate(int state, int format, int freq, int bitwidth) */
        jni_env->CallVoidMethod(service_obj,
                                uiupd_callback,
                                event->state,
                                event->format,
                                event->freq,
                                event->bitwidth);
    }

} // WmUsbAudio