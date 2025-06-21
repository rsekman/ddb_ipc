#ifndef DDB_IPC_PROPERTIES_HPP
#define DDB_IPC_PROPERTIES_HPP

#include <nlohmann/json.hpp>
#include <set>
using json = nlohmann::json;

#include <deadbeef/deadbeef.h>

#include "commands.hpp"

namespace ddb_ipc {

// extern DB_functions_t* ddb_api;

typedef json (*ipc_property_getter)();
typedef void (*ipc_property_setter)(json);

extern std::map<int, std::set<std::string>> observers;
extern std::map<std::string, ipc_property_getter> getters;
extern std::map<std::string, ipc_property_setter> setters;

json get_property_volume();
json get_property_mute();
json get_property_shuffle();
json get_property_repeat();

json command_get_property(request_id id, json args);
json command_set_property(request_id id, json args);

json property_as_json(std::string prop);

json command_observe_property(request_id id, json args);

}  // namespace ddb_ipc

#endif
