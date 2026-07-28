#ifndef DINGOO_PIE_APP_INSTRUCTION_COMPAT_H
#define DINGOO_PIE_APP_INSTRUCTION_COMPAT_H

#include "guest/guest_package.h"
#include "config/emulator_options.h"

#include "runtime/native_runtime.h"

// Installs precise hooks for guest instructions handled outside the main interpreter.
RuntimeError runtimeCompatInstallHooks(NativeRuntime* runtime, GuestPackage* appInfo, const EmulatorOptions& options);

#endif
