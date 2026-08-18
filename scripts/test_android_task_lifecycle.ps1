param(
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555',
    [int]$TaskCount = 4000
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ndkVersion = '26.3.11579264'

if (!$AndroidSdkRoot) {
    $localProperties = Join-Path $projectRoot 'local.properties'
    if (Test-Path -LiteralPath $localProperties) {
        $sdkLine = Get-Content -LiteralPath $localProperties |
            Where-Object { $_ -match '^sdk\.dir=' } |
            Select-Object -First 1
        if ($sdkLine) {
            $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).Replace('\:', ':')
        }
    }
}
if (!$AndroidSdkRoot) { $AndroidSdkRoot = $env:ANDROID_SDK_ROOT }
if (!$AndroidSdkRoot) {
    throw 'Android SDK is required. Pass -AndroidSdkRoot or configure local.properties.'
}

$resolvedSdkRoot = (Resolve-Path -LiteralPath $AndroidSdkRoot).Path
if (!$AdbPath) { $AdbPath = Join-Path $resolvedSdkRoot 'platform-tools\adb.exe' }
if (!(Test-Path -LiteralPath $AdbPath)) { throw "ADB was not found: $AdbPath" }
$adb = (Resolve-Path -LiteralPath $AdbPath).Path
$compiler = Join-Path $resolvedSdkRoot "ndk\$ndkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin\x86_64-linux-android23-clang++.cmd"
if (!(Test-Path -LiteralPath $compiler)) { throw "Android NDK compiler was not found: $compiler" }
if ($TaskCount -lt 1) { throw 'TaskCount must be positive.' }

$outputDirectory = Join-Path $projectRoot '.tools\task-lifecycle'
$null = New-Item -ItemType Directory -Force -Path $outputDirectory
$nativeTest = Join-Path $outputDirectory 'task-thread-lifecycle-test-x86_64'
$includeRoot = Join-Path $projectRoot 'native\core'
$testSource = Join-Path $projectRoot 'tests\task_thread_lifecycle_test.cpp'
$lifecycleSource = Join-Path $projectRoot 'native\core\app\hle\app_task_lifecycle.cpp'

& $compiler -std=c++17 -O2 -pthread -static-libstdc++ `
    -I $includeRoot $testSource $lifecycleSource -o $nativeTest
if ($LASTEXITCODE -ne 0) { throw 'Task thread lifecycle test compilation failed.' }

& $adb connect $Serial | Out-Null
& $adb -s $Serial push $nativeTest /data/local/tmp/task-thread-lifecycle-test | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Unable to push task thread lifecycle test.' }
& $adb -s $Serial shell chmod 755 /data/local/tmp/task-thread-lifecycle-test
$result = (& $adb -s $Serial shell /data/local/tmp/task-thread-lifecycle-test $TaskCount) -join "`n"
if ($LASTEXITCODE -ne 0 -or $result -notmatch 'task_thread_lifecycle passed') {
    throw "Task thread lifecycle regression failed:`n$result"
}
Write-Host $result.Trim()
