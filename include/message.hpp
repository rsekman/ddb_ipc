#ifndef DDB_IPC_MESSAGE_HPP
#define DDB_IPC_MESSAGE_HPP

#include <nlohmann/json.hpp>
#include <string>
using json = nlohmann::json;
#include <optional>

#include "argument.hpp"

namespace ddb_ipc {

typedef std::optional<int> request_id;
class Message {
  public:
    request_id id;
    std::string command;
    json args;
    Message() : id({}), command(""), args({}) {};
    Message(request_id _id, std::string _command, json _args) :
        id(_id), command(_command), args(_args) {};
};
void from_json(const json &j, Message &m);

std::string const prettify_json_exception(
    std::string prefix, json::exception &e
);
class Exception : std::exception {
  protected:
    std::string msg;

  public:
    Exception() : msg("") {};
    Exception(std::string _msg) : msg(_msg) {};
    const char *what() const noexcept { return msg.c_str(); }
};
class MissingArgumentError : Exception {
  public:
    MissingArgumentError(json::exception &e) {
        msg = prettify_json_exception("Missing argument", e);
    }
};
class TypeError : Exception {
  public:
    TypeError(std::string msg) : Exception(msg) {};
    TypeError(json::exception &e) {
        msg = prettify_json_exception("Type error", e);
    }
};

}  // namespace ddb_ipc
#endif
