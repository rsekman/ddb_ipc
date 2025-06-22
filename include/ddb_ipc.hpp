#ifndef DDB_IPC_PROJECT_ID

#define DDB_IPC_VERSION_MAJOR 0
#define DDB_IPC_VERSION_MINOR 1
#define DDB_IPC_PROJECT_ID "ddb_ipc"
#define DDB_IPC_PROJECT_NAME "DeaDBeef IPC"
#define DDB_IPC_PROJECT_DESC "Provides socket-based IPC using JSON messages."
#define DDB_IPC_PROJECT_URL "https://github.com/rsekman/ddb-ipc"
#define DDB_IPC_DEFAULT_SOCKET "/tmp/ddb_socket"
#define DDB_IPC_POLL_FREQ 1000      // Socket polling frequency in ms
#define DDB_IPC_WRITE_TIMEOUT 5000  // Timeout for sending messages
#define DDB_IPC_MAX_PACKET_LENGTH 4096
#define DDB_IPC_MAX_CONNECTIONS 15
#define DDB_IPC_LICENSE_TEXT                                                 \
    "Copyright 2021 Robin Ekman\n"                                           \
    "\n"                                                                     \
    "This program is free software: you can redistribute it and/or modify\n" \
    "it under the terms of the GNU General Public License as published by\n" \
    "the Free Software Foundation, either version 3 of the License, or\n"    \
    "(at your option) any later version.\n"                                  \
    "\n"                                                                     \
    "This program is distributed in the hope that it will be useful,\n"      \
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"       \
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"        \
    "GNU General Public License for more details.\n"                         \
    "\n"                                                                     \
    "You should have received a copy of the GNU General Public License\n"    \
    "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n"

#define DDB_IPC_DEFAULT_FORMAT "%artist% - %title%"

// clang-format off
#include <deadbeef/deadbeef.h>
#include <deadbeef/artwork.h>
// clang-format on
#include <linux/limits.h>
#include <pthread.h>
#include <spdlog/logger.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace ddb_ipc {

extern DB_functions_t* ddb_api;
extern ddb_artwork_plugin_t* ddb_artwork;

void send_response(json msg, int socket);

std::shared_ptr<spdlog::logger> get_logger();

}  // namespace ddb_ipc
#endif
