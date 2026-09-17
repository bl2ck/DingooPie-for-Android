#include "Common/File/FileUtil.h"

#include <sys/stat.h>

namespace File {

bool Exists(const Path& path)
{
    struct stat info;
    return stat(path.c_str(), &info) == 0;
}

}
