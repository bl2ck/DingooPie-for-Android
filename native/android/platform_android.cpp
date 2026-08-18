#include "shared/platform/storage_services.h"
#include "shared/platform/lifecycle_services.h"
#include "shared/platform/automation_services.h"
#include "shared/platform/external_launch_services.h"
#include "frontend/shell/frontend_shell.h"
#include "shared/services/guest_filesystem.h"
#include "app/hle/app_hle.h"
#include "app/save/app_save_state.h"
#include "jni_local_ref.h"

#include <SDL_system.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* kAndroidContentGamePrefix = "android-content://";

extern "C" JNIEXPORT void JNICALL
Java_com_dingoopie_android_DingooPieActivity_nativeSetAppBackgrounded(
    JNIEnv*, jclass, jboolean backgrounded)
{
    frontendNotifyAndroidBackground(backgrounded == JNI_TRUE);
}

static bool isAndroidContentGamePath(const std::string& path)
{
    return path.compare(0, strlen(kAndroidContentGamePrefix), kAndroidContentGamePrefix) == 0;
}
static int openAndroidContentGameDescriptor(const std::string& path)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return -1;
    }

    int descriptor = -1;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "openGameFileDescriptor", "(Ljava/lang/String;)I") : NULL;
    jstring pathText = method ? env->NewStringUTF(path.c_str()) : NULL;
    if (pathText)
    {
        descriptor = (int)env->CallIntMethod(activity, method, pathText);
        env->DeleteLocalRef(pathText);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        descriptor = -1;
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
    return descriptor;
}

static int openAndroidGameSiblingDescriptor(const std::string& gamePath,
    const std::string& fileName)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity || gamePath.empty() || fileName.empty())
    {
        return -1;
    }

    int descriptor = -1;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "openSiblingGameFileDescriptor", "(Ljava/lang/String;Ljava/lang/String;)I") : NULL;
    jstring pathText = method ? env->NewStringUTF(gamePath.c_str()) : NULL;
    jstring fileNameText = pathText ? env->NewStringUTF(fileName.c_str()) : NULL;
    if (pathText && fileNameText)
    {
        descriptor = (int)env->CallIntMethod(activity, method, pathText, fileNameText);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        descriptor = -1;
    }
    if (fileNameText) env->DeleteLocalRef(fileNameText);
    if (pathText) env->DeleteLocalRef(pathText);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return descriptor;
}

static std::string consumeAndroidActivityString(const char* methodName)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return std::string();
    }

    std::string result;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        methodName, "()Ljava/lang/String;") : NULL;
    jstring pathText = method ? (jstring)env->CallObjectMethod(activity, method) : NULL;
    if (!env->ExceptionCheck() && pathText)
    {
        const char* chars = env->GetStringUTFChars(pathText, NULL);
        if (chars)
        {
            result.assign(chars);
            env->ReleaseStringUTFChars(pathText, chars);
        }
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        result.clear();
    }
    if (pathText) env->DeleteLocalRef(pathText);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return result;
}

std::string platformConsumeCheatManagerAutomationGamePath(void)
{
    return consumeAndroidActivityString("consumeCheatManagerAutomationGamePath");
}

std::string platformConsumeGameAutomationPath(void)
{
    return consumeAndroidActivityString("consumeGameAutomationPath");
}

std::string platformConsumeExternalGameLaunchPath(void)
{
    return consumeAndroidActivityString("consumeExternalGameLaunchPath");
}

bool platformConsumeAudioValidationAutomationEnabled(void)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return false;
    }

    bool enabled = false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "consumeAudioValidationAutomationEnabled", "()Z") : NULL;
    if (method)
    {
        enabled = env->CallBooleanMethod(activity, method) == JNI_TRUE;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        enabled = false;
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
    return enabled;
}

void platformRequestApplicationExit(void)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return;
    }

    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "requestApplicationExitFromNative", "()V") : NULL;
    if (method)
    {
        env->CallVoidMethod(activity, method);
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    if (activityClass)
    {
        env->DeleteLocalRef(activityClass);
    }
}

std::string platformGetAppSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return std::string();
    }

    std::string result;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "getGameSaveDirectory",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") : NULL;
    jstring path = method ? env->NewStringUTF(gamePath.c_str()) : NULL;
    jstring identity = path ? env->NewStringUTF(gameIdentity.c_str()) : NULL;
    jstring directory = identity ? (jstring)env->CallObjectMethod(
        activity, method, path, identity) : NULL;
    if (!env->ExceptionCheck() && directory)
    {
        const char* chars = env->GetStringUTFChars(directory, NULL);
        if (chars)
        {
            result.assign(chars);
            env->ReleaseStringUTFChars(directory, chars);
        }
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        result.clear();
    }
    if (directory) env->DeleteLocalRef(directory);
    if (identity) env->DeleteLocalRef(identity);
    if (path) env->DeleteLocalRef(path);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return result;
}

std::string platformGetCcSaveDirectory(const std::string& gamePath,
    const std::string& gameIdentity)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return std::string();
    }

    std::string result;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "getCcGameSaveDirectory",
        "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;") : NULL;
    jstring identity = method ? env->NewStringUTF(gamePath.c_str()) : NULL;
    jstring saveIdentity = identity ? env->NewStringUTF(gameIdentity.c_str()) : NULL;
    jstring directory = saveIdentity ? (jstring)env->CallObjectMethod(
        activity, method, identity, saveIdentity) : NULL;
    if (!env->ExceptionCheck() && directory)
    {
        const char* chars = env->GetStringUTFChars(directory, NULL);
        if (chars)
        {
            result.assign(chars);
            env->ReleaseStringUTFChars(directory, chars);
        }
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        result.clear();
    }
    if (directory) env->DeleteLocalRef(directory);
    if (saveIdentity) env->DeleteLocalRef(saveIdentity);
    if (identity) env->DeleteLocalRef(identity);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return result;
}

std::string platformGetLogDirectory(void)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        return std::string();
    }

    std::string result;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "getPrivateLogDirectory", "()Ljava/lang/String;") : NULL;
    jstring directory = method ? (jstring)env->CallObjectMethod(activity, method) : NULL;
    if (!env->ExceptionCheck() && directory)
    {
        const char* chars = env->GetStringUTFChars(directory, NULL);
        if (chars)
        {
            result.assign(chars);
            env->ReleaseStringUTFChars(directory, chars);
        }
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        result.clear();
    }
    if (directory) env->DeleteLocalRef(directory);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return result;
}

bool platformIsPrivateStorageDirectory(const std::string& directoryUri)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity || directoryUri.empty())
    {
        return false;
    }

    bool result = false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "isPrivateGameSaveDirectory", "(Ljava/lang/String;)Z") : NULL;
    jstring directory = method ? env->NewStringUTF(directoryUri.c_str()) : NULL;
    if (directory)
    {
        result = env->CallBooleanMethod(activity, method, directory) == JNI_TRUE;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        result = false;
    }
    if (directory) env->DeleteLocalRef(directory);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return result;
}

FILE* platformOpenStorageFile(const std::string& directoryUri,
    const std::string& fileName, const char* mode)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity || directoryUri.empty() || fileName.empty())
    {
        return NULL;
    }

    char normalizedMode[16] = {};
    const char* effectiveMode = mode && mode[0] ? mode : "rb";
    size_t modeLength = 0;
    bool modeChanged = false;
    for (const char* current = effectiveMode; *current; ++current)
    {
        if (*current == 's')
        {
            modeChanged = true;
            continue;
        }
        if (modeLength + 1 >= sizeof(normalizedMode))
        {
            return NULL;
        }
        normalizedMode[modeLength++] = *current;
    }
    normalizedMode[modeLength] = 0;
    if (modeChanged)
    {
        if (modeLength == 0)
        {
            return NULL;
        }
        effectiveMode = normalizedMode;
    }

    FILE* file = NULL;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "openGameSaveFileDescriptor", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I") : NULL;
    jstring directoryText = method ? env->NewStringUTF(directoryUri.c_str()) : NULL;
    jstring fileNameText = directoryText ? env->NewStringUTF(fileName.c_str()) : NULL;
    jstring modeText = fileNameText ? env->NewStringUTF(effectiveMode) : NULL;
    jint descriptor = (directoryText && fileNameText && modeText) ?
        env->CallIntMethod(activity, method, directoryText, fileNameText, modeText) : -1;
    if (!env->ExceptionCheck() && descriptor >= 0)
    {
        file = fdopen((int)descriptor, effectiveMode);
        if (!file)
        {
            close((int)descriptor);
        }
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    if (modeText) env->DeleteLocalRef(modeText);
    if (fileNameText) env->DeleteLocalRef(fileNameText);
    if (directoryText) env->DeleteLocalRef(directoryText);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return file;
}

bool platformDeleteStorageFile(const std::string& directoryUri,
    const std::string& fileName)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity || directoryUri.empty() || fileName.empty())
    {
        return false;
    }

    bool deleted = false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "deleteGameSaveFile", "(Ljava/lang/String;Ljava/lang/String;)Z") : NULL;
    jstring directoryText = method ? env->NewStringUTF(directoryUri.c_str()) : NULL;
    jstring fileNameText = directoryText ? env->NewStringUTF(fileName.c_str()) : NULL;
    if (directoryText && fileNameText)
    {
        deleted = env->CallBooleanMethod(activity, method, directoryText, fileNameText) == JNI_TRUE;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        deleted = false;
    }
    if (fileNameText) env->DeleteLocalRef(fileNameText);
    if (directoryText) env->DeleteLocalRef(directoryText);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return deleted;
}

uint64_t platformGetStorageFileModifiedTime(const std::string& directoryUri,
    const std::string& fileName)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity || directoryUri.empty() || fileName.empty())
    {
        return 0;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "getGameSaveFileModifiedTime", "(Ljava/lang/String;Ljava/lang/String;)J") : NULL;
    jstring directoryText = method ? env->NewStringUTF(directoryUri.c_str()) : NULL;
    jstring fileNameText = directoryText ? env->NewStringUTF(fileName.c_str()) : NULL;
    jlong modifiedTime = directoryText && fileNameText ?
        env->CallLongMethod(activity, method, directoryText, fileNameText) : 0;
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        modifiedTime = 0;
    }
    if (fileNameText) env->DeleteLocalRef(fileNameText);
    if (directoryText) env->DeleteLocalRef(directoryText);
    if (activityClass) env->DeleteLocalRef(activityClass);
    return modifiedTime > 0 ? (uint64_t)modifiedTime : 0;
}

static bool writeSaveAutomationFile(const std::string& directory,
    const char* name, const char* mode, const uint8_t* data, size_t size)
{
    FILE* file = platformOpenStorageFile(directory, name, mode);
    if (!file)
    {
        return false;
    }
    bool ok = fwrite(data, 1, size, file) == size && fflush(file) == 0;
    fclose(file);
    return ok;
}

static bool readSaveAutomationFile(const std::string& directory,
    const char* name, const uint8_t* expected, size_t size)
{
    FILE* file = platformOpenStorageFile(directory, name, "rb");
    if (!file)
    {
        return false;
    }
    uint8_t actual[16] = {};
    bool ok = size <= sizeof(actual) && fread(actual, 1, size, file) == size &&
        fgetc(file) == EOF && memcmp(actual, expected, size) == 0;
    fclose(file);
    return ok;
}

static bool saveAutomationFileMissing(const std::string& directory, const char* name)
{
    FILE* file = platformOpenStorageFile(directory, name, "rb");
    if (!file)
    {
        return true;
    }
    fclose(file);
    return false;
}

static bool writeGuestSaveAutomationFile(const char* name,
    const char* mode, const uint8_t* data, size_t size)
{
    uint32_t stream = fsys_fopen(name, mode);
    if (!stream)
    {
        return false;
    }
    bool ok = fsys_fwrite((void*)data, 1, (uint32_t)size, stream) == size;
    return fsys_fclose(stream) == 0 && ok;
}

static bool readGuestSaveAutomationFile(const char* name,
    const uint8_t* expected, size_t size)
{
    uint32_t stream = fsys_fopen(name, "rb");
    if (!stream)
    {
        return false;
    }
    uint8_t actual[32] = {};
    bool ok = size <= sizeof(actual) &&
        vm_fread(actual, 1, (uint32_t)size, stream) == size &&
        vm_fread(actual + size, 1, 1, stream) == 0 &&
        memcmp(actual, expected, size) == 0;
    fsys_fclose(stream);
    return ok;
}

static bool runGuestSaveTransactionAutomation(const std::string& directory)
{
    fsys_reset_guest_package(NULL);
    fsys_set_save_directory(directory.c_str());
    const uint8_t initial[] = { 31, 32, 33, 34 };
    const uint8_t appended[] = { 35, 36 };
    const uint8_t combined[] = { 31, 32, 33, 34, 35, 36 };
    const uint8_t replacement[] = { 41, 42 };
    bool normal = writeGuestSaveAutomationFile(
        "transaction/sample.sav", "wb", initial, sizeof(initial)) &&
        writeGuestSaveAutomationFile(
            "transaction/sample.sav", "ab", appended, sizeof(appended)) &&
        readGuestSaveAutomationFile(
            "transaction/sample.sav", combined, sizeof(combined)) &&
        writeGuestSaveAutomationFile(
            "transaction/sample.sav", "wb", replacement, sizeof(replacement)) &&
        readGuestSaveAutomationFile(
            "transaction/sample.sav", replacement, sizeof(replacement));

    const uint8_t damaged[] = { 99 };
    const char interrupted = '1';
    bool recoverySetup = writeSaveAutomationFile(directory,
        "transaction/recovery.sav", "wb", damaged, sizeof(damaged)) &&
        writeSaveAutomationFile(directory,
            "transaction/recovery.sav.dingoopie.transaction-v1.backup",
            "wb", initial, sizeof(initial)) &&
        writeSaveAutomationFile(directory,
            "transaction/recovery.sav.dingoopie.transaction-v1.pending", "wb",
            (const uint8_t*)&interrupted, 1);
    bool recovered = recoverySetup && readGuestSaveAutomationFile(
        "transaction/recovery.sav", initial, sizeof(initial));
    bool sidecarsRemoved = saveAutomationFileMissing(directory,
        "transaction/recovery.sav.dingoopie.transaction-v1.pending") &&
        saveAutomationFileMissing(directory,
            "transaction/recovery.sav.dingoopie.transaction-v1.backup");

    uint32_t streams[127] = {};
    bool capacity = true;
    for (size_t index = 0; index < sizeof(streams) / sizeof(streams[0]); ++index)
    {
        streams[index] = fsys_fopen("transaction/sample.sav", "rb");
        if (!streams[index])
        {
            capacity = false;
            break;
        }
    }
    bool exhaustedSafely = fsys_fopen("transaction/sample.sav", "rb") == 0;
    for (size_t index = 0; index < sizeof(streams) / sizeof(streams[0]); ++index)
    {
        if (streams[index])
        {
            fsys_fclose(streams[index]);
        }
    }
    uint32_t reopened = fsys_fopen("transaction/sample.sav", "rb");
    bool reusable = reopened != 0;
    if (reopened)
    {
        fsys_fclose(reopened);
    }
    fsys_reset_guest_package(NULL);
    fsys_set_save_directory("");
    printf("save-automation: transaction normal=%u recovered=%u sidecars_removed=%u "
        "capacity=%u exhausted_safely=%u reusable=%u\n",
        normal ? 1u : 0u,
        recovered ? 1u : 0u,
        sidecarsRemoved ? 1u : 0u,
        capacity ? 1u : 0u,
        exhaustedSafely ? 1u : 0u,
        reusable ? 1u : 0u);
    return normal && recovered && sidecarsRemoved && capacity &&
        exhaustedSafely && reusable;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_dingoopie_android_DingooPieActivity_nativeRunSaveAutomation(
    JNIEnv* env, jclass, jstring appDirectoryText, jstring ccDirectoryText)
{
    if (!env || !appDirectoryText || !ccDirectoryText)
    {
        return JNI_FALSE;
    }
    const char* appChars = env->GetStringUTFChars(appDirectoryText, NULL);
    const char* ccChars = env->GetStringUTFChars(ccDirectoryText, NULL);
    if (!appChars || !ccChars)
    {
        if (ccChars) env->ReleaseStringUTFChars(ccDirectoryText, ccChars);
        if (appChars) env->ReleaseStringUTFChars(appDirectoryText, appChars);
        return JNI_FALSE;
    }
    std::string appDirectory(appChars);
    std::string ccDirectory(ccChars);
    env->ReleaseStringUTFChars(ccDirectoryText, ccChars);
    env->ReleaseStringUTFChars(appDirectoryText, appChars);

    bool directoryClassification =
        platformIsPrivateStorageDirectory(appDirectory) &&
        platformIsPrivateStorageDirectory(ccDirectory) &&
        !platformIsPrivateStorageDirectory(
            "content://automation/tree/authorized-directory");
    const uint8_t initial[] = { 11, 12, 13, 14 };
    const uint8_t appended[] = { 15, 16 };
    const uint8_t combined[] = { 11, 12, 13, 14, 15, 16 };
    const uint8_t replacement[] = { 21, 22 };
    bool ok = directoryClassification && writeSaveAutomationFile(appDirectory,
        "native/sample.bin", "wbs", initial, sizeof(initial));
    ok = writeSaveAutomationFile(appDirectory,
        "native/sample.bin", "ab", appended, sizeof(appended)) && ok;
    ok = readSaveAutomationFile(appDirectory,
        "native/sample.bin", combined, sizeof(combined)) && ok;
    ok = writeSaveAutomationFile(appDirectory,
        "native/sample.bin", "wb", replacement, sizeof(replacement)) && ok;
    ok = readSaveAutomationFile(appDirectory,
        "native/sample.bin", replacement, sizeof(replacement)) && ok;
    ok = writeSaveAutomationFile(ccDirectory,
        "native/sample.dat", "wb", initial, sizeof(initial)) && ok;
    ok = readSaveAutomationFile(ccDirectory,
        "native/sample.dat", initial, sizeof(initial)) && ok;
    ok = runGuestSaveTransactionAutomation(appDirectory) && ok;
    ok = bridge_run_semaphore_regression() && ok;
    ok = saveStateRunRegressionTests() && ok;
    printf("save-automation: directory_classification=%u\n",
        directoryClassification ? 1u : 0u);
    return ok ? JNI_TRUE : JNI_FALSE;
}

FILE* platformOpenGameFile(const std::string& path)
{
    FILE* file = fopen(path.c_str(), "rb");
    if (file || !isAndroidContentGamePath(path))
    {
        return file;
    }

    int descriptor = openAndroidContentGameDescriptor(path);
    if (descriptor < 0)
    {
        return NULL;
    }
    file = fdopen(descriptor, "rb");
    if (!file)
    {
        close(descriptor);
    }
    return file;
}

FILE* platformOpenGameSiblingFile(const std::string& gamePath, const std::string& fileName)
{
    if (gamePath.empty() || fileName.empty())
    {
        return NULL;
    }
    if (!isAndroidContentGamePath(gamePath))
    {
        size_t separator = gamePath.find_last_of("\\/");
        std::string path = separator == std::string::npos ?
            fileName : gamePath.substr(0, separator + 1) + fileName;
        return fopen(path.c_str(), "rb");
    }

    int descriptor = openAndroidGameSiblingDescriptor(gamePath, fileName);
    if (descriptor < 0)
    {
        return NULL;
    }
    FILE* file = fdopen(descriptor, "rb");
    if (!file)
    {
        close(descriptor);
    }
    return file;
}

bool platformFileExists(const std::string& path)
{
    struct stat info;
    if (path.empty())
    {
        return false;
    }
    if (stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode))
    {
        return true;
    }
    FILE* file = platformOpenGameFile(path);
    if (!file)
    {
        return false;
    }
    fclose(file);
    return true;
}

bool platformChangeToGameDirectory(const std::string& gamePath)
{
    if (isAndroidContentGamePath(gamePath))
    {
        return true;
    }
    size_t separator = gamePath.find_last_of('/');
    return separator == std::string::npos || chdir(gamePath.substr(0, separator).c_str()) == 0;
}

std::string platformWideToUtf8(const std::wstring& text)
{
    std::string result;
    for (size_t index = 0; index < text.size(); ++index)
    {
        uint32_t codePoint = (uint32_t)text[index];
        if (codePoint <= 0x7fu)
        {
            result.push_back((char)codePoint);
        }
        else if (codePoint <= 0x7ffu)
        {
            result.push_back((char)(0xc0u | (codePoint >> 6)));
            result.push_back((char)(0x80u | (codePoint & 0x3fu)));
        }
        else if (codePoint <= 0xffffu)
        {
            result.push_back((char)(0xe0u | (codePoint >> 12)));
            result.push_back((char)(0x80u | ((codePoint >> 6) & 0x3fu)));
            result.push_back((char)(0x80u | (codePoint & 0x3fu)));
        }
        else if (codePoint <= 0x10ffffu)
        {
            result.push_back((char)(0xf0u | (codePoint >> 18)));
            result.push_back((char)(0x80u | ((codePoint >> 12) & 0x3fu)));
            result.push_back((char)(0x80u | ((codePoint >> 6) & 0x3fu)));
            result.push_back((char)(0x80u | (codePoint & 0x3fu)));
        }
    }
    return result;
}
