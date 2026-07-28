param(
    [string]$AndroidSdkRoot,
    [string]$Serial = '127.0.0.1:7555',
    [string]$GameName = [string]::Concat([char]0x5929, [char]0x5730, [char]0x9053),
    [string]$GameDirectory = '/sdcard/Download/DingooSample',
    [string]$PcCheatSource = 'D:\Project\C++\dingoo-emu\cheats',
    [switch]$SkipBuild,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

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
if (!$AndroidSdkRoot) {
    $AndroidSdkRoot = $env:ANDROID_SDK_ROOT
}
if (!$AndroidSdkRoot) {
    throw 'Android SDK is required. Pass -AndroidSdkRoot or configure local.properties.'
}

$resolvedSdkRoot = (Resolve-Path -LiteralPath $AndroidSdkRoot).Path
$adb = Join-Path $resolvedSdkRoot 'platform-tools\adb.exe'
$ndkRoot = Join-Path $resolvedSdkRoot 'ndk\26.3.11579264'
if (!(Test-Path -LiteralPath $adb)) {
    throw "ADB was not found: $adb"
}
if (!(Test-Path -LiteralPath $ndkRoot)) {
    throw "Android NDK was not found: $ndkRoot"
}

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $output = & $adb -s $Serial @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ADB command failed: $($Arguments -join ' ')"
    }
    return $output
}

if (!$SkipBuild) {
    Push-Location $projectRoot
    try {
        & '.\gradlew.bat' --no-daemon :app:assembleDebug
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
    finally {
        Pop-Location
    }
}

$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
if (!(Test-Path -LiteralPath $apk)) {
    throw "Debug APK was not found: $apk"
}

& $adb connect $Serial | Out-Null
if (!$SkipInstall) {
    Invoke-Adb -Arguments @('install', '-r', $apk) | Out-Null
}

$cheatFile = Join-Path $PcCheatSource "$GameName.cht"
if (!(Test-Path -LiteralPath $cheatFile)) {
    throw "Cheat file was not found: $cheatFile"
}
$shaLine = Get-Content -LiteralPath $cheatFile |
    Where-Object { $_ -match '^app_sha256=([0-9A-Fa-f]{64})$' } |
    Select-Object -First 1
if (!$shaLine) {
    throw "No app_sha256 was found in $cheatFile"
}
$appSha256 = ([regex]::Match($shaLine, '([0-9A-Fa-f]{64})')).Groups[1].Value

$abi = (Invoke-Adb -Arguments @('shell', 'getprop', 'ro.product.cpu.abi') | Out-String).Trim()
$target = switch ($abi) {
    'arm64-v8a' { 'aarch64-linux-android23' }
    'armeabi-v7a' { 'armv7a-linux-androideabi23' }
    'x86' { 'i686-linux-android23' }
    'x86_64' { 'x86_64-linux-android23' }
    default { throw "Unsupported device ABI: $abi" }
}
$compiler = Join-Path $ndkRoot "toolchains\llvm\prebuilt\windows-x86_64\bin\$target-clang++.cmd"
if (!(Test-Path -LiteralPath $compiler)) {
    throw "NDK compiler was not found: $compiler"
}

$testBinary = Join-Path $env:TEMP 'dingoopie-cheat-engine-test'
& $compiler '-std=c++11' '-O2' '-static-libstdc++' '-I' `
    (Join-Path $projectRoot 'native\core') `
    (Join-Path $projectRoot 'tests\cheat_engine_test.cpp') `
    (Join-Path $projectRoot 'native\core\config\cheat_engine.cpp') `
    (Join-Path $projectRoot 'native\core\game\game_paths.cpp') `
    '-o' $testBinary
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to compile the Android cheat engine test.'
}

$remoteTest = '/data/local/tmp/dingoopie-cheat-engine-test'
$remoteCheat = '/data/local/tmp/dingoopie-cheat-test.cht'
try {
    Invoke-Adb -Arguments @('push', $testBinary, $remoteTest) | Out-Null
    Invoke-Adb -Arguments @('push', $cheatFile, $remoteCheat) | Out-Null
    Invoke-Adb -Arguments @('shell', 'chmod', '755', $remoteTest) | Out-Null
    $nativeResult = Invoke-Adb -Arguments @(
        'shell', $remoteTest, $remoteCheat, $appSha256) | Out-String
    if ($nativeResult -notmatch 'sha_match=1' -or
        $nativeResult -notmatch 'attempted=1 applied=1' -or
        $nativeResult -notmatch 'value=0x12345678') {
        throw "Unexpected native cheat test result: $nativeResult"
    }
    Write-Host $nativeResult.Trim()

    Get-ChildItem -LiteralPath $PcCheatSource -Filter '*.cht' | ForEach-Object {
        Invoke-Adb -Arguments @('push', $_.FullName, "$GameDirectory/$($_.Name)") | Out-Null
    }
    $formatCheatName = "$GameName.app.cht"
    Invoke-Adb -Arguments @(
        'push', $cheatFile, "$GameDirectory/$formatCheatName") | Out-Null

    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'am', 'start', '-W',
        '-n', 'com.dingoopie.android/.DingooPieActivity',
        '--ez', 'dingoopie.cheat_manager_automation', 'true',
        '--es', 'dingoopie.cheat_manager_game_name', $GameName) | Out-Null

    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    $automationLine = $null
    do {
        Start-Sleep -Seconds 1
        $nativeLog = Invoke-Adb -Arguments @(
            'exec-out', 'run-as', 'com.dingoopie.android',
            'cat', 'files/dingoopie-native.log') | Out-String
        $automationLine = $nativeLog -split "`r?`n" |
            Where-Object { $_ -like 'CHEAT_MANAGER_AUTOMATION result=*' } |
            Select-Object -Last 1
    } while (!$automationLine -and [DateTime]::UtcNow -lt $deadline)

    if (!$automationLine -or $automationLine -notlike 'CHEAT_MANAGER_AUTOMATION result=pass*') {
        throw "Cheat automation did not pass: $automationLine"
    }
    $expectedLoadPattern = 'cheat: loaded 12 code\(s\), parse_errors=0,.*source=' +
        [regex]::Escape($formatCheatName)
    if ($nativeLog -notmatch $expectedLoadPattern) {
        throw "The APP-specific $formatCheatName load was not confirmed in the native log."
    }
    Write-Host $automationLine
}
finally {
    if (Test-Path -LiteralPath $testBinary) {
        Remove-Item -LiteralPath $testBinary
    }
    Invoke-Adb -Arguments @('shell', 'rm', '-f', $remoteTest, $remoteCheat) | Out-Null
    if ($formatCheatName) {
        Invoke-Adb -Arguments @(
            'shell', 'rm', '-f', "$GameDirectory/$formatCheatName") | Out-Null
    }
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'monkey', '-p', 'com.dingoopie.android',
        '-c', 'android.intent.category.LAUNCHER', '1') | Out-Null
}

Write-Host 'Android cheat automation passed, including library file switching.'
