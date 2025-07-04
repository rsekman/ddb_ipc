#include <errno.h>
#include <pthread.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
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
std::recursive_mutex sock_mutex;

typedef struct pollfd pollfd_t;

pollfd_t fds[DDB_IPC_MAX_CONNECTIONS + 1];

std::shared_ptr<spdlog::logger> get_logger() {
    return spdlog::get(DDB_IPC_PROJECT_ID);
}

int open_socket(char* socket_path) {
    struct sockaddr_un name;
    size_t size;
    int sock;
    sock = socket(PF_LOCAL, SOCK_STREAM | SOCK_NONBLOCK, 0);

    auto logger = get_logger();
    if (sock < 0) {
        // TODO: Better error handling here
        logger->error("Error creating socket: {}", errno);
        return -1;
    }
    name.sun_family = PF_LOCAL;
    strncpy(name.sun_path, socket_path, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';
    size = SUN_LEN(&name);
    ::unlink(socket_path);
    if (bind(sock, (struct sockaddr*)&name, size) < 0) {
        // TODO: Better error handling here
        logger->error("Error binding socket: {}", errno);
        return -1;
    }
    return sock;
}

// Send a message to one client

void close_connection(int socket) {
    auto logger = get_logger();
    logger->debug("Closed connection with descriptor {}.", socket);
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
    int max_packet_len = DDB_IPC_MAX_PACKET_LENGTH;
    int packet_len;
    struct timeval start_time, pkt_time, cur_time;
    int timeout_ms = DDB_IPC_WRITE_TIMEOUT;
    int waited_ms;
    pollfd_t pfd = {.fd = socket, .events = POLLOUT, .revents = 0};
    gettimeofday(&start_time, NULL);
    size_t i = 0;

    auto logger = get_logger();
    std::lock_guard lock(sock_mutex);
    int req_id = response["request_id"];
    while (i < resp_len) {
        // for (int i = 0; i < resp_len; i += max_packet_len) {
        // wait for the socket to become available for writing
        gettimeofday(&pkt_time, NULL);
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret == 0) {
            logger->error(
                "Error sending response (request id: {}) on descriptor {}: "
                "timed out after {} ms.",
                req_id,
                socket,
                timeout_ms
            );
            close_connection(socket);
            return;
        }
        if (ret < 0) {
            logger->error(
                "Error sending response (request id: {}) on descriptor {}: {}",
                req_id,
                socket,
                errno
            );
            close_connection(socket);
            return;
        }
        while (i < resp_len) {
            packet_len =
                i + max_packet_len > resp_len ? resp_len - i : max_packet_len;
            ssize_t sent = send(socket, bytes + i, packet_len, MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                logger->error(
                    "Error sending response (request id: {}) on descriptor {}: "
                    "{}.",
                    req_id,
                    socket,
                    errno
                );
                close_connection(socket);
                return;
            } else {
                i += sent;
            }
        }
    }
    gettimeofday(&cur_time, NULL);
    waited_ms = (cur_time.tv_sec - start_time.tv_sec) * 1000 +
                (cur_time.tv_usec - start_time.tv_usec) / 1000;
    size_t elision_len = 1024;
    response_str.erase(response_str.end() - 1);
    if (resp_len > elision_len + 20) {
        logger->debug(
            "Responded (request id: {}): {} [..., {} characters omitted] {} in "
            "{} ms.",
            req_id,
            response_str.substr(0, elision_len / 2),
            resp_len - elision_len,
            response_str.substr(resp_len - elision_len / 2, elision_len / 2),
            waited_ms
        );
    } else {
        logger->debug(
            "Responded (request id: {}): {} in {} ms.",
            req_id,
            response_str,
            waited_ms
        );
    }
}

// Send a message to all connected clients

void broadcast(json message) {
    auto logger = get_logger();
    std::string message_str = message.dump();
    logger->debug("Broadcasting: {}.", message_str);
    message_str += "\n";
    std::lock_guard lock(sock_mutex);
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
    auto logger = get_logger();
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
        logger->debug("Invalid message {}: {}.", message.dump(), e.what());
    } catch (std::exception& e) {
        logger->debug(
            "Error handling message {}: {}.", message.dump(), e.what()
        );
    }
}

int read_messages(int fd) {
    // return value: 0 if no errors occured and the connection should be kept
    // open -1 otherwise
    int should_close_conn = 0;
    char buf[DDB_IPC_MAX_PACKET_LENGTH];
    int rc;
    json message;
    std::lock_guard lock(sock_mutex);

    auto logger = get_logger();
    do {
        // zero out the buffer
        memset(buf, '\0', DDB_IPC_MAX_PACKET_LENGTH);
        rc = recv(fd, buf, DDB_IPC_MAX_PACKET_LENGTH, 0);
        if (rc < 0) {
            if (errno != EWOULDBLOCK) {
                should_close_conn = -1;
            }
            break;
        } else if (rc == 0) {
            should_close_conn = -1;
            break;
        }
        logger->debug("Received message on descriptor {}: {}.", fd, buf);
        std::istringstream msg(buf);
        std::string l;
        while (std::getline(msg, l)) {
            try {
                message = json::parse(l);
            } catch (const json::exception& e) {
                logger->warn("Message is not valid JSON: {}.", e.what());
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
    auto logger = get_logger();
    for (int i = 1; i <= n_fds; i++) {
        if (fds[i].fd < 0) {
            fds[i].fd = new_conn;
            logger->debug(
                "Accepted new connection with descriptor {}.", new_conn
            );
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

    auto logger = get_logger();
    while (ipc_listening) {
        rc = poll(fds, DDB_IPC_MAX_CONNECTIONS + 1, DDB_IPC_POLL_FREQ);
        if (rc < 0) {
            logger->error("Error reading from socket: {}.", errno);
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
                logger->warn("accept() failed");
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
    auto logger = get_logger();
    logger->debug("Stopping polling thread...");
    ipc_listening = 0;
    pthread_join(ipc_thread, NULL);
    logger->debug("Closing socket....");
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
    auto logger = get_logger();
    logger->debug("Config changed...");
    broadcast(json{{"event", "config-changed"}});
    json resp;
    for (auto obs = observers.begin(); obs != observers.end(); obs++) {
        logger->debug("Handling observer #{}.", obs->first);
        auto propset = obs->second;
        for (auto prop = propset.begin(); prop != propset.end(); prop++) {
            resp = json{{"event", "property-change"}, {"property", *prop}};
            if (getters.count(*prop)) {
                resp["value"] = getters[*prop]();
            } else {
                resp["value"] = property_as_json(*prop);
            }
            logger->debug(
                "Property {}; value {}.", *prop, resp["value"].dump()
            );
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

    auto logger = spdlog::stderr_color_mt(DDB_IPC_PROJECT_ID);
    logger->set_level(spdlog::level::debug);
    logger->set_pattern("[%n] [%^%l%$] [thread %t] %v");
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
    auto logger = get_logger();
    logger->debug("Opening socket at {}.", socket_path);
    ipc_listening = 1;
    pthread_create(&ipc_thread, NULL, listen, (void*)socket_path);
    return &definition_;
}

}  // namespace ddb_ipc

extern "C" DB_plugin_t* ddb_ipc_load(DB_functions_t* api) {
    return ddb_ipc::load(api);
}
