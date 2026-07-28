param(
    [string]$ApkPath,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
if (!$ApkPath) {
    & (Join-Path $PSScriptRoot 'build_android.ps1') -Configuration Debug
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$apk = if ($ApkPath) {
    (Resolve-Path -LiteralPath $ApkPath).Path
} else {
    Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($apk)
try {
    $requiredEntries = @('AndroidManifest.xml', 'lib/arm64-v8a/libmain.so', 'lib/armeabi-v7a/libmain.so', 'lib/x86/libmain.so', 'lib/x86_64/libmain.so')
    foreach ($entry in $requiredEntries) {
        if (!$archive.Entries.FullName.Contains($entry)) { throw "APK is missing $entry" }
    }
}
finally { $archive.Dispose() }

if ($Install) {
    $sdkRoot = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } elseif ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $projectRoot '.tools\android\sdk' }
    $adb = Join-Path $sdkRoot 'platform-tools\adb.exe'
    & $adb install -r $apk
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Write-Host "APK validation passed: $apk"
