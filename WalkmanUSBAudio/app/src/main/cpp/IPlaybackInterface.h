//
// Created by Bowen Cui on 2023-10-15.
//

#ifndef WALKMAN_USB_AUDIO_IPLAYBACKINTERFACE_H
#define WALKMAN_USB_AUDIO_IPLAYBACKINTERFACE_H

#include <memory>
#include <tuple>
#include "UacEventListener.h"

namespace WmUsbAudio {

    typedef std::tuple<size_t, void*> AudioPacket;

    class IPlaybackInterface : public UacEventReceiver {
    public:
        virtual void ProcessUacEvent(const struct UacEvent *event) = 0;
        virtual void ProcessAudio(AudioPacket packet) = 0;
    };

} // WmUsbAudio

#endif //WALKMAN_USB_AUDIO_IPLAYBACKINTERFACE_H
