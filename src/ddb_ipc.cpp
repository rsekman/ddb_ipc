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

#include "ddb_ipc.hpp"

using namespace ddb_ipc;

const char configDialog_ [] =
    "property \"Socket location\" entry " DDB_IPC_PROJECT_ID ".socketpath \"" DDB_IPC_DEFAULT_SOCKET "\" ;\n";

DB_plugin_t definition_;
int ipc_listening = 0;
int ddb_socket = -1;
pthread_t ipc_thread;
char socket_path[PATH_MAX];

typedef struct pollfd pollfd_t;

int open_socket(char* socket_path){
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

void send_response(json response, int socket){
    std::string response_str = response.dump() + std::string("\n");
    if ( send(socket, response_str.c_str(), response_str.size(), MSG_NOSIGNAL) > 0 ) {
        DDB_IPC_DEBUG << "Responded: " << response << std::endl;
    } else{
        DDB_IPC_ERR << "Error sending response: errno " << errno << std::endl;
    }
    // TODO: error handling
}

void handle_message(json message, int socket){
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


void* listen(void* sockname){
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
    pollfd_t fds[DDB_IPC_MAX_CONNECTIONS +1];
    pollfd_t open_slot = {
        .fd = -1,
        .events = POLLIN,
        .revents = 0
    };
    int i;
    int rc;
    int new_conn, conn_accepted;
    int close_conn;
    char buffer[DDB_IPC_MAX_MESSAGE_LENGTH];
    for(i=1; i<= DDB_IPC_MAX_CONNECTIONS; i++){
        memcpy(&fds[i], &open_slot, sizeof(pollfd_t) );
    }
    while (ipc_listening){
        status = poll(fds, DDB_IPC_MAX_CONNECTIONS + 1, DDB_IPC_POLL_FREQ);
        if (status < 0 ){
            DDB_IPC_ERR << "Error reading from socket:" << errno << std::endl;
        }
        if (status == 0) {
            // timed out
            continue;
        }
        if(fds[0].revents & POLLIN){
            // there is an incoming connection
            while( new_conn = accept(fds[0].fd, NULL, NULL) >= 0) {
                conn_accepted = 0;
                // find an open slot
                for(i=1; i<= DDB_IPC_MAX_CONNECTIONS; i++){
                    if(fds[i].fd < 0){
                        fds[i].fd = new_conn;
                        DDB_IPC_DEBUG << "accepted new connectiod with descriptor" << new_conn;
                        conn_accepted = 1;
                        break;
                    }
                }
                if(!conn_accepted){
                    send_response( json {
                            {"status", DDB_IPC_RESPONSE_ERR},
                            {"response", std::string("Maximum connections reached.")}
                            },
                            new_conn);
                    ::close(new_conn);
                }
            }
            if (errno != EWOULDBLOCK) {
                DDB_IPC_DEBUG << "accept() failed";
            }
        }
        for(i=1; i<= DDB_IPC_MAX_CONNECTIONS; i++){
            close_conn = 0;
            if( fds[i].revents & POLLIN ) {
                do {
                    // there is data to read
                    rc = recv(fds[i].fd, buffer, DDB_IPC_MAX_MESSAGE_LENGTH, 0);
                    if( rc < 0) {
                        if( errno != EWOULDBLOCK ){
                            close_conn = 1;
                            break;
                        }
                    } else if (rc == 0) {
                        close_conn = 1;
                        break;
                    }
                    DDB_IPC_DEBUG << "We received this message on descriptor" << fds[i].fd << ":"  << buf;
                    try {
                        message = json::parse(buf);
                        handle_message(message, conn);
                    } catch (const json::exception& e) {
                        DDB_IPC_WARN << "Message is not valid JSON: " << e.what() << std::endl;
                        send_response( json {
                                {"status", DDB_IPC_RESPONSE_ERR},
                                {"response", std::string("Message is not VALID JSON: ") + e.what()}
                                },
                                fds[i].fd);
                    }
                } while (1);
                if(close_conn) {
                    ::close(fds[i].fd);
                    // free up the slot
                    fds[i].fd = -1;
                }
            }
        }

    }
    return 0;
}


int start() {
    return 0;
}

int stop() {
    DDB_IPC_DEBUG << "Stopping polling thread..." << std::endl;
    ipc_listening = 0;
    pthread_join(ipc_thread, NULL);
    DDB_IPC_DEBUG << "Closing socket...." << std::endl;
    ::close(ddb_socket);
    ::unlink(socket_path);
    return 0;
}

int disconnect(){
    return 0;
}

int connect(){
    return 0;
}

int handleMessage(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2){
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
    ddb_api->conf_get_str(DDB_IPC_PROJECT_ID ".socketpath", DDB_IPC_DEFAULT_SOCKET, socket_path, PATH_MAX);
    DDB_IPC_DEBUG << "Opening socket at " << socket_path << std::endl;
    ipc_listening = 1;
    pthread_create(&ipc_thread, NULL, listen, (void*) socket_path);
    return &definition_;
}

extern "C" DB_plugin_t* ddb_ipc_load(DB_functions_t* api) {
    return load(api);
}
