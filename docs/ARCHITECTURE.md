# Native Core Architecture

The Android Gradle module remains `app/`; this is unrelated to the Dingoo `.app` package format.

## Source Ownership

Shared emulator code under `native/core/` is divided into five top-level
ownership areas:

- `app/`: APP lifecycle, MIPS execution, PPSSPP IR JIT integration, APP memory, SDK HLE, and APP state serialization.
- `cc/`: CC lifecycle, ARM32 interpreter and Dynarmic backend, CC memory/HLE, and CC state serialization.
- `shared/`: format selection, common execution coordination, guest services, save infrastructure, diagnostics, platform-neutral contracts, and shared runtime constants.
- `frontend/`: cross-platform framebuffer, frame processing, input, and other SDL frontend infrastructure.
- `config/`: settings persistence, compatibility profiles, and cheats.

Android integration remains under `native/android/`: `main.cpp` owns startup,
`frontend/` owns the shell, menus, and game library, `platform/` owns Android
services and JNI-facing helpers, `diagnostics/` owns Android log/resource hooks,
and `compat/` owns platform compatibility shims. Core code includes only
platform-neutral contracts such as `native/core/shared/platform/storage_services.h`.

## CPU Backend Scheme

- APP uses its own MIPS runtime. Automatic mode selects the PPSSPP IR JIT backend. The `Compatibility` menu value, unavailable JIT support, or a per-game compatibility override selects the built-in MIPS interpreter.
- CC uses its own ARM32 runtime. Automatic mode selects Dynarmic A32 when it is compiled. The `Compatibility` menu value, explicit interpreter instruction sampling, or unavailable Dynarmic support selects the built-in ARM32 interpreter. The normal Performance Log path keeps Dynarmic enabled.
- The execution-mode setting is shared, but APP and CC retain separate CPU state, memory, HLE, diagnostics, and save-state implementations.

## Dependency Direction

- `app` and `cc` never include one another.
- `shared` never includes `app`, `cc`, or `frontend`, except `shared/game/game_runtime.cpp`, the composition facade that selects the active format implementation.
- `frontend` never includes APP or CC headers; it uses `shared/game/game_runtime.h` and shared save-slot APIs.
- `config` never includes frontend or format-specific runtime headers.
- Android platform code may implement shared platform contracts and compose native modules.
- Core `.cpp` files are independent compilation units and must never be included from another source file.

Menu rendering and actions are compiled from
`native/android/frontend/menu/menu_overlay.cpp`. Its public menu operations are
declared in `menu_overlay.h`; the explicit shell integration boundary is
declared in `menu_overlay_internal.h`.

The runtime facade owns start, stop, pause, settings application, runtime capabilities, diagnostics, and save-state capture/restore dispatch. Format-specific state structures remain private to APP and CC implementations.

## Save Boundaries

`shared/save/` owns slot naming, thumbnail storage, compression helpers, transactional writes, recovery, and unchanged file headers. `app/save/` and `cc/save/` own only their format serialization and runtime state models. The existing magic values, header sizes, field order, and compatibility behavior are unchanged.

## Naming

Use `app_*` for APP-only implementation, `cc_*` for CC-only implementation,
`game_*` for multi-format orchestration, format-neutral names in core `shared/`
and `frontend/`, and platform-qualified names under `native/android/` when the
implementation is Android-specific. Ambiguous legacy names such as unqualified
`emulator_core`, `save_state`, `sdk_hle`, `menu_overlay.inl`, and
`platform_services.h` are rejected.

## Enforcement

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_core_architecture.ps1
```

The script rejects forbidden include directions, source-file includes, legacy ambiguous file names, and revived files under the obsolete `native/core/game`, `guest`, or `runtime` directories.
