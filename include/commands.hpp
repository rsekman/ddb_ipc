#ifndef DDB_IPC_COMMANDS_HPP
#define DDB_IPC_COMMANDS_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <optional>

#include "argument.hpp"

typedef std::optional<int> request_id;

#define COMMAND(n, argt)                         \
    json command_##n(request_id id, argt& args); \
    json command_##n(request_id id, json args) { \
        argt a = args;                           \
        return command_##n(id, a);               \
    }                                            \
    json command_##n(request_id id, argt& args)
namespace ddb_ipc {

typedef json (*ipc_command)(request_id, json);

json call_command(std::string command, request_id id, json args);

}  // namespace ddb_ipc

#endif
