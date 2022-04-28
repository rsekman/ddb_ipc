#ifndef DDB_IPC_PROPERTIES_HPP
#define DDB_IPC_PROPERTIES_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <deadbeef/deadbeef.h>

namespace ddb_ipc {

//extern DB_functions_t* ddb_api;

typedef json (*ipc_property_getter)();
typedef void (*ipc_property_setter)(json);

json get_property_volume();
json get_property_mute();
json get_property_shuffle();
json get_property_repeat();
json command_set_property(int id, json args);
json property_as_json(std::string prop);
json command_get_property(int id, json args);

}

#endif
