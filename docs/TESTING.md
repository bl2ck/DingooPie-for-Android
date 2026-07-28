# Android testing

Use the smallest relevant regression first, then run the APK and text-format checks before release.

## APK validation

Build a debug APK and verify the manifest plus all four native ABIs:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1
```

Validate an existing release without rebuilding:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1 `
    -ApkPath app/build/outputs/apk/release/DingooPie.apk
```

## APP and CC runtime regression

`test_android_cc.ps1` runs the native CC graphics, math, timing, and ARM interpreter tests, launches a CC sample, captures logs and screenshots, and can launch an APP sample to verify shared settings and runtime isolation.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_cc.ps1 `
    -GamePath "C:\Games\sample.cc" `
    -AppGamePath "C:\Games\sample.app" `
    -VerifySharedSettings
```

For MuMu, the script automatically prefers the MuMu ADB when using serial `127.0.0.1:7555`. Pass `-AdbPath` for another emulator-specific ADB. Do not mix two ADB server implementations against the same running emulator.

With `-VerifySharedSettings`, the test checks the visible Settings order and
applies the same video, audio, input, execution-mode, clock, speed, delay,
cheat, and language values to both formats. Auto and Compatibility must still
select format-specific runtime implementations.

## Audio regression

Capture and analyze actual output for one APP and one CC sample:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_audio_validation.ps1 `
    -GamePath "C:\Games\sample.app"
powershell -ExecutionPolicy Bypass -File scripts/test_android_audio_validation.ps1 `
    -GamePath "C:\Games\sample.cc"
```

Use samples that begin audio without waiting for an unanswered in-game prompt.
The report fails on empty audio, dropped buffers, SDL queue errors, or invalid
capture timing.

## Cheat regression

Run the native cheat parser and Android cheat-manager automation:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_cheats.ps1 `
    -Serial 127.0.0.1:7555
```

Cheat selection is format-aware: APP prefers `Game.app.cht`, CC prefers `Game.cc.cht`, and `Game.cht` is used only when the format-specific file is absent.

## Input and IME regression

Validate Android system-keyboard policy:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_ime.ps1 `
    -Serial 127.0.0.1:7555
```

Debug builds support timed virtual-control sequences through `DINGOO_PIE_AUTOTEST_VIRTUAL_CLICK_SEQUENCE`. Input tracing is enabled with `DINGOO_PIE_INPUT_TRACE=1`. Use these only in isolated automation runs and clear package wrap properties afterward.

## Save regression

Debug builds expose `dingoopie.save_automation`. The automation verifies APP/CC private-directory isolation, nested paths, overwrite and append modes, Java and native `FILE*` readback, and path traversal rejection. A passing run writes the following result to logcat and removes its temporary files:

```text
SAVE_AUTOMATION result=pass ... native_io=true ...
```

Games opened from a writable Storage Access Framework folder save beside the game. Single-file imports, direct paths without writable access, and expired folder grants fall back to application-private `app-saves` or `cc-saves` directories.

## Guest failure logs

Both runtimes must report a successfully written `DingooPie-crash-*.log` after
an injected guest execution failure. A writable imported folder places the log
beside the game; otherwise it uses the format-isolated private save directory.
APP reports MIPS runtime context and CC reports ARM registers plus execution and
import statistics. Startup file-open or package-parse rejection is not a guest
execution failure and is validated through the native runtime log instead.

## Menu structure

`native/core/frontend/menu_model.h` is the source of truth for visible menu row
indices. When a menu changes, verify the same order in `menu_overlay.inl`, the
selection handler, `EmulatorSettings`, INI load/save tracing, and both UI
languages. `-VerifySharedSettings` provides the runtime-side ordering and value
application check.

## Android compatibility regression

Run the API 35 AVD compatibility check:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android_compatibility.ps1
```

Multiple AVDs require `-RunMatrix`. Use `-WipeData` only for an intentional clean-device test. The script blocks emulator startup after recent Windows WHEA hardware errors unless `-IgnoreHostHardwareErrors` is explicitly supplied.

## Text format

Repository text files must be UTF-8 without BOM, use CRLF line endings, and end with a final CRLF:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
```

Use `-Fix` to normalize detected text files before the final validation.
