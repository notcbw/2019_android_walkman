//
// Created by Bowen Cui on 2023-10-15.
//

#include "UacSink.h"

#include <memory>
#include <tuple>
#include <exception>

#include <tinyalsa/asoundlib.h>

namespace WmUsbAudio {

    enum pcm_format conv_pcm_format(const struct UacEvent *event) {
        if (event->format == UacEventFormat::DSD) {
            return PCM_FORMAT_DSD_U8;
        } else {
            switch (event->subslot) {
                case 3:
                    return PCM_FORMAT_S24_3LE;
                case 4:
                    return PCM_FORMAT_S32_LE;
                default:
                    return PCM_FORMAT_S16_LE;
            }
        }
    }

    UacSink::UacSink(IPlaybackInterface *pb) {
        this->state = UacEventState::NONE;
        this->pb = pb;
        LOGD("UacSink: created");
    }

    UacSink::~UacSink() {
        StopStreaming();
        pcm_close(pcm_handle);
        LOGD("UacSink: destroyed");
    }

    void UacSink::ProcessUacEvent(const struct UacEvent *event) {
        switch (event->state) {
            case UacEventState::PLAY: {
                switch (state) {
                    case UacEventState::PLAY: {
                        mutex.lock();
                        pcm_close(pcm_handle);
                        pcm_config = {
                                .channels = 2,
                                .rate = event->freq,
                                .period_size = PERIOD_SIZE,
                                .period_count = PERIOD_COUNT,
                                .format = conv_pcm_format(event),
                                .start_threshold = PERIOD_SIZE * 2,
                                .stop_threshold = PERIOD_SIZE * 2,
                                .silence_threshold = 0,
                                .silence_size = 0
                        };
                        pcm_open(ALSA_CARD, ALSA_DEVICE, PCM_IN, &pcm_config);
                        mutex.unlock();
                        frame_size = pcm_frames_to_bytes(pcm_handle, 1);
                        break;
                    }
                    case UacEventState::STOP: {
                        state = UacEventState::PLAY;
                        cond.notify_all();
                        break;
                    }
                    default: {
                        state = UacEventState::PLAY;
                        StartStreaming(event);
                    }
                }
            }
            case UacEventState::STOP: {
                pcm_stop(pcm_handle);
                state = UacEventState::STOP;
                StartStreaming(event);
            }
            default: {
                StopStreaming();
            }
        }

    }

    void UacSink::SetPlaybackInterface(IPlaybackInterface *pb) {
        this->pb = pb;
    }

    void UacSink::StartStreaming(const struct UacEvent *event) {
        pcm_config = {
                .channels = 2,
                .rate = event->freq,
                .period_size = PERIOD_SIZE,
                .period_count = PERIOD_COUNT,
                .format = conv_pcm_format(event),
                .start_threshold = PERIOD_SIZE * 2,
                .stop_threshold = PERIOD_SIZE * 2,
                .silence_threshold = 0,
                .silence_size = 0
        };
        pcm_handle = pcm_open(ALSA_CARD, ALSA_DEVICE, PCM_IN, &pcm_config);
        if (pcm_handle == nullptr) {
            throw std::runtime_error("Failed allocate memory for UAC2 sink!");
        } else if (!pcm_is_ready(pcm_handle)) {
            pcm_close(pcm_handle);
            LOGE("failed to open uac sink: %s", pcm_get_error(pcm_handle));
            throw std::runtime_error("Failed open UAC2 sink!");
        }

        frame_size = pcm_frames_to_bytes(pcm_handle, 1);

        //state = UacEventState::PLAY;
        stream_thread = std::thread(&UacSink::AudioStreamJob, this);
        LOGD("UacSink: streaming started");
    }

    void UacSink::StopStreaming() {
        state = UacEventState::NONE;
        cond.notify_all();
        stream_thread.join();
        LOGD("UacSink: streaming stopped");
    }

    void UacSink::AudioStreamJob() {
        while (state != UacEventState::NONE) {
            std::unique_lock<std::mutex> lock(mutex);
            cond.wait(lock, [this]{ return state != UacEventState::STOP; });
            if (state == UacEventState::NONE) return;

            size_t bufsize = PERIOD_SIZE * frame_size;
            void *buf = malloc(bufsize);

            int read_count = pcm_read(pcm_handle, buf, PERIOD_SIZE);
            if (read_count > 0) {
                bufsize = pcm_frames_to_bytes(pcm_handle, read_count);
                LOGD("Packet sent: len=%d frames, %u bytes", read_count, bufsize);
                pb->ProcessAudio({bufsize, buf});
            } else {
                free(buf);
            }
        }
    }
} // WmUsbAudio