#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#include <linux/limits.h>

#include <deadbeef/deadbeef.h>
#include <nlohmann/json.hpp>

namespace ddb_ipc{

DB_functions_t* ddb_api;

class Plugin{
    public:
        Plugin();
        static DB_plugin_t* load(DB_functions_t* api);
    private:
        static void init (DB_functions_t* api);
        static int start();
        static int stop();
        static int connect();
        static int disconnect();
        static int handleMessage(uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2);
        static DB_plugin_t definition_;
        static const char configDialog_ [];
        static int open_socket(char* socketpath);
        static int ddb_socket;
        static char socket_path [PATH_MAX];
        static void* listen(void* sock);
        static int ipc_listening;
        static pthread_t ipc_thread;
};

}
