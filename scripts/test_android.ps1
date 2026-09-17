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
$sdkRoot = if ($env:ANDROID_SDK_ROOT) {
    $env:ANDROID_SDK_ROOT
} elseif ($env:ANDROID_HOME) {
    $env:ANDROID_HOME
} else {
    Join-Path $projectRoot '.tools\android\sdk'
}
$buildToolsRoot = Join-Path $sdkRoot 'build-tools'
$aapt2 = Get-ChildItem -LiteralPath $buildToolsRoot -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'aapt2.exe' } |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
if (!$aapt2) { throw "Android SDK aapt2.exe was not found below $buildToolsRoot" }

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($apk)
try {
    $requiredEntries = @('AndroidManifest.xml', 'lib/arm64-v8a/libmain.so', 'lib/armeabi-v7a/libmain.so', 'lib/x86/libmain.so', 'lib/x86_64/libmain.so')
    foreach ($entry in $requiredEntries) {
        if (!$archive.Entries.FullName.Contains($entry)) { throw "APK is missing $entry" }
    }
}
finally { $archive.Dispose() }

$manifest = & $aapt2 dump xmltree $apk --file AndroidManifest.xml
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$requiredManifestValues = @(
    'android:scheme.*="file"',
    'android:scheme.*="content"',
    'android:sspPattern.*="\.\*\\\.app"',
    'android:sspPattern.*="\.\*\\\.cc"',
    'android:sspPattern.*="\.\*\\\.c2m"',
    'android:sspPattern.*="\.\*\\\.c2s"',
    'android:sspPattern.*="\.\*\\\.c3s"',
    'android:host.*="\*"',
    'android:pathPattern.*="\.\*\\\.app"',
    'android:pathPattern.*="\.\*\\\.cc"',
    'android:pathPattern.*="\.\*\\\.c2m"',
    'android:pathPattern.*="\.\*\\\.c2s"',
    'android:pathPattern.*="\.\*\\\.c3s"'
)
foreach ($pattern in $requiredManifestValues) {
    if (!($manifest -match $pattern)) { throw "Compiled manifest is missing expected association: $pattern" }
}

if ($Install) {
    $adb = Join-Path $sdkRoot 'platform-tools\adb.exe'
    & $adb install -r $apk
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Write-Host "APK validation passed: $apk"
