#include <deadbeef/deadbeef.h>
#include <string.h>

#include "../src/defs.hpp"
#include "ddb_mock.hpp"

namespace ddb_ipc{

DB_functions_t get_ddb_mock_api(){
    DB_functions_t ddb_api;
    ddb_api.conf_get_str = &conf_get_str;
    return ddb_api;
}

void conf_get_str(const char* key, const char* def, char* buffer, int buffer_size){
    if (strcmp(key, DDB_IPC_PROJECT_ID ".socketpath") == 0) {
        strncpy (buffer, DDB_IPC_DEFAULT_SOCKET, buffer_size);
    }
}
}
