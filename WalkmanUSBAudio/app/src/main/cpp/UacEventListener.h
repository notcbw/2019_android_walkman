//
// Created by Bowen Cui on 2023-10-15.
//

#ifndef WALKMAN_USB_AUDIO_UACEVENTLISTENER_H
#define WALKMAN_USB_AUDIO_UACEVENTLISTENER_H

#include <thread>
#include <vector>
#include <linux/netlink.h>

#define NL_MAX_PAYLOAD 8192

namespace WmUsbAudio {

    struct UacEvent {
        short action;
        short format;
        short bitwidth;
        short subslot;
        unsigned int freq;
    };

    class UacEventReceiver {
        virtual void UacEventProcess(struct UacEvent *event) = 0;
    };

    class NullEventReceiver : public UacEventReceiver {
        void UacEventProcess(struct UacEvent *event) {
            // do nothing
        }
    };

    class UacEventListener {
    public:
        UacEventListener(UacEventReceiver *receiver);
        ~UacEventListener();

        void SetReceiver(UacEventReceiver *receiver);

    private:
        UacEventReceiver *receiver;
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
