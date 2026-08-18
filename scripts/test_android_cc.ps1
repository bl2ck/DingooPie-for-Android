param(
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555',
    [Parameter(Mandatory = $true)][string]$GamePath,
    [string]$AppGamePath,
    [string]$OutputDirectory,
    [switch]$SkipBuild,
    [switch]$SkipInstall,
    [switch]$VerifyDoudizhuDeal,
    [switch]$VerifyDoudizhuExit,
    [switch]$VerifySharedSettings
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if ($VerifyDoudizhuDeal -and $VerifyDoudizhuExit) {
    throw 'VerifyDoudizhuDeal and VerifyDoudizhuExit must run separately.'
}

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
$adb = $AdbPath
if (!$adb -and $Serial -eq '127.0.0.1:7555') {
    $mumuAdbCandidates = @(
        'D:\Program Files\MuMu\emulator\nemu\vmonitor\bin\adb_server.exe',
        'C:\Program Files\Netease\MuMuPlayerGlobal-12.0\shell\adb.exe',
        'C:\Program Files\Netease\MuMu Player 12\shell\adb.exe'
    )
    $adb = $mumuAdbCandidates |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
}
if (!$adb) {
    $adb = Join-Path $resolvedSdkRoot 'platform-tools\adb.exe'
}
if (!(Test-Path -LiteralPath $adb)) {
    throw "ADB was not found: $adb"
}
$adb = (Resolve-Path -LiteralPath $adb).Path
if (!(Test-Path -LiteralPath $GamePath)) {
    throw "CC game was not found: $GamePath"
}
if ($AppGamePath -and !(Test-Path -LiteralPath $AppGamePath)) {
    throw "APP regression game was not found: $AppGamePath"
}
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot '.tools\cc-automation'
}
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$resolvedOutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $adb -s $Serial @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    if ($exitCode -ne 0) {
        throw "ADB command failed: $($Arguments -join ' ')`n$($output -join "`n")"
    }
    return $output
}

function Await-WinRt {
    param($Operation, [Type]$ResultType)
    $method = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and
            $_.GetParameters().Count -eq 1
        } |
        Select-Object -First 1
    $task = $method.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    $task.Wait()
    return $task.Result
}

function Get-WindowsOcrText {
    param([string]$ImagePath)
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
    $null = [Windows.Storage.FileAccessMode, Windows.Storage, ContentType = WindowsRuntime]
    $null = [Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime]
    $null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
    $null = [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
    $null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
    $null = [Windows.Media.Ocr.OcrResult, Windows.Foundation, ContentType = WindowsRuntime]
    $null = [Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime]

    $file = Await-WinRt `
        ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ImagePath)) `
        ([Windows.Storage.StorageFile])
    $stream = Await-WinRt `
        ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) `
        ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await-WinRt `
        ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) `
        ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await-WinRt `
        ($decoder.GetSoftwareBitmapAsync()) `
        ([Windows.Graphics.Imaging.SoftwareBitmap])
    $language = [Windows.Globalization.Language]::new('zh-Hans-CN')
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($language)
    if (!$engine) {
        throw 'Windows Simplified Chinese OCR language is unavailable.'
    }
    $result = Await-WinRt `
        ($engine.RecognizeAsync($bitmap)) `
        ([Windows.Media.Ocr.OcrResult])
    return $result.Text
}

function Invoke-VirtualControlHold {
    param([int]$X, [int]$Y)
    Invoke-Adb -Arguments @(
        'shell', 'input', 'swipe', $X.ToString(), $Y.ToString(),
        $X.ToString(), $Y.ToString(), '350') | Out-Null
    Start-Sleep -Milliseconds 250
}

function Save-DeviceScreenshot {
    param([string]$DevicePath, [string]$HostPath)
    Invoke-Adb -Arguments @('shell', 'screencap', '-p', $DevicePath) | Out-Null
    Invoke-Adb -Arguments @('pull', $DevicePath, $HostPath) | Out-Null
    Invoke-Adb -Arguments @('shell', 'rm', $DevicePath) | Out-Null
}

function Start-AutomationGameAndReadNativeLog {
    param([string]$DeviceGamePath, [int]$WaitSeconds = 15)
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'am', 'start', '-W',
        '-n', 'com.dingoopie.android/.DingooPieActivity',
        '--es', 'dingoopie.game_automation_path', $DeviceGamePath) | Out-Null
    Start-Sleep -Seconds $WaitSeconds
    return (Invoke-Adb -Arguments @(
        'shell', 'run-as', 'com.dingoopie.android',
        'cat', 'logs/dingoopie-native.log')) -join "`r`n"
}

function Assert-NativeLogContains {
    param([string]$LogText, [string[]]$Expected, [string]$RuntimeName)
    foreach ($value in $Expected) {
        if (!$LogText.Contains($value)) {
            throw "$RuntimeName shared-setting evidence is missing: $value"
        }
    }
}

function Get-CyanDealSampleCount {
    param([string]$ImagePath)
    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($ImagePath)
    try {
        if ($bitmap.Width -ne 960 -or $bitmap.Height -ne 540) {
            throw "Doudizhu deal verification requires a 960x540 MuMu screenshot."
        }
        $count = 0
        for ($y = 190; $y -lt 290; $y += 2) {
            for ($x = 340; $x -lt 710; $x += 2) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.G -ge 100 -and $pixel.B -ge 100 -and $pixel.R -lt 130) {
                    ++$count
                }
            }
        }
        return $count
    }
    finally {
        $bitmap.Dispose()
    }
}

$deviceList = (& $adb devices 2>$null) -join "`n"
if ($deviceList -notmatch "(?m)^$([regex]::Escape($Serial))\s+device$") {
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $connectOutput = & $adb connect $Serial 2>&1
    $connectExitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    if ($connectExitCode -ne 0) {
        throw "ADB could not connect to MuMu at ${Serial}:`n$($connectOutput -join "`n")"
    }
}
$deviceState = (& $adb -s $Serial get-state 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $deviceState.Trim() -ne 'device') {
    throw "MuMu is not connected through ADB at $Serial."
}
$deviceAbi = ((Invoke-Adb -Arguments @('shell', 'getprop', 'ro.product.cpu.abi')) -join '').Trim()
if ($deviceAbi -ne 'x86_64') {
    throw "This MuMu CC test currently requires x86_64; connected ABI is $deviceAbi."
}

$ndkVersion = '26.3.11579264'
$compiler = Join-Path $resolvedSdkRoot `
    "ndk\$ndkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin\x86_64-linux-android23-clang++.cmd"
if (!(Test-Path -LiteralPath $compiler)) {
    throw "Android NDK compiler was not found: $compiler"
}
$timingTest = Join-Path $resolvedOutputDirectory 'cc-runtime-timing-test-x86_64'
& $compiler -std=c++11 -O2 -Wall -Wextra -Werror -static-libstdc++ `
    -I (Join-Path $projectRoot 'native\core') `
    (Join-Path $projectRoot 'tests\cc_runtime_timing_test.cpp') `
    -o $timingTest
if ($LASTEXITCODE -ne 0) {
    throw 'CC runtime timing regression test compilation failed.'
}
Invoke-Adb -Arguments @('push', $timingTest, '/data/local/tmp/cc-runtime-timing-test') | Out-Null
Invoke-Adb -Arguments @('shell', 'chmod', '755', '/data/local/tmp/cc-runtime-timing-test') | Out-Null
$timingResult = (Invoke-Adb -Arguments @(
    'shell', '/data/local/tmp/cc-runtime-timing-test')) -join "`n"
if ($timingResult -notmatch 'regression passed') {
    throw "CC runtime timing regression test failed:`n$timingResult"
}
$armTest = Join-Path $resolvedOutputDirectory 'cc-arm-interpreter-test-x86_64'
& $compiler -std=c++11 -O2 -Wall -Wextra -Werror -static-libstdc++ `
    -I (Join-Path $projectRoot 'native\core') `
    (Join-Path $projectRoot 'tests\cc_arm_interpreter_test.cpp') `
    (Join-Path $projectRoot 'native\core\cc\cpu\arm32_interpreter.cpp') `
    (Join-Path $projectRoot 'native\core\shared\services\guest_package.cpp') `
    -o $armTest
if ($LASTEXITCODE -ne 0) {
    throw 'CC ARM interpreter regression test compilation failed.'
}
Invoke-Adb -Arguments @('push', $armTest, '/data/local/tmp/cc-arm-interpreter-test') | Out-Null
Invoke-Adb -Arguments @('shell', 'chmod', '755', '/data/local/tmp/cc-arm-interpreter-test') | Out-Null

$env:ANDROID_HOME = $resolvedSdkRoot
$env:ANDROID_SDK_ROOT = $resolvedSdkRoot
if (!$SkipBuild) {
    Push-Location $projectRoot
    try {
        & '.\gradlew.bat' --no-daemon assembleDebug
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
if (!$SkipInstall) {
    Invoke-Adb -Arguments @('install', '-r', $apk) | Out-Null
}

$deviceGamePath = '/sdcard/Download/dingoopie-cc-automation.cc'
Invoke-Adb -Arguments @('push', (Resolve-Path -LiteralPath $GamePath).Path, $deviceGamePath) | Out-Null
$armResult = (Invoke-Adb -Arguments @(
    'shell', '/data/local/tmp/cc-arm-interpreter-test', $deviceGamePath)) -join "`n"
if ($LASTEXITCODE -ne 0 -or $armResult -notmatch 'unsupported_pc=0x00000000') {
    throw "CC ARM interpreter regression test failed:`n$armResult"
}
Invoke-Adb -Arguments @('logcat', '-c') | Out-Null
Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null
Invoke-Adb -Arguments @(
    'shell', 'am', 'start', '-W',
    '-n', 'com.dingoopie.android/.DingooPieActivity',
    '--es', 'dingoopie.game_automation_path', $deviceGamePath) | Out-Null
Start-Sleep -Seconds 12

$deviceScreenshot = '/sdcard/dingoopie-cc-automation.png'
$screenshot = Join-Path $resolvedOutputDirectory 'doudizhu-cc.png'
$logPath = Join-Path $resolvedOutputDirectory 'doudizhu-cc.log'
$ocrPath = Join-Path $resolvedOutputDirectory 'doudizhu-cc-ocr.txt'
$nativeLogPath = Join-Path $resolvedOutputDirectory 'doudizhu-cc-native.log'
Save-DeviceScreenshot -DevicePath $deviceScreenshot -HostPath $screenshot
$logText = (Invoke-Adb -Arguments @('logcat', '-d', '-v', 'threadtime')) -join "`r`n"
[System.IO.File]::WriteAllText($logPath, $logText, [System.Text.UTF8Encoding]::new($false))
if ($logText -match 'FATAL EXCEPTION|Fatal signal|ANR in com\.dingoopie\.android') {
    throw "Android runtime failure detected. See $logPath"
}
$nativeLogText = (Invoke-Adb -Arguments @(
    'shell', 'run-as', 'com.dingoopie.android',
    'cat', 'logs/dingoopie-native.log')) -join "`r`n"
[System.IO.File]::WriteAllText(
    $nativeLogPath, $nativeLogText, [System.Text.UTF8Encoding]::new($false))
if ($nativeLogText -notmatch 'cc-arm: timing runtime_scale=') {
    throw "CC runtime did not apply the shared emulator timing settings. See $nativeLogPath"
}

$ocrText = Get-WindowsOcrText -ImagePath $screenshot
[System.IO.File]::WriteAllText($ocrPath, $ocrText, [System.Text.UTF8Encoding]::new($false))
$normalizedOcr = [regex]::Replace($ocrText, '[^\p{L}\p{N}]', '')
$startGamePrefix = [string][char]0x5f00 + [char]0x59cb + [char]0x6e38
$startGameOcrFallback = [string][char]0x5f00 + [char]0x7684 + [char]0x6e38
if (!$normalizedOcr) {
    throw 'Windows OCR did not recognize any CC game content.'
}
if (($VerifyDoudizhuDeal -or $VerifyDoudizhuExit) -and
    !$normalizedOcr.Contains($startGamePrefix) -and
    !$normalizedOcr.Contains($startGameOcrFallback)) {
    throw "Windows OCR did not recognize the repaired start-game label. OCR: $ocrText"
}

if ($VerifyDoudizhuExit) {
    $buttonAX = 936
    $buttonAY = 270
    $dpadDownX = 60
    $dpadDownY = 306

    1..2 | ForEach-Object { Invoke-VirtualControlHold -X $dpadDownX -Y $dpadDownY }
    $exitSelectionScreenshot = Join-Path $resolvedOutputDirectory 'doudizhu-exit-selected.png'
    Save-DeviceScreenshot `
        -DevicePath '/sdcard/dingoopie-exit-selected.png' `
        -HostPath $exitSelectionScreenshot
    Invoke-VirtualControlHold -X $buttonAX -Y $buttonAY

    $exitNativeLogText = ''
    for ($attempt = 0; $attempt -lt 20; ++$attempt) {
        Start-Sleep -Milliseconds 500
        $exitNativeLogText = (Invoke-Adb -Arguments @(
            'shell', 'run-as', 'com.dingoopie.android',
            'cat', 'logs/dingoopie-native.log')) -join "`r`n"
        if ($exitNativeLogText -match 'game-runtime: CC runtime thread joined') {
            break
        }
    }

    $exitScreenshot = Join-Path $resolvedOutputDirectory 'doudizhu-exit-library.png'
    $exitOcrPath = Join-Path $resolvedOutputDirectory 'doudizhu-exit-library-ocr.txt'
    $exitNativeLogPath = Join-Path $resolvedOutputDirectory 'doudizhu-exit-native.log'
    Save-DeviceScreenshot `
        -DevicePath '/sdcard/dingoopie-exit-library.png' `
        -HostPath $exitScreenshot
    $exitOcrText = Get-WindowsOcrText -ImagePath $exitScreenshot
    [System.IO.File]::WriteAllText(
        $exitOcrPath, $exitOcrText, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText(
        $exitNativeLogPath, $exitNativeLogText, [System.Text.UTF8Encoding]::new($false))

    if ($exitNativeLogText -notmatch
        'cc-runtime: guest completed; returning to library|cc-arm: guest exit requested by (TaskMediaFunStop|OSTaskDel|vxGoHome|abort|av_end_thread|av_queue_abort)' -or
        $exitNativeLogText -notmatch 'frontend: game exit requested; returning to library' -or
        $exitNativeLogText -notmatch 'game-runtime: CC runtime thread joined') {
        throw "CC in-game exit did not complete through the shared frontend path. See $exitNativeLogPath"
    }
    $normalizedExitOcr = [regex]::Replace($exitOcrText, '[^\p{L}\p{N}]', '')
    $addGameText = [string][char]0x6dfb + [char]0x52a0 + [char]0x6e38 + [char]0x620f
    if ($normalizedExitOcr -notmatch '(?i)Dingoopie' -or
        !$normalizedExitOcr.Contains($addGameText)) {
        throw "CC in-game exit did not return to the visual game library. OCR: $exitOcrText"
    }
    Write-Host "CC in-game exit screenshot: $exitScreenshot"
    Write-Host "CC in-game exit OCR: $exitOcrText"
}

if ($VerifyDoudizhuDeal) {
    $buttonAX = 936
    $buttonAY = 270
    $dpadDownX = 60
    $dpadDownY = 306
    $dpadRightX = 95
    $dpadRightY = 270

    Invoke-VirtualControlHold -X $buttonAX -Y $buttonAY
    Start-Sleep -Seconds 2
    Invoke-VirtualControlHold -X $buttonAX -Y $buttonAY
    Start-Sleep -Seconds 2
    Invoke-VirtualControlHold -X $buttonAX -Y $buttonAY
    Start-Sleep -Seconds 2
    Invoke-VirtualControlHold -X $buttonAX -Y $buttonAY
    1..3 | ForEach-Object { Invoke-VirtualControlHold -X $dpadDownX -Y $dpadDownY }
    1..4 | ForEach-Object { Invoke-VirtualControlHold -X $dpadRightX -Y $dpadRightY }
    Invoke-VirtualControlHold -X $buttonAX -Y $buttonAY
    Start-Sleep -Seconds 2
    Invoke-VirtualControlHold -X $buttonAX -Y $buttonAY

    Start-Sleep -Milliseconds 700
    $dealEarly = Join-Path $resolvedOutputDirectory 'doudizhu-deal-early.png'
    Save-DeviceScreenshot -DevicePath '/sdcard/doudizhu-deal-early.png' -HostPath $dealEarly
    Start-Sleep -Seconds 5
    $dealComplete = Join-Path $resolvedOutputDirectory 'doudizhu-deal-complete.png'
    Save-DeviceScreenshot -DevicePath '/sdcard/doudizhu-deal-complete.png' -HostPath $dealComplete
    $earlyCyan = Get-CyanDealSampleCount -ImagePath $dealEarly
    $completeCyan = Get-CyanDealSampleCount -ImagePath $dealComplete
    if ($earlyCyan -lt 1500) {
        throw "Doudizhu dealing completed too quickly or navigation failed; early cyan samples=$earlyCyan."
    }
    if ($completeCyan -ge 1000 -or $earlyCyan -lt $completeCyan * 2) {
        throw "Doudizhu dealing did not complete in the expected window; early=$earlyCyan complete=$completeCyan."
    }
    Write-Host "Deal timing samples: early=$earlyCyan complete=$completeCyan"
}

if ($AppGamePath) {
    $deviceAppPath = '/sdcard/Download/dingoopie-app-regression.app'
    Invoke-Adb -Arguments @(
        'push', (Resolve-Path -LiteralPath $AppGamePath).Path, $deviceAppPath) | Out-Null
    Invoke-Adb -Arguments @('logcat', '-c') | Out-Null
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'am', 'start', '-W',
        '-n', 'com.dingoopie.android/.DingooPieActivity',
        '--es', 'dingoopie.game_automation_path', $deviceAppPath) | Out-Null
    Start-Sleep -Seconds 15
    $appScreenshot = Join-Path $resolvedOutputDirectory 'app-regression.png'
    $appLogPath = Join-Path $resolvedOutputDirectory 'app-regression.log'
    $appNativeLogPath = Join-Path $resolvedOutputDirectory 'app-regression-native.log'
    Save-DeviceScreenshot `
        -DevicePath '/sdcard/dingoopie-app-regression.png' `
        -HostPath $appScreenshot
    $appLogText = (Invoke-Adb -Arguments @(
        'logcat', '-d', '-v', 'threadtime')) -join "`r`n"
    [System.IO.File]::WriteAllText(
        $appLogPath, $appLogText, [System.Text.UTF8Encoding]::new($false))
    if ($appLogText -match 'FATAL EXCEPTION|Fatal signal|ANR in com\.dingoopie\.android') {
        throw "APP runtime failure detected. See $appLogPath"
    }
    $appNativeLogText = (Invoke-Adb -Arguments @(
        'shell', 'run-as', 'com.dingoopie.android',
        'cat', 'logs/dingoopie-native.log')) -join "`r`n"
    [System.IO.File]::WriteAllText(
        $appNativeLogPath, $appNativeLogText, [System.Text.UTF8Encoding]::new($false))
    if ($appNativeLogText -match 'execution backend: ARM32 interpreter' -or
        $appNativeLogText -notmatch 'execution backend effective:' -or
        $appNativeLogText -notmatch 'hle: runtime speed scale') {
        throw "APP regression did not use its existing backend and runtime settings. See $appNativeLogPath"
    }
    Write-Host "APP regression screenshot: $appScreenshot"
}

if ($VerifySharedSettings) {
    $settingsPath = Join-Path $resolvedOutputDirectory 'shared-settings.ini'
    $cheatPath = Join-Path $resolvedOutputDirectory 'dingoopie-cc-automation.cc.cht'
    $settingsText = @'
[video]
anti_aliasing=low
effect=sepia
brightness=125
contrast=90
gamma=110
saturation=150
minimized_behavior=throttle
screen_orientation=landscape
show_fps=1
[audio]
volume_percent=75
buffer_samples=4096
effect=bass_boost
audio_disabled=1
[input]
system_ime_disabled=0
show_virtual_controls=1
controller_mapping=A=B
keyboard_mapping=space=A
[runtime]
backend=compatibility
cpu_hz=360000000
speed_scale=0.80
ostimedly_scale=0.75
cheats_enabled=1
[cheats]
dingoopie-cc-automation.cc.cht=CC Shared Settings
[ui]
language=english
[debug]
profile=1
'@
    [System.IO.File]::WriteAllText(
        $settingsPath,
        ($settingsText -replace "`r?`n", "`r`n"),
        [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText(
        $cheatPath,
        "on|CC Shared Settings|u16|0x13FFFFFE|0x5AA5`r`n",
        [System.Text.UTF8Encoding]::new($false))

    Invoke-Adb -Arguments @(
        'shell', 'run-as', 'com.dingoopie.android',
        'cp', 'files/DingooPie.ini', 'files/DingooPie.ini.cc-settings-backup') | Out-Null
    try {
        Invoke-Adb -Arguments @('push', $settingsPath, '/data/local/tmp/DingooPie.ini') | Out-Null
        Invoke-Adb -Arguments @('shell', 'chmod', '644', '/data/local/tmp/DingooPie.ini') | Out-Null
        Invoke-Adb -Arguments @(
            'shell', 'run-as', 'com.dingoopie.android',
            'cp', '/data/local/tmp/DingooPie.ini', 'files/DingooPie.ini') | Out-Null
        Invoke-Adb -Arguments @(
            'shell', 'rm', '-f', '/sdcard/Download/dingoopie-cc-automation.cc.cht') | Out-Null
        $missingCheatLog = Start-AutomationGameAndReadNativeLog -DeviceGamePath $deviceGamePath
        [System.IO.File]::WriteAllText(
            (Join-Path $resolvedOutputDirectory 'shared-settings-cc-no-cheat-native.log'),
            $missingCheatLog,
            [System.Text.UTF8Encoding]::new($false))
        Assert-NativeLogContains -RuntimeName 'CC without matching cheat' `
            -LogText $missingCheatLog -Expected @(
                'cc-arm: game settings cheats_enabled=0 cheats_available=0 cheat_entries=0 cheat_startup_applied=0'
            )

        Invoke-Adb -Arguments @(
            'push', $cheatPath, '/sdcard/Download/dingoopie-cc-automation.cc.cht') | Out-Null

        $sharedCcLog = Start-AutomationGameAndReadNativeLog -DeviceGamePath $deviceGamePath
        [System.IO.File]::WriteAllText(
            (Join-Path $resolvedOutputDirectory 'shared-settings-cc-native.log'),
            $sharedCcLog,
            [System.Text.UTF8Encoding]::new($false))
        Assert-NativeLogContains -RuntimeName 'CC' -LogText $sharedCcLog -Expected @(
            'cc-arm: settings requested_backend=compatibility effective_backend=arm32_interpreter execution_mode=base cpu_clock=360000000 target_ips=16071428 runtime_scale=0.800 delay_scale=0.750',
            'cc-arm: compatibility mode uses base ARM32 execution paths',
            'frontend: video settings anti_aliasing=low effect=sepia brightness=125 contrast=90 gamma=110 saturation=150 minimized_behavior=throttle screen_orientation=landscape screen_fill=aspect portrait=0 show_fps=1',
            'frontend: audio settings volume=75 buffer_samples=4096 effect=bass_boost digital_noise_reduction=high audio_disabled=1',
            'frontend: input settings system_ime_disabled=0 virtual_controls=1 virtual_control_scale=100 virtual_dpad_type=joystick controller_mapping=A=B keyboard_mapping=space=A',
            'cheat: loaded 1 code(s), parse_errors=0, enabled=1, sha_mismatch=0, source=dingoopie-cc-automation.cc.cht',
            'cc-arm: game settings cheats_enabled=1 cheats_available=1 cheat_entries=1 cheat_startup_applied=1'
        )
        if ($sharedCcLog -match 'crash-log:wrote|cc-runtime: execution failed|task failed result=') {
            throw "CC shared-settings automation crashed. See shared-settings-cc-native.log"
        }

        if ($AppGamePath) {
            $sharedAppLog = Start-AutomationGameAndReadNativeLog -DeviceGamePath $deviceAppPath
            [System.IO.File]::WriteAllText(
                (Join-Path $resolvedOutputDirectory 'shared-settings-app-native.log'),
                $sharedAppLog,
                [System.Text.UTF8Encoding]::new($false))
            Assert-NativeLogContains -RuntimeName 'APP' -LogText $sharedAppLog -Expected @(
                'execution backend effective: compatibility',
                'frontend: video settings anti_aliasing=low effect=sepia brightness=125 contrast=90 gamma=110 saturation=150 minimized_behavior=throttle screen_orientation=landscape screen_fill=aspect portrait=0 show_fps=1',
                'frontend: audio settings volume=75 buffer_samples=4096 effect=bass_boost digital_noise_reduction=high audio_disabled=1',
                'frontend: input settings system_ime_disabled=0 virtual_controls=1 virtual_control_scale=100 virtual_dpad_type=joystick controller_mapping=A=B keyboard_mapping=space=A',
                'hle: runtime speed scale 0.800 env',
                'hle: host delay scale 0.750 env'
            )
        }
        $autoSettingsText = $settingsText -replace 'backend=compatibility', 'backend='
        [System.IO.File]::WriteAllText(
            $settingsPath,
            ($autoSettingsText -replace "`r?`n", "`r`n"),
            [System.Text.UTF8Encoding]::new($false))
        Invoke-Adb -Arguments @('push', $settingsPath, '/data/local/tmp/DingooPie.ini') | Out-Null
        Invoke-Adb -Arguments @(
            'shell', 'run-as', 'com.dingoopie.android',
            'cp', '/data/local/tmp/DingooPie.ini', 'files/DingooPie.ini') | Out-Null
        $autoCcLog = Start-AutomationGameAndReadNativeLog -DeviceGamePath $deviceGamePath
        Assert-NativeLogContains -RuntimeName 'CC automatic backend' `
            -LogText $autoCcLog -Expected @(
                'cc-arm: settings requested_backend=auto effective_backend=arm32_interpreter execution_mode=optimized'
            )
        if ($AppGamePath) {
            $autoAppLog = Start-AutomationGameAndReadNativeLog -DeviceGamePath $deviceAppPath
            Assert-NativeLogContains -RuntimeName 'APP automatic backend' `
                -LogText $autoAppLog -Expected @(
                    'app-runtime: execution backend: ppsspp_irjit',
                    'execution backend effective: ppsspp_irjit',
                    'settings-trace:loaded runtime.backend=auto runtime.cpu_hz=360000000',
                    'profile:irjit ',
                    'clock_hz=360000000'
                )
        }
        Write-Host 'Shared CC/APP emulator settings and backend mode verification passed.'
    }
    finally {
        Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null
        Invoke-Adb -Arguments @(
            'shell', 'run-as', 'com.dingoopie.android',
            'cp', 'files/DingooPie.ini.cc-settings-backup', 'files/DingooPie.ini') | Out-Null
        Invoke-Adb -Arguments @(
            'shell', 'run-as', 'com.dingoopie.android',
            'rm', 'files/DingooPie.ini.cc-settings-backup') | Out-Null
        Invoke-Adb -Arguments @(
            'shell', 'rm', '-f', '/data/local/tmp/DingooPie.ini',
            '/sdcard/Download/dingoopie-cc-automation.cc.cht') | Out-Null
    }
}

Write-Host $nativeResult
Write-Host $mathResult
Write-Host $timingResult
Write-Host $armResult
Write-Host "OCR: $ocrText"
Write-Host "Screenshot: $screenshot"
Write-Host "Log: $logPath"
Write-Host 'CC MuMu rendering automation passed.'
