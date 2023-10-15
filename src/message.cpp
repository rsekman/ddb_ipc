#include "message.hpp"

namespace ddb_ipc {

std::string const prettify_json_exception(std::string prefix, json::exception &e) {
    std::string w = e.what();
    auto idx = w.find("] ");
    if (idx == std::string::npos) {
        w = w.substr(idx + 1);
    }
    return prefix + ": " + w;
}

void from_json(const json &j, Message &m) {
    std::string command;
    if (!j.contains("command") || j["command"].type() != json::value_t::string) {
        throw Exception("`command` field must be present and must be a string.");
    } else {
        command = j["command"];
    }
    request_id id{};
    if (j.contains("request_id") && j["request_id"].is_number_integer()) {
        id = j["request_id"];
    }
    json args = j.contains("args") ? j["args"] : json {} ;
    m = Message(id, command, args);
}

}  // namespace ddb_ipc
