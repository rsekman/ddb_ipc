#ifndef DDB_IPC_ARGUMENT_HPP
#define DDB_IPC_ARGUMENT_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <optional>

namespace ddb_ipc {

class Argument {};
void from_json(const json &j, Argument &a);

}

#endif
