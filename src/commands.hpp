#ifndef DDB_IPC_COMMANDS_HPP
#define DDB_IPC_COMMANDS_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <deadbeef/deadbeef.h>

#define COMMAND(n, argt) \
    json command_ ## n(int id, argt &args); \
    json command_ ## n (int id, json args) { \
        argt a = args; \
        return command_ ## n(id, a); \
    } \
    json command_ ## n(int id, argt &args) {

namespace ddb_ipc {

class Argument { };

typedef json (*ipc_command)(int, json);

json call_command(std::string command, int id, json args);

}

#endif
