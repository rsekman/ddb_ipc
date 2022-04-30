#ifndef DDB_IPC_COMMANDS_HPP
#define DDB_IPC_COMMANDS_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <deadbeef/deadbeef.h>

namespace ddb_ipc {

typedef json (*ipc_command)(int, json);
enum arg_type {
    ARG_POLYMORPHIC,
    ARG_NUMBER,
    ARG_INT,
    ARG_STRING,
};
typedef struct arg_s {
    bool mandatory;
    arg_type type;
} arg_t;
typedef std::map<std::string, arg_t> arg_schema;

extern std::map<std::string, ipc_command> commands;

void validate_arguments(arg_schema schema, json args);

json command_play(int id, json args);
json command_pause(int id, json args);
json command_play_pause(int id, json args);
json command_stop(int id, json args);
json command_prev(int id, json args);
json command_next(int id, json args);
json command_set_volume(int id, json args);
json command_adjust_volume(int id, json args);
json command_toggle_mute(int id, json args);
json command_get_playpos(int id, json args);
json command_seek(int id, json args);

}

#endif
