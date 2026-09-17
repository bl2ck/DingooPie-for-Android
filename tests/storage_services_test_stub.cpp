#include "shared/platform/storage_services.h"

FILE* platformOpenFile(const std::string& path, const char* mode)
{
    return mode ? fopen(path.c_str(), mode) : NULL;
}
