//
// Created by Bowen Cui on 2023-10-15.
//

#ifndef WALKMAN_USB_AUDIO_UACSINK_H
#define WALKMAN_USB_AUDIO_UACSINK_H

#include "3rdparty/tinyalsa/include/tinyalsa/pcm.h"

#define ALSADEV_NAME "UAC2Gadget"

namespace WmUsbAudio {

    class UacSink {
    public:
        UacSink();
        ~UacSink();

    private:
        struct pcm *pcm_handle;
    };

} // WmUsbAudio

#endif //WALKMAN_USB_AUDIO_UACSINK_H
