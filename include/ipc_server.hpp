#ifndef DDB_IPC_SERVER_HPP
#define DDB_IPC_SERVER_HPP

#include <sys/poll.h>
#include <sys/socket.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "message.hpp"

namespace ddb_ipc {

typedef struct {
    json message;
    int address;
} addressed_json_t;

using json = nlohmann::json;
class IPCServer {
  public:
    IPCServer(std::string socket_path);
    void send_message(json message, int socket);
    void close_connection(int socket);
    void broadcast(json message);

  private:
    void handle_message(Message m, int socket);
    void handle_message(json message, int socket);

    int accept_connection(int new_conn, pollfd* fds, int n_fds);
    void process_inbox();
    void process_outbox();
    void send_one(json message, int socket);

    bool running;

    std::vector<pollfd> fds;
    std::mutex socket_mutex;

    std::deque<addressed_json_t> inbox;
    std::mutex inbox_mutex;
    std::condition_variable inbox_cv;

    std::deque<addressed_json_t> outbox;
    std::mutex outbox_mutex;
    std::condition_variable outbox_cv;
};

}  // namespace ddb_ipc
#endif
