LOCAL_PATH := $(call my-dir)
PROJECT_ROOT := $(LOCAL_PATH)/../..
SDL_PATH := $(PROJECT_ROOT)/third_party/SDL2-2.26.5
PPSSPP_PATH := $(PROJECT_ROOT)/third_party/ppsspp-master

include $(SDL_PATH)/Android.mk

include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_C_INCLUDES := \
    $(SDL_PATH)/include \
    $(PROJECT_ROOT)/native/core \
    $(PROJECT_ROOT)/native/core/app \
    $(PROJECT_ROOT)/native/core/cc \
    $(PROJECT_ROOT)/native/core/config \
    $(PROJECT_ROOT)/native/core/frontend \
    $(PROJECT_ROOT)/native/core/game \
    $(PROJECT_ROOT)/native/core/guest \
    $(PROJECT_ROOT)/native/core/runtime \
    $(PROJECT_ROOT)/native/android \
    $(PPSSPP_PATH) \
    $(PPSSPP_PATH)/Common \
    $(PPSSPP_PATH)/Core
LOCAL_CPP_FEATURES := exceptions rtti
LOCAL_CPPFLAGS := -std=c++17 -Wno-format-security
LOCAL_SRC_FILES := \
    $(SDL_PATH)/src/main/android/SDL_android_main.c \
    $(PROJECT_ROOT)/native/core/app/app_runtime.cpp \
    $(PROJECT_ROOT)/native/core/cc/arm32_interpreter.cpp \
    $(PROJECT_ROOT)/native/core/frontend/audio_validation_capture.cpp \
    $(PROJECT_ROOT)/native/core/cc/cc_arm_runtime.cpp \
    $(PROJECT_ROOT)/native/core/config/cheat_engine.cpp \
    $(PROJECT_ROOT)/native/core/config/cheat_runtime.cpp \
    $(PROJECT_ROOT)/native/core/config/compat_profile.cpp \
    $(PROJECT_ROOT)/native/core/runtime/crash_log.cpp \
    $(PROJECT_ROOT)/native/core/runtime/debug_log.cpp \
    $(PROJECT_ROOT)/native/core/app/emulated_memory.cpp \
    $(PROJECT_ROOT)/native/core/config/emulator_options.cpp \
    $(PROJECT_ROOT)/native/core/config/emulator_settings.cpp \
    $(PROJECT_ROOT)/native/core/runtime/execution_backend.cpp \
    $(PROJECT_ROOT)/native/core/frontend/framebuffer.cpp \
    $(PROJECT_ROOT)/native/core/game/game_history.cpp \
    $(PROJECT_ROOT)/native/core/game/game_paths.cpp \
    $(PROJECT_ROOT)/native/core/game/game_runtime.cpp \
    $(PROJECT_ROOT)/native/core/guest/guest_audio.cpp \
    $(PROJECT_ROOT)/native/core/guest/guest_filesystem.cpp \
    $(PROJECT_ROOT)/native/core/guest/guest_text_format.cpp \
    $(PROJECT_ROOT)/native/core/guest/guest_package.cpp \
    $(PROJECT_ROOT)/native/core/frontend/input_controls.cpp \
    $(PROJECT_ROOT)/native/core/app/instruction_compat.cpp \
    $(PROJECT_ROOT)/native/core/main.cpp \
    $(PROJECT_ROOT)/native/core/frontend/menu_strings.cpp \
    $(PROJECT_ROOT)/native/core/runtime/native_runtime.cpp \
    $(PROJECT_ROOT)/native/core/runtime/pause_gate.cpp \
    $(PROJECT_ROOT)/native/core/app/ppsspp_irjit_backend.cpp \
    $(PROJECT_ROOT)/native/core/app/ppsspp_shim.cpp \
    $(PROJECT_ROOT)/native/core/runtime/runtime_debug.cpp \
    $(PROJECT_ROOT)/native/core/runtime/runtime_log.cpp \
    $(PROJECT_ROOT)/native/core/app/sdk_hle.cpp \
    $(PROJECT_ROOT)/native/core/frontend/sdl_audio.cpp \
    $(PROJECT_ROOT)/native/core/frontend/sdl_frontend.cpp \
    $(PROJECT_ROOT)/native/core/app/task_scheduler.cpp \
    $(PROJECT_ROOT)/native/android/Common/Crypto/sha256.cpp \
    $(PROJECT_ROOT)/native/android/platform_android.cpp \
    $(PROJECT_ROOT)/native/android/capstone_stub.cpp

LOCAL_CPPFLAGS += -DDINGOO_PIE_DINGOO_MEMORY -D__LIBRETRO__
LOCAL_SRC_FILES += \
    $(PROJECT_ROOT)/native/android/ppsspp_file_stub.cpp \
    $(PPSSPP_PATH)/ext/xxhash.c \
    $(PPSSPP_PATH)/Common/ABI.cpp \
    $(PPSSPP_PATH)/Common/Buffer.cpp \
    $(PPSSPP_PATH)/Common/File/AndroidContentURI.cpp \
    $(PPSSPP_PATH)/Common/File/Path.cpp \
    $(PPSSPP_PATH)/Common/MemoryUtil.cpp \
    $(PPSSPP_PATH)/Common/Net/URL.cpp \
    $(PPSSPP_PATH)/Common/StringUtils.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSCodeUtils.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSDis.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSDisVFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSInt.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSIntVFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSTables.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSVFPUFallbacks.cpp \
    $(PPSSPP_PATH)/Core/MIPS/MIPSVFPUUtils.cpp \
    $(PPSSPP_PATH)/Core/MIPS/JitCommon/JitBlockCache.cpp \
    $(PPSSPP_PATH)/Core/MIPS/JitCommon/JitState.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRAnalysis.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRCompALU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRCompBranch.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRCompFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRCompLoadStore.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRCompVFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRFrontend.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRInst.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRInterpreter.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRJit.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRNativeCommon.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRPassSimplify.cpp \
    $(PPSSPP_PATH)/Core/MIPS/IR/IRRegCache.cpp

ifneq ($(filter x86 x86_64,$(TARGET_ARCH_ABI)),)
LOCAL_SRC_FILES += \
    $(PPSSPP_PATH)/Common/x64Emitter.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRAsm.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRCompALU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRCompBranch.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRCompFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRCompLoadStore.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRCompSystem.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRCompVec.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRJit.cpp \
    $(PPSSPP_PATH)/Core/MIPS/x86/X64IRRegCache.cpp
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
LOCAL_SRC_FILES += \
    $(PPSSPP_PATH)/Common/ArmEmitter.cpp \
    $(PPSSPP_PATH)/ext/disarm.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmAsm.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmCompALU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmCompBranch.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmCompFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmCompLoadStore.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmCompReplace.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmCompVFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmJit.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmRegCache.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM/ArmRegCacheFPU.cpp
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
LOCAL_SRC_FILES += \
    $(PPSSPP_PATH)/Common/Arm64Emitter.cpp \
    $(PPSSPP_PATH)/Core/Util/DisArm64.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRAsm.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRCompALU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRCompBranch.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRCompFPU.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRCompLoadStore.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRCompSystem.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRCompVec.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRJit.cpp \
    $(PPSSPP_PATH)/Core/MIPS/ARM64/Arm64IRRegCache.cpp
endif
LOCAL_LDLIBS := -lGLESv1_CM -lGLESv2 -lOpenSLES -llog -landroid
LOCAL_SHARED_LIBRARIES := SDL2
include $(BUILD_SHARED_LIBRARY)
