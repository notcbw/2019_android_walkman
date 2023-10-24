//
// Created by bowencui on 2023-10-21.
//

#ifndef WALKMAN_USB_AUDIO_NULLPLAYBACKINTERFACE_H
#define WALKMAN_USB_AUDIO_NULLPLAYBACKINTERFACE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include "IPlaybackInterface.h"

#define TAG "NullPlaybackInterface"

namespace WmUsbAudio {

    class NullPlaybackInterface : public IPlaybackInterface {
    public:
        NullPlaybackInterface();
        ~NullPlaybackInterface();

        void ProcessUacEvent(const struct UacEvent *event);
        void ProcessAudio(AudioPacket packet);

    private:
        std::thread playback_thread;
        std::queue<AudioPacket> packetq;
        std::mutex mutex;
        std::condition_variable cond;
        bool stop;

        void StartPlayback();
        void StopPlayback();
        void AudioPlaybackJob();
    };

} // WmUsbAudio

#endif //WALKMAN_USB_AUDIO_NULLPLAYBACKINTERFACE_H
