#include "platform_services.h"
#include "jni_local_ref.h"

#include <SDL_system.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* kAndroidContentGamePrefix = "android-content://";

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

std::string platformAndroidConsumeCheatManagerAutomationGamePath(void)
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
        "consumeCheatManagerAutomationGamePath", "()Ljava/lang/String;") : NULL;
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

std::string platformAndroidConsumeGameAutomationPath(void)
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
        "consumeGameAutomationPath", "()Ljava/lang/String;") : NULL;
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

bool platformAndroidConsumeAudioValidationAutomationEnabled(void)
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

void platformAndroidRequestApplicationExit(void)
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

std::string platformAndroidGetSaveDirectory(const std::string& gamePath)
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
        "getGameSaveDirectory", "(Ljava/lang/String;)Ljava/lang/String;") : NULL;
    jstring identity = method ? env->NewStringUTF(gamePath.c_str()) : NULL;
    jstring directory = identity ? (jstring)env->CallObjectMethod(activity, method, identity) : NULL;
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
    if (activityClass) env->DeleteLocalRef(activityClass);
    return result;
}

std::string platformAndroidGetCcSaveDirectory(const std::string& gamePath,
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

FILE* platformAndroidOpenSaveFile(const std::string& directoryUri,
    const std::string& fileName, const char* mode)
{
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    JniLocalRef<jobject> activity(env, (jobject)SDL_AndroidGetActivity());
    if (!env || !activity || directoryUri.empty() || fileName.empty())
    {
        return NULL;
    }

    FILE* file = NULL;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID method = activityClass ? env->GetMethodID(activityClass,
        "openGameSaveFileDescriptor", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I") : NULL;
    jstring directoryText = method ? env->NewStringUTF(directoryUri.c_str()) : NULL;
    jstring fileNameText = directoryText ? env->NewStringUTF(fileName.c_str()) : NULL;
    jstring modeText = fileNameText ? env->NewStringUTF(mode ? mode : "rb") : NULL;
    jint descriptor = (directoryText && fileNameText && modeText) ?
        env->CallIntMethod(activity, method, directoryText, fileNameText, modeText) : -1;
    if (!env->ExceptionCheck() && descriptor >= 0)
    {
        const char* hostMode = mode && mode[0] ? mode : "rb";
        file = fdopen((int)descriptor, hostMode);
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

static bool writeSaveAutomationFile(const std::string& directory,
    const char* name, const char* mode, const uint8_t* data, size_t size)
{
    FILE* file = platformAndroidOpenSaveFile(directory, name, mode);
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
    FILE* file = platformAndroidOpenSaveFile(directory, name, "rb");
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

    const uint8_t initial[] = { 11, 12, 13, 14 };
    const uint8_t appended[] = { 15, 16 };
    const uint8_t combined[] = { 11, 12, 13, 14, 15, 16 };
    const uint8_t replacement[] = { 21, 22 };
    bool ok = writeSaveAutomationFile(appDirectory,
        "native/slot1.sav", "wb", initial, sizeof(initial));
    ok = writeSaveAutomationFile(appDirectory,
        "native/slot1.sav", "ab", appended, sizeof(appended)) && ok;
    ok = readSaveAutomationFile(appDirectory,
        "native/slot1.sav", combined, sizeof(combined)) && ok;
    ok = writeSaveAutomationFile(appDirectory,
        "native/slot1.sav", "wb", replacement, sizeof(replacement)) && ok;
    ok = readSaveAutomationFile(appDirectory,
        "native/slot1.sav", replacement, sizeof(replacement)) && ok;
    ok = writeSaveAutomationFile(ccDirectory,
        "native/slot1.dat", "wb", initial, sizeof(initial)) && ok;
    ok = readSaveAutomationFile(ccDirectory,
        "native/slot1.dat", initial, sizeof(initial)) && ok;
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
