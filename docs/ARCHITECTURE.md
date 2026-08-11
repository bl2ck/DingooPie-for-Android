# DingooPie Android Architecture

The Android Gradle module remains named `app/` because that is the standard
Android application-module convention. Its directory name is unrelated to the
Dingoo `.app` game format.

Runtime code is organized by ownership. Android lifecycle, Storage Access
Framework permissions, and JNI callbacks remain in `DingooPieActivity`; game
dispatch, guest services, and emulator behavior live in native modules.

## Layer Boundaries

The native core is divided into shared orchestration and format-specific
runtimes:

APP packages use the XBurst/MIPS architecture of the Ingenic JZ4732 SoC. CC
packages use the ARM11 architecture of the ChinaChip CC1800 SoC. Their
format-specific runtimes provide the corresponding execution paths and
platform-specific compatibility helpers for these guest environments.

All CC packages target ChinaChip CC1800. The runtime selects between two
software memory and input layouts by package origin; this distinction does not
represent different hardware platforms:

- The commercial release layout uses package origin `0x10100000`, runtime RAM
  `0x10000000`-`0x14000000`, and a 32 MiB heap at `0x21000000`.
- The homebrew-platform layout uses SDK-linked application origin `0x13800000`,
  a 16 MiB application window at `0x13800000`-`0x14800000`, system work RAM at
  `0x10000000`-`0x13800000`, and a 32 MiB OS heap at `0x09000000`.
- `native/core/app/` contains the APP MIPS runtime, PPSSPP integration, APP
  memory model, instruction compatibility, SDK HLE, and APP task scheduling.
- `native/core/cc/` contains the CC ARM32 runtime, package-layout constants,
  interpreter, input mapping, graphics compatibility, math compatibility, and
  timing helpers. `cc_package_layout.h` is the single source of truth for CC
  package origins.
- `native/core/frontend/` owns SDL presentation, audio output, virtual input,
  menus, framebuffer snapshots, and validation capture.
- `native/core/guest/` owns format-neutral package, filesystem, audio, and text
  services visible to guest programs.
- `native/core/runtime/` owns execution infrastructure, logging, crash reports,
  pause coordination, and runtime debugging.
- `native/core/config/` owns settings, options, compatibility profiles, and the
  shared cheat engine/runtime.
- `native/core/game/` owns format detection, game paths, and runtime selection.
  `native/core/main.cpp` remains the application entry point, while
  `platform_services.h` remains the platform boundary.

Android save access is routed through `DingooPieActivity` and
`native/android/platform_android.cpp`. Writable Storage Access Framework
folders are used directly. Games without a valid writable folder grant fall
back to format-isolated application-private directories. `SaveAutomation.java`
contains the debug-only Java and native save regression and remains separate
from the activity lifecycle and menu implementation.

- `game_runtime.*` selects a runtime by `GameFormat`. APP is the primary format
  and is intentionally listed and dispatched before CC.
- `game_paths.*` owns supported-extension detection, normalization, display
  names, and cheat-file naming for every game format.
- `guest_package.*` parses the package container used by APP and CC images.
- `guest_text_format.*`, `guest_filesystem.*`, audio, input, settings, cheats,
  and frontend code are shared services and therefore use format-neutral names.
- `app_mips_runtime.*` contains only the MIPS APP execution path.
- `cc_arm_runtime.*`, `arm32_interpreter.*`, and
  `cc_runtime_timing.h` contain only the ARM CC execution path and its
  compatibility behavior.

Format-specific runtimes may depend on shared services. They must not include,
call, or expose identifiers named after the other format. The shared
`game_runtime` facade is the only layer that selects between APP and CC.

## Compatibility Rules

- The current INI schema uses only the documented values. Invalid or removed
  values fall back to current defaults instead of selecting legacy aliases.
- Shared emulator settings, controls, rendering, timing, save paths, cheats,
  and frontend behavior are applied before or through either runtime. CC must
  not maintain a disconnected copy of a setting already owned by shared code.
- Shared audio owns the Android output device and converts each guest stream
  from its declared sample rate, format, and channel count. CC queue pressure
  yields only the current ARM task so audio pacing cannot stall the game loop.
- Digital noise reduction is shared by APP and CC. High enables resampling
  low-pass filtering, DC blocking, boundary smoothing, and soft limiting;
  Medium omits the low-pass filter; Low keeps boundary smoothing and soft
  limiting while omitting DC blocking.
- Format runtimes report completion to `game_runtime`; they do not directly
  drive frontend transitions. Frontend-requested stops are not recorded as
  normal guest exits.
- CPU execution modes are format-aware. Auto selects PPSSPP IR JIT for APP
  unless an APP compatibility profile overrides it, while CC selects its
  optimized ARM32 runtime. Compatibility selects the APP MIPS interpreter or
  the base CC ARM32 interpreter path.
- The persisted runtime backend values are empty for Auto and `compatibility`
  for format-specific base interpreters. PPSSPP IR JIT remains an internal APP
  implementation selected by Auto rather than a separate user-facing mode.
- Format-specific behavior belongs in its runtime and should be connected to a
  shared setting or compatibility profile whenever such a setting exists.
- APP and CC guest failures use the shared crash-log writer and the active save
  location. Each runtime contributes architecture-specific register and
  execution context.
- If no cheat file is available for the selected game, the shared cheat status
  remains unavailable and the frontend must not allow cheats to be enabled.
- Cheat files are format-aware: `Game.app` prefers `Game.app.cht`, while
  `Game.cc` prefers `Game.cc.cht`. The legacy `Game.cht` name is used only when
  the format-specific file is absent. Feature selections are persisted under
  the format-specific name so same-named APP and CC games remain isolated.
- Removing a game from the Android library must require confirmation and must
  never delete the source package from storage.

## Naming And Ordering

Use `game_*` for multi-format orchestration and paths, `guest_*` for services
visible to either guest runtime, `app_*` for APP-only implementation, and
`cc_*` for CC-only implementation. When APP and CC values appear together in
an enum, switch, menu, build list, or documentation table, list APP first unless
an external binary or persisted configuration format requires another order.

Internal C++ functions and variables use lower camel case, types use PascalCase,
constants use a `k` prefix, and enum values use uppercase snake case. Names that
mirror exported Dingoo SDK symbols, such as `fsys_*`, `waveout_*`, `_kbd_*`, and
`bridge_*`, retain their guest-facing spelling because they are part of the HLE
and compatibility contract rather than ordinary host implementation names.

`frontend/menu_model.h` owns menu-screen and row enums. Row values are visible
indices, so declarations, row rendering, and selection handlers must remain in
the same order. `EmulatorSettings` fields and INI serialization follow the
Video, Audio, Input, and Settings menu order.
