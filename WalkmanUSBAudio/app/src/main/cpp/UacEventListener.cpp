//
// Created by Bowen Cui on 2023-10-15.
//

#include "UacEventListener.h"
#include "android_log.h"

#include <sys/socket.h>
#include <exception>
#include <unistd.h>

#define TAG "UacEventListener"

namespace WmUsbAudio {

    UacEventListener::UacEventListener(UacEventReceiver *receiver) {
        this->receiver = receiver;
        memset(&event, 0, sizeof(event));

        memset(&src_addr, 0, sizeof(src_addr));
        src_addr.nl_family = AF_NETLINK;
        src_addr.nl_groups = -1;
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

        LOGI("UacEventListener instantiated, now listening...");
        cancel = false;
        listener_thread = std::thread(&UacEventListener::EventListener, this);
    }

    UacEventListener::~UacEventListener() {
        cancel = true;
        listener_thread.join();
    }

    void UacEventListener::SetReceiver(UacEventReceiver *receiver) {
        this->receiver = receiver;
    }

    void UacEventListener::EventListener() {
        int r;
        while (true) {
            if (cancel) return;
            r = recv(nl_socket, msg, NL_MAX_PAYLOAD, MSG_DONTWAIT);
            if (r < 0) {
                if (r != -1)
                    LOGW("Socket recv error: %d", r);
                std::this_thread::yield();
                continue;
            }
            // process received message
            LOGI("Uevent: length=%d, msg:\n%s", r, msg);
        }
    }

} // WmUsbAudio