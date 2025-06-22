#include "ipc_server.hpp"

#include <sys/time.h>

#include <iostream>
#include <mutex>

#include "commands.hpp"
#include "ddb_ipc.hpp"
#include "response.hpp"

namespace ddb_ipc {

void IPCServer::process_inbox() {
    while (running) {
        std::unique_lock lock(inbox_mutex);
        inbox_cv.wait(lock, [this]() {
            return !inbox.empty() || !this->running;
        });
        while (!inbox.empty()) {
            auto m = inbox.front();
            handle_message(m.message, m.address);
            inbox.pop_front();
        }
    }
}

void IPCServer::handle_message(Message m, int socket) {
    json response;
    response = call_command(m.command, m.id, m.args);
    send_message(response, socket);
}

void IPCServer::handle_message(json message, int socket) {
    if (!message.contains("args")) {
        message["args"] = {};
    }
    try {
        Message m = message;
        if (!m.args.is_object() and !m.args.is_null()) {
            send_message(
                bad_request_response(
                    m.id, "args must be a JSON object or null."
                ),
                socket
            );
        }
        m.args["socket"] = socket;
        handle_message(m, socket);
    } catch (Exception& e) {
        DDB_IPC_DEBUG << e.what() << std::endl;
    } catch (std::exception& e) {
        DDB_IPC_DEBUG << e.what() << std::endl;
    }
}

void IPCServer::send_message(json message, int socket) {
    std::lock_guard lock(outbox_mutex);
    outbox.emplace_back(message, socket);
    outbox_cv.notify_one();
}

void IPCServer::process_outbox() {
    while (running) {
        std::unique_lock lock(outbox_mutex);
        outbox_cv.wait(lock, [this]() {
            return !outbox.empty() || !this->running;
        });
        while (!outbox.empty()) {
            auto m = outbox.front();
            send_one(m.message, m.address);
            outbox.pop_front();
        }
    }
}

void IPCServer::send_one(json message, int socket) {
    std::string message_str = message.dump() + std::string("\n");
    int resp_len = message_str.length();
    const char* bytes = message_str.c_str();

    int max_packet_len = 16 * 4096;
    int packet_len;

    struct timeval start_time, pkt_time, cur_time;
    int timeout_ms = DDB_IPC_WRITE_TIMEOUT;
    int waited_ms;

    pollfd pfd = {.fd = socket, .events = POLLOUT, .revents = 0};

    gettimeofday(&start_time, NULL);
    size_t i = 0;
    std::lock_guard lock(socket_mutex);
    while (i < resp_len) {
        // wait for the socket to become available for writing
        gettimeofday(&pkt_time, NULL);
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret == 0) {
            DDB_IPC_ERR << "Error sending message (request id: "
                        << message["request_id"] << ") on descriptor " << socket
                        << ": timed out after " << timeout_ms << "ms"
                        << std::endl;
            close_connection(socket);
            return;
        }
        if (pfd.revents & POLLOUT) {
            gettimeofday(&cur_time, NULL);
            waited_ms = (cur_time.tv_sec - pkt_time.tv_sec) * 1000 +
                        (cur_time.tv_usec - pkt_time.tv_usec) / 1000;
            DDB_IPC_DEBUG << "Waited " << waited_ms
                          << " ms for socket to become ready" << std::endl;
        }
        gettimeofday(&pkt_time, NULL);
        while (i < resp_len) {
            packet_len =
                i + max_packet_len > resp_len ? resp_len - i : max_packet_len;
            ssize_t sent = send(socket, bytes + i, packet_len, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                DDB_IPC_ERR << "Error sending message (request id: "
                            << message["request_id"] << ") on descriptor "
                            << socket << ": errno " << errno << std::endl;
                close_connection(socket);
                return;
            } else if (sent > 0) {
                i += sent;
                gettimeofday(&cur_time, NULL);
                waited_ms = (cur_time.tv_sec - pkt_time.tv_sec) * 1000 +
                            (cur_time.tv_usec - pkt_time.tv_usec) / 1000;
                DDB_IPC_DEBUG << "Sent " << sent << " bytes in " << waited_ms
                              << " ms." << std::endl;
            }
        }
    }
    gettimeofday(&cur_time, NULL);
    waited_ms = (cur_time.tv_sec - start_time.tv_sec) * 1000 +
                (cur_time.tv_usec - start_time.tv_usec) / 1000;
    if (resp_len > 1024 + 20) {
        DDB_IPC_DEBUG << "Sent: " << message_str.substr(0, 512) << " [..., "
                      << resp_len - 1024 << " characters omitted] "
                      << message_str.substr(resp_len - 512, 512) << "in "
                      << waited_ms << " ms." << std::endl;
    } else {
        DDB_IPC_DEBUG << "Responded: " << message << "in " << waited_ms
                      << " ms." << std::endl;
        ;
    }
}

}  // namespace ddb_ipc
