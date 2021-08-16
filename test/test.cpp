#include <iostream>
#include <dlfcn.h>
#include <unistd.h>

#include <deadbeef/deadbeef.h>
#include <nlohmann/json.hpp>

#include "ddb_mock.hpp"

int main(void) {
    void* ddb_ipc_lib = dlopen("build/ddb_ipc.so", RTLD_LAZY);
    if (ddb_ipc_lib == NULL) {
        std::cerr << "Could not load ddb_ipc.so:" << dlerror() << std::endl;
        std::cerr << dlerror() << std::endl;
        return 1;
    }
    typedef DB_plugin_t* (*ddb_ipc_load_t)(DB_functions_t*);
    ddb_ipc_load_t ddb_ipc_load = (ddb_ipc_load_t) dlsym(ddb_ipc_lib, "ddb_ipc_load");
    if (ddb_ipc_load == NULL) {
        std::cerr << "Could not load entry point:" << dlerror() << std::endl;
        return 1;
    } else {
        // ddb_ipc_lib = (ddb_ipc_load_t) ddb_ipc_lib;
    }
    DB_functions_t ddb_api = ddb_ipc::get_ddb_mock_api();
    DB_plugin_t* plugin = ddb_ipc_load( &ddb_api );
    std::cout << "Loaded plugin " << plugin->name << std::endl;
    sleep(20);
    plugin->stop();
    return 0;
}
