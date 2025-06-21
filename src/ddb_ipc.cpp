#include <errno.h>
#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
using json = nlohmann::json;

#include <deadbeef/deadbeef.h>

#include "argument.hpp"
#include "commands.hpp"
#include "ddb_ipc.hpp"
#include "message.hpp"
#include "properties.hpp"
#include "response.hpp"

namespace ddb_ipc {

DB_functions_t* ddb_api;
ddb_artwork_plugin_t* ddb_artwork;

const char configDialog_[] =
    "property \"Socket location\" entry " DDB_IPC_PROJECT_ID
    ".socketpath \"" DDB_IPC_DEFAULT_SOCKET "\" ;\n";

DB_plugin_t definition_;
int ipc_listening = 0;
int ddb_socket = -1;
pthread_t ipc_thread;
char socket_path[PATH_MAX];

typedef struct pollfd pollfd_t;

pollfd_t fds[DDB_IPC_MAX_CONNECTIONS + 1];

int open_socket(char* socket_path) {
    struct sockaddr_un name;
    size_t size;
    int sock;
    sock = socket(PF_LOCAL, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock < 0) {
        // TODO: Better error handling here
        DDB_IPC_ERR << "Error creating socket: " << errno << std::endl;
        return -1;
    }
    name.sun_family = PF_LOCAL;
    strncpy(name.sun_path, socket_path, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';
    size = SUN_LEN(&name);
    ::unlink(socket_path);
    if (bind(sock, (struct sockaddr*)&name, size) < 0) {
        // TODO: Better error handling here
        DDB_IPC_ERR << "Error binding socket: " << errno << std::endl;
        return -1;
    }
    return sock;
}

// Send a message to one client

void close_connection(int socket) {
    DDB_IPC_DEBUG << "Closed connection with descriptor " << socket
                  << std::endl;
    ;
    ::close(socket);
    for (int i = 0; i <= DDB_IPC_MAX_CONNECTIONS; i++) {
        if (fds[i].fd == socket) {
            fds[i].fd = -1;
            break;
        }
    }
    observers.erase(socket);
}

void send_response(json response, int socket) {
    std::string response_str = response.dump() + std::string("\n");
    int resp_len = response_str.length();
    const char* bytes = response_str.c_str();
    int max_packet_len = DDB_IPC_MAX_MESSAGE_LENGTH;
    int packet_len;
    struct timeval start_time, cur_time;
    int timeout_ms = DDB_IPC_WRITE_TIMEOUT;
    int waited_ms;
    pollfd_t pfd = {.fd = socket, .events = POLLOUT, .revents = 0};
    gettimeofday(&start_time, NULL);
    for (int i = 0; i < resp_len; i += max_packet_len) {
        packet_len = i + 4096 > resp_len ? resp_len - i : max_packet_len;
        // wait for the socket to become available for writing
        do {
            poll(&pfd, 1, timeout_ms);
            gettimeofday(&cur_time, NULL);
            waited_ms = (cur_time.tv_sec - start_time.tv_sec) * 1000 +
                        (cur_time.tv_usec - start_time.tv_usec) / 1000;
            if (waited_ms >= timeout_ms) {
                DDB_IPC_ERR << "Error sending response on descriptor " << socket
                            << ": timed out after " << timeout_ms << "ms"
                            << std::endl;
            }
            if (pfd.revents & POLLOUT) {
                break;
            }
        } while (1);
        if (send(socket, bytes + i, packet_len, MSG_NOSIGNAL) <= 0) {
            DDB_IPC_ERR << "Error sending response on descriptor " << socket
                        << ": errno " << errno << std::endl;
            close_connection(socket);
            return;
        }
    }
    if (resp_len > 1024 + 20) {
        DDB_IPC_DEBUG << "Responded: " << response_str.substr(0, 512)
                      << " [..., " << resp_len - 1024 << " characters omitted] "
                      << response_str.substr(resp_len - 512, 512) << "in "
                      << waited_ms << " ms." << std::endl;
    } else {
        DDB_IPC_DEBUG << "Responded: " << response << std::endl;
    }
}

// Send a message to all connected clients

void broadcast(json message) {
    std::string message_str = message.dump() + std::string("\n");
    DDB_IPC_DEBUG << "Broadcasting: " << message_str;
    for (int i = 1; i <= DDB_IPC_MAX_CONNECTIONS; i++) {
        if (fds[i].fd > -1) {
            send(
                fds[i].fd, message_str.c_str(), message_str.size(), MSG_NOSIGNAL
            );
        }
    }
}

void handle_message(Message m, int socket) {
    json response;
    response = call_command(m.command, m.id, m.args);
    send_response(response, socket);
}

void handle_message(json message, int socket) {
    if (!message.contains("args")) {
        message["args"] = {};
    }
    try {
        Message m = message;
        if (!m.args.is_object() and !m.args.is_null()) {
            send_response(
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

int read_messages(int fd) {
    // return value: 0 if no errors occured and the connection should be kept
    // open -1 otherwise
    int should_close_conn = 0;
    char buf[DDB_IPC_MAX_MESSAGE_LENGTH];
    int rc;
    json message;
    do {
        // zero out the buffer
        memset(buf, '\0', DDB_IPC_MAX_MESSAGE_LENGTH);
        rc = recv(fd, buf, DDB_IPC_MAX_MESSAGE_LENGTH, 0);
        if (rc < 0) {
            if (errno != EWOULDBLOCK) {
                should_close_conn = -1;
            }
            break;
        } else if (rc == 0) {
            should_close_conn = -1;
            break;
        }
        DDB_IPC_DEBUG << "Received message on descriptor " << fd << ": " << buf;
        std::istringstream msg(buf);
        std::string l;
        while (std::getline(msg, l)) {
            try {
                message = json::parse(l);
            } catch (const json::exception& e) {
                DDB_IPC_WARN << "Message is not valid JSON: " << e.what()
                             << std::endl;
                send_response(
                    json{
                        {"status", DDB_IPC_RESPONSE_ERR},
                        {"response",
                         std::string("Message is not valid JSON: ") + e.what()}
                    },
                    fd
                );
                continue;
            }
            handle_message(message, fd);
        }
    } while (1);
    return should_close_conn;
}

int accept_connection(int new_conn, pollfd_t* fds, int n_fds) {
    // try to find an open slot, return 0 if success, -1 otherwise
    for (int i = 1; i <= n_fds; i++) {
        if (fds[i].fd < 0) {
            fds[i].fd = new_conn;
            DDB_IPC_DEBUG << "Accepted new connection with descriptor "
                          << new_conn << std::endl;
            return 0;
        }
    }
    return -1;
}

void* listen(void* sockname) {
    int i;
    int rc;
    int new_conn;
    json message;

    pollfd_t open_slot = {.fd = -1, .events = POLLIN, .revents = 0};
    for (i = 0; i <= DDB_IPC_MAX_CONNECTIONS; i++) {
        memcpy(&fds[i], &open_slot, sizeof(pollfd_t));
    }

    ddb_socket = open_socket((char*)sockname);
    rc = ::listen(ddb_socket, 1);
    fds[0].fd = ddb_socket;

    while (ipc_listening) {
        rc = poll(fds, DDB_IPC_MAX_CONNECTIONS + 1, DDB_IPC_POLL_FREQ);
        if (rc < 0) {
            DDB_IPC_ERR << "Error reading from socket:" << errno << std::endl;
        }
        if (rc == 0) {
            // timed out
            continue;
        }
        if (fds[0].revents & POLLIN) {
            // TODO refactor this into its own function
            // there are incoming connections
            while ((new_conn = accept4(fds[0].fd, NULL, NULL, SOCK_NONBLOCK)) >=
                   0)
            {
                if (accept_connection(new_conn, fds, DDB_IPC_MAX_CONNECTIONS) <
                    0)
                {
                    json resp =
                        error_response(0, "Maximum connections reached.");
                    send_response(resp, new_conn);
                    ::close(new_conn);
                }
            }
            if (errno != EWOULDBLOCK) {
                DDB_IPC_DEBUG << "accept() failed";
            }
        }
        for (i = 1; i <= DDB_IPC_MAX_CONNECTIONS; i++) {
            if (fds[i].revents & POLLIN) {
                if (read_messages(fds[i].fd) < 0) {
                    close_connection(fds[i].fd);
                }
            }
        }
    }
    for (i = 0; i <= DDB_IPC_MAX_CONNECTIONS; i++) {
        if (fds[i].fd > -1) {
            ::close(fds[i].fd);
        }
    }
    observers.clear();
    return 0;
}

int start() { return 0; }

int stop() {
    DDB_IPC_DEBUG << "Stopping polling thread..." << std::endl;
    ipc_listening = 0;
    pthread_join(ipc_thread, NULL);
    DDB_IPC_DEBUG << "Closing socket...." << std::endl;
    ::close(ddb_socket);
    ::unlink(socket_path);
    return 0;
}

int disconnect() { return 0; }

int connect() {
    ddb_artwork = (ddb_artwork_plugin_t*)ddb_api->plug_get_for_id("artwork2");
    return 0;
}

// Handle events sent to us by DeaDBeeF

void on_toggle_pause(int p) {
    if (p) {
        broadcast(json{{"event", "paused"}});
    } else {
        broadcast(json{{"event", "unpaused"}});
    }
}

void on_track_changed() { broadcast(json{{"event", "track-changed"}}); }

void on_seek(ddb_event_playpos_t* ctx) {
    float dur = ddb_api->pl_get_item_duration(ctx->track);
    broadcast(
        json{
            {"event", "seek"},
            {"data",
             {
                 {"duration", dur},
                 {"position", ctx->playpos},
             }}
        }
    );
    return;
}

void on_volume_change() {
    float mindb = ddb_api->volume_get_min_db();
    float vol = 100 * (mindb - ddb_api->volume_get_db()) / mindb;
    broadcast(
        json{
            {"event", "property-change"}, {"property", "volume"}, {"value", vol}
        }
    );
}

void on_config_changed() {
    DDB_IPC_DEBUG << "Config changed..." << std::endl;
    broadcast(json{{"event", "config-changed"}});
    json resp;
    for (auto obs = observers.begin(); obs != observers.end(); obs++) {
        DDB_IPC_DEBUG << "Observer #" << obs->first << std::endl;
        auto propset = obs->second;
        for (auto prop = propset.begin(); prop != propset.end(); prop++) {
            resp = json{{"event", "property-change"}, {"property", *prop}};
            if (getters.count(*prop)) {
                resp["value"] = getters[*prop]();
            } else {
                resp["value"] = property_as_json(*prop);
            }
            DDB_IPC_DEBUG << "Property " << *prop << "; value " << resp["value"]
                          << std::endl;
            send_response(resp, obs->first);
        }
    }
}

void on_playlist_switched() {
    broadcast(
        json{
            {"event", "playlist-switched"}, {"idx", ddb_api->plt_get_curr_idx()}
        }
    );
}

int handleMessage(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2) {
    switch (id) {
        case DB_EV_PAUSED:
            on_toggle_pause(p1);
            break;
        case DB_EV_SEEKED:
            on_seek((ddb_event_playpos_t*)ctx);
            break;
        case DB_EV_SONGCHANGED:
            on_track_changed();
            break;
        case DB_EV_VOLUMECHANGED:
            on_volume_change();
            break;
        case DB_EV_CONFIGCHANGED:
            on_config_changed();
            break;
        case DB_EV_PLAYLISTSWITCHED:
            on_playlist_switched();
    }
    return 0;
}

void init(DB_functions_t* api) {
    auto& p = definition_;
    p.api_vmajor = 1;
    p.api_vminor = DDB_API_LEVEL;
    p.version_major = DDB_IPC_VERSION_MAJOR;
    p.version_minor = DDB_IPC_VERSION_MINOR;
    p.type = DB_PLUGIN_MISC;
    p.id = DDB_IPC_PROJECT_ID;
    p.name = DDB_IPC_PROJECT_NAME;
    p.descr = DDB_IPC_PROJECT_DESC;
    p.copyright = DDB_IPC_LICENSE_TEXT;
    p.website = DDB_IPC_PROJECT_URL;
    p.start = start;
    p.stop = stop;
    p.connect = connect;
    p.disconnect = disconnect;
    p.message = handleMessage;
    p.configdialog = configDialog_;
}

DB_plugin_t* load(DB_functions_t* api) {
    ddb_api = api;
    init(api);
    ipc_listening = 0;
    ddb_api->conf_get_str(
        DDB_IPC_PROJECT_ID ".socketpath",
        DDB_IPC_DEFAULT_SOCKET,
        socket_path,
        PATH_MAX
    );
    DDB_IPC_DEBUG << "Opening socket at " << socket_path << std::endl;
    ipc_listening = 1;
    pthread_create(&ipc_thread, NULL, listen, (void*)socket_path);
    return &definition_;
}

}  // namespace ddb_ipc

extern "C" DB_plugin_t* ddb_ipc_load(DB_functions_t* api) {
    return ddb_ipc::load(api);
}
