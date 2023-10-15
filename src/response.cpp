#include "response.hpp"

namespace ddb_ipc {

void to_json(json& j, const ResponseStatus& c) {
    switch (c) {
        case DDB_IPC_RESPONSE_OK:
            j = "OK";
            break;
        case DDB_IPC_RESPONSE_ERR:
            j = "ERROR";
            break;
        case DDB_IPC_RESPONSE_BADQ:
            j = "BAD_REQUEST";
            break;
    }
}


void to_json(json& j, const Response& r) {
    j = r.data;
    if (r.id) {
        int id = r.id.value();
        j["request_id"] = id;
    }
    j["status"] = r.status;
}

Response ok_response(request_id id, json data) {
    return Response(id, DDB_IPC_RESPONSE_OK, data);
};

Response bad_request_response(request_id id, std::string mess) {
    return Response(id, DDB_IPC_RESPONSE_BADQ, {{"message", mess}});
}

Response error_response(request_id id, std::string mess) {
    return Response(id, DDB_IPC_RESPONSE_ERR, {{"message", mess}});
}

}
