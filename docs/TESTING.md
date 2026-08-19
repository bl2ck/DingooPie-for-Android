# Testing DingooPie Android

Use the smallest relevant regression first, then run the APK and text-format checks before release.

## APK Validation

Build a debug APK and verify the manifest plus all four native ABIs:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1
```

Validate an existing release without rebuilding:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1 `
    -ApkPath app/build/outputs/apk/release/DingooPie.apk
```

## APP And CC Runtime Regression

`test_android_cc.ps1` runs the native CC graphics, math, timing, and ARM
interpreter tests, launches a CC sample, captures logs and screenshots, and can
launch an APP sample to verify shared settings and runtime isolation.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_cc.ps1 `
    -GamePath "C:\Games\sample.cc" `
    -AppGamePath "C:\Games\sample.app" `
    -VerifySharedSettings
```

For MuMu, the script automatically prefers the MuMu ADB when using serial
`127.0.0.1:7555`. Pass `-AdbPath` for another emulator-specific ADB. Do not mix
two ADB server implementations against the same running emulator.

The MuMu x86_64 build uses a pinned Dynarmic A32 backend when `profile=0`.
Prepare its static libraries before the normal Gradle build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
    -File scripts/prepare_android_dynarmic.ps1
.\gradlew.bat --no-daemon assembleDebug
```

The preparation script pins Dynarmic commit
`a41c380246d3d9f9874f0f792d234dc0cc17c180`, Boost headers 1.84.0 with
SHA-512 verification, NDK 26.3.11579264, and CMake 3.22.1. Other ABIs continue
to use the ARM32 interpreter. Profile runs also use the interpreter so PC/LR
sampling remains available.

Run both retail 3D CC regressions with OCR-guided scene entry and FPS sampling:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_cc_3d_regression.ps1 `
    -CcDirectory "C:\Games\cc"
```

For a MuMu Dynarmic acceptance run, pass `-ExpectedBackend dynarmic` to each
single-game invocation. The JSON summary records `execution_backend` and
`profile_active`; formal FPS samples require `profile_active=0`.

Each case archives screenshots, `fps.csv`, Android logcat, the private native
log, extracted `cc-profile` / `profile:frontend` lines, and a JSON summary. With
`debug.profile=1`, CC profile rows include interpreter IPS, framebuffer submit
count, framebuffer copy time, average/maximum frame interval, and counts above
25 ms and 33 ms. Use those fields to distinguish guest CPU limits from frame
submission or frontend presentation stalls.

For a diagnostic run without manually editing the emulator settings, pass
`-EnableProfile` to the single-game script. It backs up `DingooPie.ini`, enables
`debug.profile=1` for the run, and restores the original file afterward:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_cc_3d_fps.ps1 `
    -Game qiye -GamePath "C:\Games\cc\sample.cc" `
    -EnableProfile
```

With `-VerifySharedSettings`, the test checks the visible Settings order and
applies the same video, audio, input, execution-mode, clock, speed, delay,
cheat, and language values to both formats. Auto and Compatibility must still
select format-specific runtime implementations.

## Audio Regression

Capture and analyze actual output for one APP and one CC sample:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_audio_validation.ps1 `
    -GamePath "C:\Games\sample.app"
powershell -ExecutionPolicy Bypass -File scripts/test_android_audio_validation.ps1 `
    -GamePath "C:\Games\sample.cc"
```

Use samples that begin audio without waiting for an unanswered in-game prompt.
The report fails on empty audio, dropped buffers, SDL queue errors, or invalid
capture timing. It also reports the parsed device buffer duration; APP and CC
use the same stable 48 kHz host output so equal buffer settings have equal
playback latency regardless of the guest sample rate. When `-InputSequence` is
used, the report estimates input response from the native button timestamp,
the next PCM submission, queued audio, and the parsed device buffer duration.
The host queue uses the Auto latency target so audio stays aligned with the 60 Hz frame
submission cadence while still tolerating short irregular APP and CC writes.
This avoids allowing audio to run ahead of the displayed animation on MuMu
while avoiding the former quarter-second backlog.

## Cheat Regression

Run the native cheat parser and Android cheat-manager automation:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_cheats.ps1 `
    -Serial 127.0.0.1:7555
```

Cheat selection is format-aware: APP prefers `Game.app.cht`, CC prefers
`Game.cc.cht`, and `Game.cht` is used only when the format-specific file is
absent.

## Input And IME Regression

Validate Android system-keyboard policy:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_ime.ps1 `
    -Serial 127.0.0.1:7555
```

Debug builds support timed virtual-control sequences through
`DINGOO_PIE_AUTOTEST_VIRTUAL_CLICK_SEQUENCE`. Input tracing is enabled with
`DINGOO_PIE_INPUT_TRACE=1`. Use these only in isolated automation runs and clear
package wrap properties afterward.

## Save Regression

Debug builds expose `dingoopie.save_automation`. The automation verifies APP/CC
private-directory isolation, nested paths, overwrite and append modes, Java and
native `FILE*` readback, path traversal rejection, DGSS payload corruption
handling, and 100 consecutive save-state compression/decompression cycles. A
passing run writes the following results and removes its temporary files:

```text
SAVE_AUTOMATION result=pass ... native_io=true ...
save-state-regression: result=pass iterations=100 ... corruption_rejected=1 invalid_region_rejected=1
```

Games opened from a writable Storage Access Framework folder save beside the
game. Single-file imports, direct paths without writable access, and expired
folder grants fall back to application-private `app-saves` or `cc-saves`
directories. Both formats use the game content SHA-256 as the private save
subdirectory name.

APP and CC instant states use 15 slots under the active format-specific save
directory. State files use `savestates/<game>.slotN.dps`, and previews use
`savestates/<game>.slotN.thumb.bmp`. Deleting a slot removes both files and
refreshes its empty state, preview, and timestamp. APP writes the PC-compatible
DGSS payload, while CC writes its ARM32 runtime state with game, task, stream,
resource, semaphore, and memory-layout validation.

Run the all-sample save/load automation when sample games are available:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_save_state_all_samples.ps1 `
    -SampleRoot "D:\Games\DingooSamples" `
    -Serial 127.0.0.1:7555
```

Each sample must save, load, and return to the game screen without leaving the
application in the menu or a stalled runtime state.

For a repeatable CC 3D interactive-scene save test, use the TiandiDao workflow:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_tiandidao_cc_save_restart.ps1 `
    -GamePath "C:\Games\cc\TiandiDao.cc" `
    -AdbPath "D:\Android\sdk\platform-tools\adb.exe" `
    -Serial 127.0.0.1:7555
```

The workflow enters the playable 3D scene, captures a state, exits and restarts
without clearing private saves, loads the state, and verifies that the native CC
runtime restores and continues. Pair it with the OCR/FPS regression above so
scene interactivity and frame pacing are checked independently from save loading.

Validate the instant save-state menu visually on the 960x540 Android test device:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_save_state_layout.ps1 `
    -Serial 127.0.0.1:7555
```

The script launches an APP game, opens the pause and instant save-state menus,
captures both screens, and verifies from rendered pixels that the standard menu
row, mode tabs, slot cells, save/load action, delete action, and back action all
have the same 46-pixel height. It also checks borderless tabs and slots, equal
dynamically sized slot columns, six-pixel vertical and horizontal gaps, the
timestamp block aligned with the final slot row, and the independent 4:3 preview
whose top aligns with the first slot row.

## Task Thread Lifecycle Regression

Run the Android native stress test for APP subtask thread cleanup:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_task_lifecycle.ps1 `
    -Serial 127.0.0.1:7555
```

The test creates 4,000 short-lived detached tasks, verifies that all 4,000
concurrent profiling increments are retained, waits for the active count to
reach zero, and fails if the process virtual-memory increase exceeds 16 MiB. It
also stresses synchronized diagnostic snapshots and the shared APP tick clock
with 16 concurrent host threads.

## Android Resource Regressions

Run the host-side USB identifier, staged-upload, and external-launch request
comparisons:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_resource_regressions.ps1
```

The USB test verifies that the previous first-device value of zero is replaced
by stable positive identifiers. The upload test reproduces direct-write target
corruption after an incomplete request and verifies that staging preserves the
existing file until the full request body has arrived. The external-launch test
verifies APP/CC extension recognition, quoted path handling, and `file://` URI
normalization used by emulator frontends.

## Android Physical Keyboard Mapping

Run the host-side default mapping and Android SDL input-chain regression:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_keyboard_mapping.ps1
```

The test verifies every documented default keyboard binding, checks that SDL's
Android key table maps the required Android key codes to the expected SDL
scancodes, and confirms that keyboard down/up events reach SDL native input.
This is a code-level regression; final USB and Bluetooth keyboard acceptance
still requires an Android device or emulator with physical-keyboard input.

## File Manager Socket Queue Regression

Run the host-side comparison for queued file-manager connections:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_socket_task.ps1
```

The test first reproduces the previous lambda-queue behavior, where
`shutdownNow()` removes a queued task but leaves its socket open. It then checks
that `CloseableSocketTask` closes a queued socket and transfers a running socket
to its handler exactly once.

## Guest Failure Logs

Both runtimes must report a successfully written `DingooPie-crash-*.log` after
an injected guest execution failure. A writable imported folder places the log
beside the game; otherwise it uses the format-isolated private save directory.
APP reports MIPS runtime context and CC reports ARM registers plus execution and
import statistics. Startup file-open or package-parse rejection is not a guest
execution failure and is validated through the native runtime log instead.

## Menu Structure

`native/core/frontend/menu/menu_model.h` is the source of truth for visible menu row
indices. When a menu changes, verify the same order in `native/core/frontend/menu/menu_overlay.cpp`, the
selection handler, `EmulatorSettings`, INI load/save tracing, and both UI
languages. `-VerifySharedSettings` provides the runtime-side ordering and value
application check.

Run the structural order regression directly after changing a visible setting:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_settings_order.ps1
```

## Android Compatibility Regression

Run the API 35 AVD compatibility check:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_compatibility.ps1
```

Multiple AVDs require `-RunMatrix`. Use `-WipeData` only for an intentional
clean-device test. The script blocks emulator startup after recent Windows WHEA
hardware errors unless `-IgnoreHostHardwareErrors` is explicitly supplied.

## Text Format

Repository text files must be UTF-8 without BOM, use CRLF line endings, and end with a final CRLF:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
powershell -ExecutionPolicy Bypass -File scripts/check_core_architecture.ps1
```

Use `-Fix` to normalize detected text files before the final validation.
