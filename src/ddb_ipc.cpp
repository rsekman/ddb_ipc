#include <stdlib.h>
#include <iostream>
#include <sys/socket.h>

#include <deadbeef/deadbeef.h>
#include <nlohmann/json.hpp>

#include "defs.hpp"
#include "ddb_ipc.hpp"


namespace ddb_ipc{

const char Plugin::configDialog_ [] =
    "property \"Socket location\" entry " DDB_IPC_PROJECT_ID ".socketpath \"" DDB_IPC_DEFAULT_SOCKET "\" ;\n";

DB_plugin_t Plugin::definition_;

DB_plugin_t* Plugin::load(DB_functions_t* api) {
    ddb_api = api;
    init(api);
    return &definition_;
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
