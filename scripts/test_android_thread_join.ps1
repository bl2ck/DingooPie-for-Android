param(
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ndkVersion = '26.3.11579264'

if (!$AndroidSdkRoot) {
    $sdkLine = Get-Content -LiteralPath (Join-Path $projectRoot 'local.properties') |
        Where-Object { $_ -match '^sdk\.dir=' } |
        Select-Object -First 1
    if ($sdkLine) {
        $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).Replace('\:', ':')
    }
}
if (!$AndroidSdkRoot) { throw 'Android SDK is required.' }
if (!$AdbPath) { $AdbPath = Join-Path $AndroidSdkRoot 'platform-tools\adb.exe' }

$compiler = Join-Path $AndroidSdkRoot `
    "ndk\$ndkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin\x86_64-linux-android23-clang++.cmd"
$outputDirectory = Join-Path $projectRoot '.tools\thread-join'
$null = New-Item -ItemType Directory -Force -Path $outputDirectory
$nativeTest = Join-Path $outputDirectory 'thread-join-test-x86_64'

& $compiler -std=c++17 -O2 -pthread -static-libstdc++ `
    -I (Join-Path $projectRoot 'native\core') `
    (Join-Path $projectRoot 'tests\thread_join_test.cpp') `
    (Join-Path $projectRoot 'native\core\shared\execution\thread_join.cpp') `
    -o $nativeTest
if ($LASTEXITCODE -ne 0) { throw 'Thread join regression compilation failed.' }

& $AdbPath connect $Serial | Out-Null
& $AdbPath -s $Serial push $nativeTest /data/local/tmp/thread-join-test | Out-Null
& $AdbPath -s $Serial shell chmod 755 /data/local/tmp/thread-join-test
$result = (& $AdbPath -s $Serial shell /data/local/tmp/thread-join-test) -join "`n"
if ($LASTEXITCODE -ne 0 -or $result -notmatch 'thread_join_regression result=pass') {
    throw "Thread join regression failed:`n$result"
}
Write-Host $result.Trim()
