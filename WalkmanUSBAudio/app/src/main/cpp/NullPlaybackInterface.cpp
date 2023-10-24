//
// Created by bowencui on 2023-10-21.
//

#include "NullPlaybackInterface.h"
#include "android_log.h"
#include "IPlaybackInterface.h"

namespace WmUsbAudio {

    NullPlaybackInterface::NullPlaybackInterface() {
        //StartPlayback();
        stop = true;
        LOGI("NullPlaybackInterface: started");
    }
    NullPlaybackInterface::~NullPlaybackInterface() {
        StopPlayback();
        LOGI("NullPlaybackInterface: deleted");
    }

    void NullPlaybackInterface::ProcessUacEvent(const struct UacEvent *event) {
        LOGI("NullPlaybackInterface: received event, parameter is updated");
        switch (event->state) {
            case UacEventState::PLAY:
                if (!stop)
                    StopPlayback();
                StartPlayback();
                LOGI("NullPlaybackInterface: resume");
                break;
            default:
                StopPlayback();
                LOGI("NullPlaybackInterface: stop");
        }
    }

    void NullPlaybackInterface::ProcessAudio(AudioPacket packet) {
        std::unique_lock<std::mutex> lock(mutex);
        packetq.push(packet);
        cond.notify_one();
    }

    void NullPlaybackInterface::StartPlayback() {
        stop = false;
        playback_thread = std::thread(&NullPlaybackInterface::AudioPlaybackJob, this);
    }

    void NullPlaybackInterface::StopPlayback() {
        stop = true;
        if (playback_thread.joinable())
            playback_thread.join();

    }

    void NullPlaybackInterface::AudioPlaybackJob() {
        while (!stop) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [this]{ return !packetq.empty(); });

            size_t bufsize = std::get<0>(packetq.front());
            free(std::get<1>(packetq.front()));
            packetq.pop();
            lock.unlock();

            LOGI("NullPlaybackInterface: packet consumed: length=%lu", bufsize);
        }
    }

} // WmUsbAudio