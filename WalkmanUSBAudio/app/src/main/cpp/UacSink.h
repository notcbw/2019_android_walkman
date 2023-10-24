//
// Created by Bowen Cui on 2023-10-15.
//

#ifndef WALKMAN_USB_AUDIO_UACSINK_H
#define WALKMAN_USB_AUDIO_UACSINK_H

#include <memory>
#include <mutex>
#include <condition_variable>
#include <tinyalsa/asoundlib.h>

#include "UacEventListener.h"
#include "IPlaybackInterface.h"

#define TAG "UacSink"

#define ALSA_CARD 2
#define ALSA_DEVICE 0

#define PERIOD_SIZE 1024
#define PERIOD_COUNT 4

namespace WmUsbAudio {

    enum pcm_format conv_pcm_format(const struct UacEvent *event);

    class UacSink : public UacEventReceiver {
    public:
        UacSink(IPlaybackInterface *pb);
        ~UacSink();

        void ProcessUacEvent(const struct UacEvent *event);
        void SetPlaybackInterface(IPlaybackInterface *pb);
    private:
        struct pcm *pcm_handle;
        IPlaybackInterface *pb;
        std::thread stream_thread;
        std::mutex mutex;
        std::condition_variable cond;
        UacEventState state;
        struct pcm_config pcm_config;

        unsigned int frame_size;    // byte size of one frame

        void StartStreaming(const struct UacEvent *event);
        void StopStreaming();

        // audio streaming thread
        void AudioStreamJob();
    };

} // WmUsbAudio

#endif //WALKMAN_USB_AUDIO_UACSINK_H
