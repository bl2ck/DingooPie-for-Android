#pragma once

#include <jni.h>

template <typename T>
class JniLocalRef
{
public:
    JniLocalRef(JNIEnv* env, T reference) : env_(env), reference_(reference) {}

    ~JniLocalRef()
    {
        if (env_ && reference_)
        {
            env_->DeleteLocalRef(reference_);
        }
    }

    JniLocalRef(const JniLocalRef&) = delete;
    JniLocalRef& operator=(const JniLocalRef&) = delete;

    operator T() const { return reference_; }
    explicit operator bool() const { return reference_ != NULL; }

private:
    JNIEnv* env_;
    T reference_;
};
