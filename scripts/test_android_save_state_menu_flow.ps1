param(
    [string]$AndroidSdkRoot
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
            $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).
                Replace('\:', ':').Replace('\\', '\')
        }
    }
}
if (!$AndroidSdkRoot) { $AndroidSdkRoot = $env:ANDROID_SDK_ROOT }
if (!$AndroidSdkRoot) {
    throw 'Android SDK is required. Pass -AndroidSdkRoot or configure local.properties.'
}

$resolvedSdkRoot = (Resolve-Path -LiteralPath $AndroidSdkRoot).Path
$compiler = Join-Path $resolvedSdkRoot `
    "ndk\$ndkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin\x86_64-linux-android23-clang++.cmd"
if (!(Test-Path -LiteralPath $compiler)) {
    throw "Android NDK compiler was not found: $compiler"
}

$includeRoot = Join-Path $projectRoot 'native\core'
$testSource = Join-Path $projectRoot 'tests\save_state_menu_flow_test.cpp'
& $compiler -std=c++17 -fsyntax-only -I $includeRoot $testSource
if ($LASTEXITCODE -ne 0) {
    throw 'Save-state menu flow regression test failed.'
}

Write-Host 'Save-state menu flow regression passed.'
