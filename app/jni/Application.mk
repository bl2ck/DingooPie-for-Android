APP_ABI := armeabi-v7a arm64-v8a x86 x86_64
APP_PLATFORM := android-23
APP_STL := c++_static
APP_CPPFLAGS := -std=c++17 -fexceptions -frtti
APP_LDFLAGS := -Wl,-z,max-page-size=16384
APP_OPTIM := release
