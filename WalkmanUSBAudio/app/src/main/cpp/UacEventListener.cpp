//
// Created by Bowen Cui on 2023-10-15.
//

#include "UacEventListener.h"
#include "android_log.h"

#include <sys/socket.h>
#include <exception>
#include <unistd.h>
#include <cstring>

namespace WmUsbAudio {

    const char* uac_event_state_to_string(UacEventState& state) {
        switch (state) {
            case UacEventState::NONE:
                return "NONE";
            case UacEventState::PLAY:
                return "PLAY";
            case UacEventState::STOP:
                return "STOP";
            default:
                return "UNKNOWN";
        }
    }

    const char* uac_event_format_to_string(UacEventFormat& format) {
        switch (format) {
            case UacEventFormat::NONE:
                return "NONE";
            case UacEventFormat::PCM:
                return "PCM";
            case UacEventFormat::DSD:
                return "DSD";
            default:
                return "UNKNOWN";
        }
    }

    void log_uac_event(struct UacEvent* event) {
        LOGD("UAC2 Uevent received:\n"
                "  State: %s\n"
                "  Format: %s\n"
                "  Bit Width: %hd\n"
                "  Subslot: %hd\n"
                "  Sampling Rate: %u\n",
             uac_event_state_to_string(event->state),
             uac_event_format_to_string(event->format),
             event->bitwidth,
             event->subslot,
             event->freq
            );
    }

    UacEventListener::UacEventListener() {
        memset(&event, 0, sizeof(event));

        memset(&src_addr, 0, sizeof(src_addr));
        src_addr.nl_family = AF_NETLINK;
        src_addr.nl_groups = 1;
        src_addr.nl_pid = getpid();

        nl_socket = socket(AF_NETLINK, (SOCK_DGRAM | SOCK_CLOEXEC), NETLINK_KOBJECT_UEVENT);
        if (nl_socket < 0) {
            LOGE("Failed to create socket");
            throw std::runtime_error("UacEventListener: Failed to create socket");
        }

        int ret = bind(nl_socket, (struct sockaddr*) &src_addr, sizeof(src_addr));
        if (ret) {
            LOGE("Failed to bind netlink socket");
            close(nl_socket);
            throw std::runtime_error("UacEventListener: Failed to bind netlink socket");
        }

        LOGD("UacEventListener instantiated, now listening...");
        cancel = false;
        listener_thread = std::thread(&UacEventListener::EventListener, this);
    }

    UacEventListener::UacEventListener(std::vector<UacEventReceiver *> &recv)
        : UacEventListener() {
        receivers.insert(receivers.end(), recv.begin(), recv.end());
    }

    UacEventListener::~UacEventListener() {
        // stop listener thread
        cancel = true;
        listener_thread.join();
        LOGD("UacEventListener: listener thread is stopped!");

        // make fake event message
        this->event.state = UacEventState::NONE;
        this->event.format = UacEventFormat::NONE;
        this->event.bitwidth = 0;
        this->event.subslot = 0;
        this->event.freq = 0;

        // send fake event message to make the receivers stop
        std::vector<std::thread> workers;
        for (UacEventReceiver *recv : receivers)
            workers.push_back(std::thread(&UacEventReceiver::ProcessUacEvent,
                                          recv,
                                          &event));
        for (std::thread &w : workers)
            w.join();

        LOGD("UacEventListener: destroyed");
    }

    std::vector<UacEventReceiver *>& UacEventListener::GetReceivers() {
        return this->receivers;
    }

    void UacEventListener::SetReceivers(std::vector<UacEventReceiver *> recv) {
        this->receivers = recv;
    }

    void UacEventListener::EventListener() {
        int r;
        while (true) {
            if (cancel) return;
            r = recv(nl_socket, msg, NL_MAX_PAYLOAD, 0);
            if (r < 0) {
                if (r != -1)
                    LOGW("Socket recv error: %d", r);
                std::this_thread::yield();
                continue;
            }
            // process received message
            /* Example:
             * Uevent: length=201, msg:
                    change@/devices/virtual/uac2_audio/UAC2_Gadget.0
                    ACTION=change
                    DEVPATH=/devices/virtual/uac2_audio/UAC2_Gadget.0
                    SUBSYSTEM=uac2_audio
                    STATE=PLAY
                    FORMAT=PCM
                    FREQ=48000
                    BITWIDTH=32
                    SUBSLOT=4
                    SEQNUM=4939
             */
            msg[r] = 0; // prevent garbage from being loaded

            char *key = msg;
            char *value;
            bool is_uac_event = false;
            uint8_t field_filled = 0;
            // parse received message and fill the UacEvent structure buffer
            for (int i = 0; i < r; i++) {
                switch (msg[i]) {
                    case '\0':
                        key = &msg[i+1];
                        break;
                    case '=':
                        value = &msg[i+1];
                        if (strncmp(key, "SUBSYSTEM", 6) == 0) {
                            // check if event is from the uac2 gadget
                            if (strncmp(value, "uac2_audio", 5) == 0){
                                LOGD("uac2 event being processed");
                                is_uac_event = true;
                            } else {
                                break;
                            }
                        } else if (strncmp(key, "STATE", 4) == 0) {
                            if (strncmp(value, "PLAY", 4) == 0) {
                                event.state = UacEventState::PLAY;
                            } else if (strncmp(value, "STOP", 4) == 0) {
                                event.state = UacEventState::STOP;
                            } else {
                                event.state = UacEventState::NONE;
                            }
                            field_filled |= 0b00001;
                        } else if (strncmp(key, "FORMAT", 4) == 0) {
                            if (strncmp(value, "PCM", 3) == 0) {
                                event.format = UacEventFormat::PCM;
                            } else if (strncmp(value, "DSD", 3) == 0) {
                                event.format = UacEventFormat::DSD;
                            } else {
                                event.format = UacEventFormat::NONE;
                            }
                            field_filled |= 0b00010;
                        } else if (strncmp(key, "FREQ", 4) == 0) {
                            event.freq = (unsigned int) std::stoul(value);
                            field_filled |= 0b00100;
                        } else if (strncmp(key, "BITWIDTH", 4) == 0) {
                            event.bitwidth = (short) std::stoi(value);
                            field_filled |= 0b01000;
                        } else if (strncmp(key, "SUBSLOT", 6) == 0) {
                            event.subslot = (short) std::stoi(value);
                            field_filled |= 0b10000;
                        }
                        break;
                }
                // if all fields are filled, break out of the loop
                if (field_filled == 0b11111)
                    break;
            }

            // if not complete, mark it as invalid
            if (field_filled != 0b11111)
                is_uac_event = false;

            if (is_uac_event) {
                log_uac_event(&event);
//                std::vector<std::thread> workers;
//                for (UacEventReceiver *recv : receivers)
//                    workers.push_back(std::thread(&UacEventReceiver::ProcessUacEvent,
//                                                  recv,
//                                                  &event));
//                for (std::thread &w : workers)
//                    w.join();
                for (UacEventReceiver *recv : receivers)
                    recv->ProcessUacEvent(&event);
            } else {
                LOGD("Non-uac2 event identified");
            }
        }
    }

} // WmUsbAudio