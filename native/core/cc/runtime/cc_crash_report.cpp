#include "cc/runtime/cc_crash_report.h"

#include "shared/platform/storage_services.h"

#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <time.h>
#include <sys/system_properties.h>
#include <sys/utsname.h>
#include <unistd.h>

static std::string crashLogTimestamp(void)
{
    time_t raw = time(NULL);
    struct tm localTime;
    localtime_r(&raw, &localTime);
    char text[32] = {};
    strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &localTime);
    return text;
}

static unsigned long crashLogProcessId(void)
{
    return (unsigned long)getpid();
}

static FILE* crashLogOpenForGame(const std::string& timestamp,
    const std::string& saveDirectory, std::string* outFileName)
{
    char fileName[96] = {};
    snprintf(fileName, sizeof(fileName), "DingooPie-crash-%s-%lu.log",
        timestamp.c_str(), crashLogProcessId());
    fileName[sizeof(fileName) - 1] = '\0';
    FILE* fp = NULL;
    if (!saveDirectory.empty() && platformIsPrivateStorageDirectory(saveDirectory))
    {
        fp = platformOpenStorageFile(saveDirectory, fileName, "wb");
    }
    if (!fp)
    {
        std::string logDirectory = platformGetLogDirectory();
        if (!logDirectory.empty())
        {
            fp = platformOpenStorageFile(logDirectory, fileName, "wb");
        }
    }
    if (fp && outFileName) *outFileName = fileName;
    return fp;
}

static std::string crashLogAndroidProperty(const char* name)
{
    char value[PROP_VALUE_MAX] = {};
    return __system_property_get(name, value) > 0 ? value : "";
}

static const char* crashLogAndroidAbi(void)
{
#if defined(__aarch64__)
    return "arm64-v8a";
#elif defined(__arm__)
    return "armeabi-v7a";
#elif defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

static void crashLogWriteUnderline(FILE* fp, char ch, size_t length)
{
    for (size_t index = 0; index < length; ++index) fputc(ch, fp);
    fputc('\n', fp);
}

static const size_t kCrashLogSeparatorWidth = 72;

static void crashLogWriteSection(FILE* fp, const char* title)
{
    fputc('\n', fp);
    fprintf(fp, "%s\n", title);
    crashLogWriteUnderline(fp, '-', kCrashLogSeparatorWidth);
}

static void crashLogWriteField(FILE* fp, const char* key, const char* format, ...)
{
    fprintf(fp, "%-24s : ", key);
    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);
    fputc('\n', fp);
}

static void crashLogWriteAndroidSystem(FILE* fp)
{
    struct utsname kernel = {};
    uname(&kernel);
    crashLogWriteSection(fp, "Android System");
    crashLogWriteField(fp, "android_sdk", "%s", crashLogAndroidProperty("ro.build.version.sdk").c_str());
    crashLogWriteField(fp, "android_release", "%s", crashLogAndroidProperty("ro.build.version.release").c_str());
    crashLogWriteField(fp, "device_manufacturer", "%s", crashLogAndroidProperty("ro.product.manufacturer").c_str());
    crashLogWriteField(fp, "device_model", "%s", crashLogAndroidProperty("ro.product.model").c_str());
    crashLogWriteField(fp, "device_name", "%s", crashLogAndroidProperty("ro.product.device").c_str());
    crashLogWriteField(fp, "process_abi", "%s", crashLogAndroidAbi());
    crashLogWriteField(fp, "kernel", "%s %s", kernel.sysname, kernel.release);
    crashLogWriteField(fp, "build_fingerprint", "%s", crashLogAndroidProperty("ro.build.fingerprint").c_str());
}

bool crashLogWriteCcFailure(
    const CcCrashLogContext& context,
    std::string* outFileName)
{
    std::string timestamp = crashLogTimestamp();
    FILE* fp = crashLogOpenForGame(timestamp,
        context.saveDirectory ? context.saveDirectory : "", outFileName);
    if (!fp)
    {
        return false;
    }

    setvbuf(fp, NULL, _IONBF, 0);
    const uint32_t* registers = context.registers;
    uint32_t pc = registers ? registers[15] : 0;
    uint32_t lr = registers ? registers[14] : 0;
    uint32_t sp = registers ? registers[13] : 0;

    fprintf(fp, "DingooPie Crash Report\n");
    crashLogWriteUnderline(fp, '=', kCrashLogSeparatorWidth);

    crashLogWriteSection(fp, "Summary");
    crashLogWriteField(fp, "timestamp", "%s", timestamp.c_str());
    crashLogWriteField(fp, "kind", "cc-runtime-failure");
    crashLogWriteField(fp, "error", "%s", context.error ? context.error : "");
    crashLogWriteField(fp, "backend", "arm32_interpreter");
    crashLogWriteAndroidSystem(fp);

    crashLogWriteSection(fp, "Crash Location");
    crashLogWriteField(fp, "pc", "0x%08x", pc);
    crashLogWriteField(fp, "lr", "0x%08x", lr);
    crashLogWriteField(fp, "sp", "0x%08x", sp);
    crashLogWriteField(fp, "cpsr", "0x%08x", context.cpsr);
    crashLogWriteField(fp, "unsupported_instruction", "0x%08x",
        context.unsupportedInstruction);
    crashLogWriteField(fp, "unsupported_pc", "0x%08x", context.unsupportedPc);
    crashLogWriteField(fp, "fault_address", "0x%08x", context.faultAddress);
    crashLogWriteField(fp, "fault_size", "%u", context.faultSize);
    crashLogWriteField(fp, "fault_access", "%s", context.faultFetch ? "fetch" :
        (context.faultWrite ? "write" : "read"));

    crashLogWriteSection(fp, "ARM Registers");
    for (uint32_t row = 0; row < 4; ++row)
    {
        for (uint32_t column = 0; column < 4; ++column)
        {
            uint32_t index = row * 4 + column;
            fprintf(fp, "r%-2u=0x%08x%s", index,
                registers ? registers[index] : 0,
                column == 3 ? "\n" : "  ");
        }
    }

    crashLogWriteSection(fp, "CC Runtime");
    crashLogWriteField(fp, "game_path", "%s", context.gamePath ? context.gamePath : "");
    crashLogWriteField(fp, "game_sha256", "%s", context.gameSha256 ? context.gameSha256 : "");
    crashLogWriteField(fp, "instructions", "%llu",
        (unsigned long long)context.instructions);
    crashLogWriteField(fp, "import_calls", "%u", context.importCalls);
    crashLogWriteField(fp, "unknown_imports", "%u", context.unknownImports);
    crashLogWriteField(fp, "frames_submitted", "%u", context.framesSubmitted);
    crashLogWriteField(fp, "tasks_created", "%u", context.tasksCreated);
    crashLogWriteField(fp, "last_import", "%s", context.lastImport ? context.lastImport : "");
    crashLogWriteField(fp, "last_import_pc", "0x%08x", context.lastImportPc);
    crashLogWriteField(fp, "last_import_return", "0x%08x", context.lastImportReturnAddress);
    crashLogWriteField(fp, "failed_task_index", "%u", context.failedTaskIndex);
    crashLogWriteField(fp, "failed_task_entry", "0x%08x", context.failedTaskEntry);
    crashLogWriteField(fp, "failed_task_stack", "0x%08x", context.failedTaskStack);
    crashLogWriteField(fp, "failed_task_priority", "%u", context.failedTaskPriority);
    crashLogWriteField(fp, "failed_task_delay_ticks", "%u", context.failedTaskDelayTicks);

    fclose(fp);
    return true;
}
