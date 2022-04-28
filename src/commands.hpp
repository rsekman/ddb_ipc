#ifndef DDB_IPC_COMMANDS_HPP
#define DDB_IPC_COMMANDS_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <deadbeef/deadbeef.h>

namespace ddb_ipc {

// extern DB_functions_t* ddb_api;

typedef json (*ipc_command)(int, json);


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
