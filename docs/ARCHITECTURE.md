# Native Core Architecture

The Android Gradle module remains `app/`; this is unrelated to the Dingoo `.app` package format.

## Source Ownership

`native/core/` is divided into five top-level ownership areas:

- `app/`: APP lifecycle, MIPS execution, PPSSPP IR JIT integration, APP memory, SDK HLE, and APP state serialization.
- `cc/`: CC lifecycle, ARM32 interpreter and Dynarmic backend, CC memory/HLE, and CC state serialization.
- `shared/`: format selection, common execution coordination, guest services, save-slot storage, diagnostics, platform abstractions, and shared runtime constants.
- `frontend/`: SDL shell, framebuffer and frame post-processing, input, audio, menu, and library presentation responsibilities.
- `config/`: settings persistence, compatibility profiles, and cheats.

Android implementations remain in `native/android/`. Core code includes only the platform-neutral declarations under `native/core/shared/platform/`.

## Dependency Direction

- `app` and `cc` never include one another.
- `shared` never includes `app`, `cc`, or `frontend`, except `shared/game/game_runtime.cpp`, the composition facade that selects the active format implementation.
- `frontend` never includes APP or CC headers; it uses `shared/game/game_runtime.h` and shared save-slot APIs.
- `config` never includes frontend or format-specific runtime headers.
- Android platform code may implement shared platform interfaces and compose native modules.

The runtime facade owns start, stop, pause, settings application, runtime capabilities, diagnostics, and save-state capture/restore dispatch. Format-specific state structures remain private to APP and CC implementations.

## Save Boundaries

`shared/save/` owns slot naming, thumbnail storage, compression helpers, transactional writes, recovery, and unchanged file headers. `app/save/` and `cc/save/` own only their format serialization and runtime state models. The existing magic values, header sizes, field order, and compatibility behavior are unchanged.

## Naming

Use `app_*` for APP-only implementation, `cc_*` for CC-only implementation, `game_*` for multi-format orchestration, and format-neutral names in `shared/` and `frontend/`. Ambiguous legacy names such as unqualified `emulator_core`, `save_state`, `sdk_hle`, `menu_overlay.inl`, and `platform_services.h` are rejected.

## Enforcement

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_core_architecture.ps1
```

The script rejects forbidden include directions, legacy ambiguous file names, and revived files under the obsolete `native/core/game`, `guest`, or `runtime` directories.
