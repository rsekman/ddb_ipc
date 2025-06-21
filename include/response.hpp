#ifndef DDB_IPC_RESPONSE_HPP
#define DDB_IPC_RESPONSE_HPP

#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <optional>

namespace ddb_ipc {

typedef std::optional<int> request_id;
enum ResponseStatus {
    DDB_IPC_RESPONSE_OK,
    DDB_IPC_RESPONSE_ERR,
    DDB_IPC_RESPONSE_BADQ,
};

void to_json(json& j, const ResponseStatus& c);
class Response {
  public:
    request_id id;
    ResponseStatus status;
    json data;
    Response(request_id _id, ResponseStatus _status, json _data) :
        id(_id), status(_status), data(_data) {};
};
void to_json(json& j, const Response& r);

Response ok_response(request_id, json data = {});
Response bad_request_response(request_id id, std::string mess);
Response error_response(request_id id, std::string mess);

}  // namespace ddb_ipc

#endif
