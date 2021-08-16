#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <string>
#include <errno.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <unistd.h>

#include <pthread.h>
#include <linux/limits.h>

#include <deadbeef/deadbeef.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "defs.hpp"
#include "ddb_ipc.hpp"


namespace ddb_ipc{

const char Plugin::configDialog_ [] =
    "property \"Socket location\" entry " DDB_IPC_PROJECT_ID ".socketpath \"" DDB_IPC_DEFAULT_SOCKET "\" ;\n";

DB_plugin_t Plugin::definition_;
int Plugin::ipc_listening = 0;
int Plugin::ddb_socket = -1;
pthread_t Plugin::ipc_thread;
char Plugin::socket_path[PATH_MAX];

DB_plugin_t* Plugin::load(DB_functions_t* api) {
    ddb_api = api;
    init(api);
    ipc_listening = 0;
    ddb_api->conf_get_str(DDB_IPC_PROJECT_ID ".socketpath", DDB_IPC_DEFAULT_SOCKET, socket_path, PATH_MAX);
    DDB_IPC_DEBUG << "Opening socket at " << socket_path << std::endl;
    ipc_listening = 1;
    pthread_create(&ipc_thread, NULL, listen, (void*) socket_path);
    return &definition_;
}

int Plugin::open_socket(char* socket_path){
    struct sockaddr_un name;
    size_t size;
    int sock;
    sock  = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (sock < 0 ){
        // TODO: Better error handling here
        DDB_IPC_ERR << "Error creating socket: " << errno << std::endl;
        return -1;
    }
    name.sun_family = PF_LOCAL;
    strncpy(name.sun_path, socket_path, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';
    size = SUN_LEN(&name);
    ::unlink(socket_path);
    if ( bind(sock, (struct sockaddr* ) &name, size) < 0 ){
        // TODO: Better error handling here
        DDB_IPC_ERR << "Error binding socket: " << errno << std::endl;
        return -1;
    }
    return sock;
}

void* Plugin::listen(void* sockname){
    char buf[DDB_IPC_MAX_MESSAGE_LENGTH];
    buf[0] = '\0';
    ddb_socket = open_socket((char*) sockname);
    int status;
    json message;
    int conn;
    int ret = ::listen(ddb_socket, 1);
    while ( 1 ){
        conn = accept( ddb_socket, NULL, NULL);
        if ( conn < 0 ) {
            continue;
        } else {
            DDB_IPC_DEBUG << "Accepted incoming connection." << std::endl;
            break;
        }
    }
    struct pollfd fds = {
        conn,
        POLLIN
    };
    while (ipc_listening){
        status = poll(&fds, 1, DDB_IPC_POLL_FREQ);
        if (status < 0 ){
            DDB_IPC_ERR << "Error reading from socket:" << errno << std::endl;
        }
        if (status <= 0) {
            continue;
        }
        if ( recv( conn, buf, DDB_IPC_MAX_MESSAGE_LENGTH, 0) > 0 ){
            DDB_IPC_DEBUG << "We received this message: " << buf;
            try {
                message = json::parse(buf);
            } catch (const json::exception& e) {
                DDB_IPC_WARN << "Message is not valid JSON: " << e.what() << std::endl;
                send_response( json {
                        {"status", DDB_IPC_RESPONSE_ERR},
                        {"response", std::string("Message is not VALID JSON: ") + e.what()}
                    },
                 conn);
                continue;
            }
            handle_message(message, conn);
        }
        buf[0] = '\0';

    }
    return 0;
}

void Plugin::handle_message(json message, int socket){
    json response;
    if ( message.contains("id") && message["id"].type() == json::value_t::number_integer ){
        response["id"] = message["id"];
    }
    if ( !message.contains("command") || message["command"].type() != json::value_t::string ) {
        DDB_IPC_WARN << "Bad request: `command` field must be present and must be a string." << std::endl;
        response["status"]   = DDB_IPC_RESPONSE_BADQ;
        response["response"] = "`command` field must be present and must be a string.";
        send_response(response, socket);
        return;
    }
}

void Plugin::send_response(json response, int socket){
    std::string response_str = response.dump() + std::string("\n");
    if ( send(socket, response_str.c_str(), response_str.size(), MSG_NOSIGNAL) > 0 ) {
        DDB_IPC_DEBUG << "Responded: " << response << std::endl;
    } else{
        DDB_IPC_ERR << "Error sending response: errno " << errno << std::endl;
    }
    // TODO: error handling
}

void Plugin::init(DB_functions_t* api) {
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

int Plugin::start() {
    return 0;
}

int Plugin::stop() {
    DDB_IPC_DEBUG << "Stopping polling thread..." << std::endl;
    ipc_listening = 0;
    pthread_join(ipc_thread, NULL);
    DDB_IPC_DEBUG << "Closing socket...." << std::endl;
    ::close(ddb_socket);
    ::unlink(socket_path);
    return 0;
}

int Plugin::disconnect(){
    return 0;
}

int Plugin::connect(){
    return 0;
}

int Plugin::handleMessage(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2){
    return 0;
}

extern "C" DB_plugin_t* ddb_ipc_load(DB_functions_t* api) {
    return Plugin::load(api);
}

}
