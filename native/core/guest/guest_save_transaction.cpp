#include "guest/guest_save_transaction.h"
#include "platform_services.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static const char* kSaveTransactionBackupSuffix =
    ".dingoopie.transaction-v1.backup";
static const char* kSaveTransactionMarkerSuffix =
    ".dingoopie.transaction-v1.pending";

static bool modeTruncatesFile(const char* mode)
{
    return mode && strchr(mode, 'w') && !strchr(mode, 'a');
}

static std::string sidecarName(const char* name, const char* suffix)
{
    return std::string(name ? name : "") + suffix;
}

static bool syncFile(FILE* file)
{
    if (!file || fflush(file) != 0)
    {
        return false;
    }
    int descriptor = fileno(file);
    return descriptor < 0 || fsync(descriptor) == 0 ||
        errno == EINVAL || errno == ENOTSUP;
}

static bool copyFile(const std::string& directory,
    const char* sourceName, const char* destinationName)
{
    FILE* source = platformAndroidOpenSaveFile(directory, sourceName, "rb");
    if (!source)
    {
        return false;
    }
    FILE* destination = platformAndroidOpenSaveFile(
        directory, destinationName, "wb");
    if (!destination)
    {
        fclose(source);
        return false;
    }

    bool ok = true;
    uint8_t buffer[64 * 1024];
    while (ok)
    {
        size_t bytesRead = fread(buffer, 1, sizeof(buffer), source);
        if (bytesRead > 0 && fwrite(buffer, 1, bytesRead, destination) != bytesRead)
        {
            ok = false;
        }
        if (bytesRead < sizeof(buffer))
        {
            if (ferror(source))
            {
                ok = false;
            }
            break;
        }
    }
    if (ok)
    {
        ok = syncFile(destination);
    }
    if (fclose(destination) != 0)
    {
        ok = false;
    }
    fclose(source);
    return ok;
}

static bool writeMarker(const std::string& directory, const char* name, char state)
{
    std::string markerName = sidecarName(name, kSaveTransactionMarkerSuffix);
    FILE* marker = platformAndroidOpenSaveFile(directory, markerName, "wb");
    if (!marker)
    {
        return false;
    }
    bool ok = fwrite(&state, 1, 1, marker) == 1 && syncFile(marker);
    if (fclose(marker) != 0)
    {
        ok = false;
    }
    return ok;
}

static char readMarker(const std::string& directory, const char* name)
{
    std::string markerName = sidecarName(name, kSaveTransactionMarkerSuffix);
    FILE* marker = platformAndroidOpenSaveFile(directory, markerName, "rb");
    if (!marker)
    {
        return 0;
    }
    char state = 0;
    if (fread(&state, 1, 1, marker) != 1)
    {
        state = 0;
    }
    fclose(marker);
    return state;
}

static bool clearSidecars(const std::string& directory, const char* name)
{
    std::string backupName = sidecarName(name, kSaveTransactionBackupSuffix);
    std::string markerName = sidecarName(name, kSaveTransactionMarkerSuffix);
    if (!platformAndroidDeleteSaveFile(directory, backupName))
    {
        return false;
    }
    return platformAndroidDeleteSaveFile(directory, markerName);
}

static bool recoverTransaction(const std::string& directory, const char* name)
{
    std::string backupName = sidecarName(name, kSaveTransactionBackupSuffix);
    char state = readMarker(directory, name);
    if (!state)
    {
        return true;
    }
    if (state == 'C')
    {
        clearSidecars(directory, name);
        return true;
    }

    bool restored = state == '1' ?
        copyFile(directory, backupName.c_str(), name) :
        platformAndroidDeleteSaveFile(directory, name);
    if (!restored)
    {
        printf("fsys: save transaction recovery failed name=%s state=%c\n", name, state);
        return false;
    }
    if (writeMarker(directory, name, 'C'))
    {
        clearSidecars(directory, name);
    }
    printf("fsys: recovered interrupted save name=%s\n", name);
    return true;
}

static FILE* beginTransaction(const std::string& directory,
    const char* name, const char* mode)
{
    if (!recoverTransaction(directory, name))
    {
        return NULL;
    }

    std::string backupName = sidecarName(name, kSaveTransactionBackupSuffix);
    FILE* original = platformAndroidOpenSaveFile(directory, name, "rb");
    bool hadOriginal = original != NULL;
    if (original)
    {
        fclose(original);
        if (!copyFile(directory, name, backupName.c_str()))
        {
            return NULL;
        }
    }
    else
    {
        platformAndroidDeleteSaveFile(directory, backupName);
    }

    if (!writeMarker(directory, name, hadOriginal ? '1' : '0'))
    {
        platformAndroidDeleteSaveFile(directory, backupName);
        return NULL;
    }
    FILE* file = platformAndroidOpenSaveFile(directory, name, mode);
    if (!file)
    {
        recoverTransaction(directory, name);
    }
    return file;
}

static bool commitTransaction(const std::string& directory, const char* name)
{
    if (!writeMarker(directory, name, 'C'))
    {
        return false;
    }
    clearSidecars(directory, name);
    return true;
}

FILE* guestSaveOpenFile(const std::string& directory, const char* name,
    const char* mode, bool* transactional)
{
    bool useTransaction = modeTruncatesFile(mode);
    if (transactional)
    {
        *transactional = useTransaction;
    }
    return useTransaction ? beginTransaction(directory, name, mode) :
        (recoverTransaction(directory, name) ?
            platformAndroidOpenSaveFile(directory, name, mode) : NULL);
}

int guestSaveCloseFile(const std::string& directory, const char* name,
    FILE* file, bool transactional)
{
    bool transactionOk = !transactional || syncFile(file);
    int result = fclose(file);
    if (transactional && transactionOk && result == 0 &&
        !commitTransaction(directory, name))
    {
        return -1;
    }
    return result;
}
