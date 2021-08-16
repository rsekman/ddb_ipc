#include <stdlib.h>
#include <iostream>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/un.h>
#include <unistd.h>

#include <pthread.h>
#include <linux/limits.h>

#include <deadbeef/deadbeef.h>
#include <nlohmann/json.hpp>

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
    std::cerr << "Opening socket at " << socket_path << std::endl;
    ipc_listening = 1;
    pthread_create(&ipc_thread, NULL, listen, (void*) socket_path);
    return &definition_;
}

int Plugin::open_socket(char* socket_path){
    struct sockaddr_un name;
    size_t size;
    int sock;
    sock  = socket(PF_LOCAL, SOCK_DGRAM, 0);
    if (sock < 0 ){
        // TODO: Better error handling here
        std::cerr << "Error creating socket: " << errno << std::endl;
        return -1;
    }
    name.sun_family = AF_LOCAL;
    strncpy(name.sun_path, socket_path, sizeof(name.sun_path));
    name.sun_path[sizeof(name.sun_path) - 1] = '\0';
    size = SUN_LEN(&name);
    if ( bind(sock, (struct sockaddr* ) &name, size) < 0 ){
        // TODO: Better error handling here
        std::cerr << "Error binding socket: " << errno << std::endl;
        return -1;
    }
    return sock;
}

void* Plugin::listen(void* sockname){
    char buf[DDB_IPC_MAX_MESSAGE_LENGTH];
    buf[0] = '\0';
    ddb_socket = open_socket((char*) sockname);
    int status;
    struct pollfd fds = {
        ddb_socket,
        POLLIN
    };
    while (ipc_listening){
        // nanosleep ( &req, NULL );
        std::cerr << "Pollling socket..." << std::endl;
        status = poll(&fds, 1, DDB_IPC_POLL_FREQ);
        if (status <= 0 ){
            continue;
        }
        if ( recv( ddb_socket, buf, DDB_IPC_MAX_MESSAGE_LENGTH, 0) > 0 ){
            std::cerr << "We received this message: " << buf;
        }
        buf[0] = '\0';

    }
    //::close(conn);
    return 0;
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
    std::cerr << "Stopping polling thread..." << std::endl;
    ipc_listening = 0;
    pthread_join(ipc_thread, NULL);
    std::cerr << "Closing socket...." << std::endl;
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
