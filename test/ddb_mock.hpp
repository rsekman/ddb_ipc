#include <deadbeef/deadbeef.h>
#include "../src/defs.hpp"

namespace ddb_ipc{

DB_functions_t get_ddb_mock_api();
void conf_get_str(const char* key, const char* def, char* buffer, int buffer_size);

}
