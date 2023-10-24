//
// Created by Bowen Cui on 2023-10-15.
//

#ifndef WALKMAN_USB_AUDIO_UACEVENTLISTENER_H
#define WALKMAN_USB_AUDIO_UACEVENTLISTENER_H

#include <thread>
#include <vector>
#include <memory>
#include <linux/netlink.h>
#include "android_log.h"

#define NL_MAX_PAYLOAD 512
#define TAG "UacEventListener"

namespace WmUsbAudio {

    enum class UacEventState: short { NONE, PLAY, STOP };
    enum class UacEventFormat: short { NONE, PCM, DSD };

    struct UacEvent {
        UacEventState state;
        UacEventFormat format;
        short bitwidth;
        short subslot;
        unsigned int freq;
    };

    void log_uac_event(struct UacEvent* event);

    class UacEventReceiver {
    public:
        virtual void ProcessUacEvent(const struct UacEvent *event) = 0;
    };

    class NullEventReceiver : public UacEventReceiver {
        void ProcessUacEvent(const struct UacEvent *event) {
            // do nothing
            LOGD("Null event receiver: event processed");
        }
    };

    class UacEventListener {
    public:
        UacEventListener();
        UacEventListener(std::vector<UacEventReceiver *>& receivers);
        ~UacEventListener();

        std::vector<UacEventReceiver *> &GetReceivers();
        void SetReceivers(std::vector<UacEventReceiver *> recv);
    private:
        std::vector<UacEventReceiver *> receivers;
        std::thread listener_thread;
        bool cancel;
        int nl_socket;
        struct sockaddr_nl src_addr;
        struct UacEvent event;
        char msg[NL_MAX_PAYLOAD];

        void EventListener();
    };

} // WmUsbAudio

#endif //WALKMAN_USB_AUDIO_UACEVENTLISTENER_H
