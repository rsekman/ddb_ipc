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

pollfd_t fds[DDB_IPC_MAX_CONNECTIONS +1];

int open_socket(char* socket_path){
    struct sockaddr_un name;
    size_t size;
    int sock;
    sock  = socket(PF_LOCAL, SOCK_STREAM | SOCK_NONBLOCK, 0);
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

// Send a message to one client

void send_response(json response, int socket){
    std::string response_str = response.dump() + std::string("\n");
    if ( send(socket, response_str.c_str(), response_str.size(), MSG_NOSIGNAL) > 0 ) {
        DDB_IPC_DEBUG << "Responded: " << response << std::endl;
    } else{
        DDB_IPC_ERR << "Error sending response: errno " << errno << std::endl;
    }
    // TODO: error handling
}

// Send a message to all connected clients

void broadcast(json message) {
    std::string message_str = message.dump() + std::string("\n");
    DDB_IPC_DEBUG << "Broadcasting: " << message_str;
    for(int i=1; i<=DDB_IPC_MAX_CONNECTIONS; i++) {
        if(fds[i].fd > -1) {
            send(fds[i].fd, message_str.c_str(), message_str.size(), MSG_NOSIGNAL);
        }
    }
}

// Boilerplate response

json ok_response(int id) {
    json resp = json { {"status", DDB_IPC_RESPONSE_OK} };
    if(id) {
        resp["request_id"] = id;
    }
    return resp;
};

json bad_request_response(int id, std::string mess) {
    json resp = json { {"status", DDB_IPC_RESPONSE_BADQ}, {"response", mess} };
    if(id) {
        resp["request_id"] = id;
    }
    return resp;
}
json error_response(int id, std::string mess) {
    json resp = json { {"status", DDB_IPC_RESPONSE_ERR}, {"response", mess} };
    if(id) {
        resp["request_id"] = id;
    }
    return resp;
}

// Command handlers

json command_play(int id, json args) {
    ddb_api->sendmessage(DB_EV_PLAY_CURRENT, 0, 0, 0);
    return ok_response(id);
}

json command_pause(int id, json args) {
    ddb_api->sendmessage(DB_EV_PAUSE, 0, 0, 0);
    return ok_response(id);
}

json command_play_pause(int id, json args) {
    ddb_api->sendmessage(DB_EV_TOGGLE_PAUSE, 0, 0, 0);
    return ok_response(id);
}

json command_stop(int id, json args) {
    ddb_api->sendmessage(DB_EV_STOP, 0, 0, 0);
    return ok_response(id);
}

json command_prev(int id, json args) {
    ddb_api->sendmessage(DB_EV_PREV, 0, 0, 0);
    return ok_response(id);
}

json command_next(int id, json args) {
    ddb_api->sendmessage(DB_EV_NEXT, 0, 0, 0);
    return ok_response(id);
}

json command_set_volume(int id, json args) {
    if( !args.contains("volume") ) {
        return bad_request_response(id, std::string("Argument volume is mandatory"));
    }
    if(!args["volume"].is_number()) {
        return bad_request_response(id, std::string("Argument volume must be a float"));
    }
    float vol = args["volume"];
    float mindb = ddb_api->volume_get_min_db();
    if(vol > 100 || vol < 0) {
        return bad_request_response(id, std::string("Argument volume must be from [0, 100]"));
    }
    ddb_api->volume_set_db( (1-vol/100) * mindb );
    return ok_response(id);
}

json command_adjust_volume(int id, json args) {
    if( !args.contains("adjustment") ) {
        return bad_request_response(id, std::string("Argument adjustment is mandatory"));
    }
    if( !args["adjustment"].is_number() ) {
        return bad_request_response(id, std::string("Argument adjustment must be a float"));
    }
    float adj = ((float) args["adjustment"])/100;
    if(adj > 1 || adj < -1) {
        return bad_request_response(id, std::string("Argument adjustment must be from [-1, 1]"));
    }
    float mindb = ddb_api->volume_get_min_db();
    float voldb = ddb_api->volume_get_db();
    ddb_api->volume_set_db(voldb - mindb * adj);
    return ok_response(id);
}

json command_toggle_mute(int id, json args) {
    DDB_IPC_DEBUG << "Toggling mute." << std::endl;
    int m = ddb_api->audio_is_mute();
    if(m) {
        ddb_api->audio_set_mute(0);
    } else {
        ddb_api->audio_set_mute(1);
    }
    return ok_response(id);
}

json command_get_playpos(int id, json args) {
    DB_playItem_t* cur = ddb_api->streamer_get_playing_track();
    if(!cur) {
        return error_response(id, "Not playing.");
    }
    float dur = ddb_api->pl_get_item_duration(cur);
    float playpos = ddb_api->streamer_get_playpos();
    json resp = json {
        {"status", DDB_IPC_RESPONSE_OK},
        {"data", {
             {"duration", dur},
             {"position", playpos}
         } } };
    if(id) {
        resp["request_id"] = id;
    }
    return resp;
}
json command_seek(int id, json args){
    return error_response(id, std::string("Not implemented"));
}

typedef json (*ipc_command)(int, json);

void handle_message(json message, int socket){
    json response;
    int id = 0;
    if(message.contains("request_id") && message["request_id"].is_number_integer()){
        id = message["request_id"];
    }
    if ( !message.contains("command") || message["command"].type() != json::value_t::string ) {
        DDB_IPC_WARN << "Bad request: `command` field must be present and must be a string." << std::endl;
        response = bad_request_response(id, "`command` field must be present and must be a string.");
        send_response(response, socket);
        return;
    }
    if (!message.contains("args") ) {
        message["args"] = {};
    }
    std::map<std::string, ipc_command>  handlers = {
        {"play", command_play},
        {"pause", command_pause},
        {"play-pause", command_play_pause},
        {"stop", command_stop},
        {"set-volume", command_set_volume},
        {"adjust-volume", command_adjust_volume},
        {"toggle-mute", command_toggle_mute},
        {"seek", command_seek},
        {"get-playpos", command_get_playpos}
    };
    try {
        response = handlers.at(message["command"])(id, message["args"]);
    } catch (std::out_of_range& e) {
        response = error_response(id, std::string("Unknown command"));
    }
    send_response(response, socket);
}


void* listen(void* sockname){
    int i;
    int rc;
    int new_conn, conn_accepted;
    int close_conn;
    char buf[DDB_IPC_MAX_MESSAGE_LENGTH];
    buf[0] = '\0';
    json message;

    pollfd_t open_slot = {
        .fd = -1,
        .events = POLLIN,
        .revents = 0
    };
    for(i=0; i<= DDB_IPC_MAX_CONNECTIONS; i++){
        memcpy(&fds[i], &open_slot, sizeof(pollfd_t) );
    }

    ddb_socket = open_socket((char*) sockname);
    rc = ::listen(ddb_socket, 1);
    fds[0].fd = ddb_socket;

    while (ipc_listening){
        rc = poll(fds, DDB_IPC_MAX_CONNECTIONS + 1, DDB_IPC_POLL_FREQ);
        if (rc < 0 ){
            DDB_IPC_ERR << "Error reading from socket:" << errno << std::endl;
        }
        if (rc == 0) {
            // timed out
            continue;
        }
        if(fds[0].revents & POLLIN){
            // TODO refactor this into its own function
            // there are incoming connections
            while( (new_conn = accept4(fds[0].fd, NULL, NULL, SOCK_NONBLOCK)) >= 0) {
                conn_accepted = 0;
                // find an open slot
                for(i=1; i<= DDB_IPC_MAX_CONNECTIONS; i++){
                    if(fds[i].fd < 0){
                        fds[i].fd = new_conn;
                        DDB_IPC_DEBUG << "Accepted new connection with descriptor " << new_conn << std::endl;
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
            // TODO refactor this into its own function
            if( fds[i].revents & POLLIN ) {
                do {
                    DDB_IPC_DEBUG << "There is data to read on descriptor " << fds[i].fd << std::endl;
                    // zero out the buffer
                    memset(buf, '\0', DDB_IPC_MAX_MESSAGE_LENGTH);
                    rc = recv(fds[i].fd, buf, DDB_IPC_MAX_MESSAGE_LENGTH, 0);
                    if( rc < 0) {
                        if( errno != EWOULDBLOCK ){
                            close_conn = 1;
                        }
                        break;
                    } else if (rc == 0) {
                        close_conn = 1;
                        break;
                    }
                    DDB_IPC_DEBUG << "Received message on descriptor " << fds[i].fd << ":"  << buf;
                    std::istringstream msg(buf);
                    std::string l;
                    while ( std::getline(msg, l) ) {
                        try {
                            message = json::parse(l);
                            handle_message(message, fds[i].fd);
                        } catch (const json::exception& e) {
                            DDB_IPC_WARN << "Message is not valid JSON: " << e.what() << std::endl;
                            send_response( json {
                                    {"status", DDB_IPC_RESPONSE_ERR},
                                    {"response", std::string("Message is not valid JSON: ") + e.what()}
                                    },
                                    fds[i].fd);
                        }
                    }
                } while (1);
                if(close_conn) {
                    ::close(fds[i].fd);
                    DDB_IPC_DEBUG << "Closed connection with descriptor " << fds[i].fd << std::endl;;
                    // free up the slot
                    fds[i].fd = -1;
                }
            }
        }

    }
    for(i=0; i<= DDB_IPC_MAX_CONNECTIONS; i++){
        if(fds[i].fd > -1) {
            ::close(fds[i].fd);
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

// Handle events sent to us by DeaDBeeF

void on_toggle_pause(int p) {
    if(p) {
        broadcast( json { {"event", "paused" }} );
    } else {
        broadcast( json { {"event", "unpaused" }} );
    }
}

void on_track_changed() {
    broadcast( json { {"event", "track-changed"} } );
}

void on_seek(ddb_event_playpos_t* ctx) {
    float dur = ddb_api->pl_get_item_duration(ctx->track);
    broadcast( json { {"event", "seek"},
            {"data", {
                {"duration", dur},
                {"position", ctx->playpos},
            } } } );
    return;
}

void on_config_changed() {
    DDB_IPC_DEBUG << "Config changed..." << std::endl;
}

void on_volume_change() {
    float mindb = ddb_api->volume_get_min_db();
    float vol = 100 * (mindb - ddb_api->volume_get_db())/mindb;
    broadcast( json {
            {"event", "property-change"},
            {"property", "volume"},
            {"value", vol}
            } );
}

int handleMessage(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2){
    switch(id) {
        case DB_EV_PAUSED:
            on_toggle_pause(p1);
            break;
        case DB_EV_SEEKED:
            on_seek( (ddb_event_playpos_t*) ctx);
            break;
        case DB_EV_NEXT:
        case DB_EV_PREV:
        case DB_EV_PLAY_CURRENT:
            on_track_changed();
            break;
        case DB_EV_VOLUMECHANGED:
            on_volume_change();
            break;
        case DB_EV_CONFIGCHANGED:
            on_config_changed();
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
    ddb_api->conf_get_str(DDB_IPC_PROJECT_ID ".socketpath", DDB_IPC_DEFAULT_SOCKET, socket_path, PATH_MAX);
    DDB_IPC_DEBUG << "Opening socket at " << socket_path << std::endl;
    ipc_listening = 1;
    pthread_create(&ipc_thread, NULL, listen, (void*) socket_path);
    return &definition_;
}

extern "C" DB_plugin_t* ddb_ipc_load(DB_functions_t* api) {
    return load(api);
}
