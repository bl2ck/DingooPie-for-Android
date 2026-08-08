#include "runtime/debug_log.h"
#include "platform_services.h"

#include <stdio.h>
#include <string>
#include <time.h>
#include <unistd.h>

static FILE* g_debugLogFile = NULL;

static void debugLogTimestamp(char* out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return;
    }

    time_t raw = time(NULL);
    struct tm localTime;
    localtime_r(&raw, &localTime);

    strftime(out, outSize, "%Y%m%d-%H%M%S", &localTime);
    out[outSize - 1] = '\0';
}

bool debugLogOpen(void)
{
    if (!g_debugLogFile)
    {
        char timestamp[32] = {};
        debugLogTimestamp(timestamp, sizeof(timestamp));

        std::string logDirectory = platformAndroidGetLogDirectory();
        if (logDirectory.empty())
        {
            return false;
        }
        char logPath[256] = {};
        snprintf(logPath, sizeof(logPath),
            "%s/DingooPie-debug-%s-%lu.log", logDirectory.c_str(),
            timestamp, (unsigned long)getpid());
        logPath[sizeof(logPath) - 1] = '\0';

        g_debugLogFile = fopen(logPath, "w");
        if (g_debugLogFile)
        {
            setvbuf(g_debugLogFile, NULL, _IONBF, 0);
        }
    }
    return g_debugLogFile != NULL;
}

FILE* debugLogFile(void)
{
    if (!g_debugLogFile)
    {
        debugLogOpen();
    }
    return g_debugLogFile ? g_debugLogFile : stdout;
}
