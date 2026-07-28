# Android build

## Requirements

- Windows PowerShell 5.1 or newer
- JDK 17
- Android SDK platform 35, build-tools, platform-tools, and NDK 26.3.11579264
- Git (only needed when bootstrapping PPSSPP)

Set `JAVA_HOME` and `ANDROID_SDK_ROOT`, or place the SDK below `.tools/android/sdk`.

## Dependencies

`bootstrap_android.ps1` downloads SDL 2.26.5 and the pinned PPSSPP source, validates their SHA-256 hashes, and applies `patches/ppsspp-irjit-dingoo.patch`. Downloaded dependencies remain untracked under `third_party/` and `.tools/`.

## Build

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1
```

The debug APK is written to `app/build/outputs/apk/debug/DingooPie.apk`.

For a normal source change, keep the validation order deterministic: bootstrap
only when dependencies are missing, build the APK, run the focused regression
scripts for the changed subsystem, run `scripts/test_android.ps1`, and finish
with `scripts/check_text_format.ps1`. This preserves the APP/CC architecture
boundary and catches accidental encoding or line-ending changes before commit.

## Release build

Production release builds require these environment variables:

- `DINGOO_PIE_RELEASE_STORE_FILE`
- `DINGOO_PIE_RELEASE_STORE_PASSWORD`
- `DINGOO_PIE_RELEASE_KEY_ALIAS`
- `DINGOO_PIE_RELEASE_KEY_PASSWORD`

Alternatively, place an untracked properties file at
`.tools/signing/release-signing.properties` with the keys `storeFile`,
`storePassword`, `keyAlias`, and `keyPassword`. Pass
`-ReleaseSigningProperties` to use a different properties file. Environment
variables take precedence over values from the properties file.

Build a signed release and copy it to an output directory:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1 `
    -Configuration Release `
    -OutputDirectory "$HOME\Desktop"
```

The signed artifact is named `DingooPie-Android-v<version>-release.apk`. The build script reads the local untracked `.tools/signing/release-signing.properties` file automatically when it exists.

The official certificate fingerprint and verification procedure are recorded
in `docs/RELEASE_SIGNING.md`. Compare the certificate SHA-256 fingerprint before
publishing every release.

When production signing credentials are unavailable, pass `-AllowUnsignedRelease` explicitly. The resulting filename includes `-unsigned` and must be signed before store distribution.

Use `-AndroidSdkRoot` when the SDK is not configured through the environment or `.tools/android/sdk`.

Validate the generated release before distribution:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1 `
    -ApkPath "$HOME\Desktop\DingooPie-Android-v1.0-release.apk"
```

## Text format

Repository text files use UTF-8 without a byte-order mark, CRLF line endings, and a final CRLF. Validate them with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
```
