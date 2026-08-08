param(
    [Parameter(Mandatory = $true)][string]$GamePath,
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555',
    [string]$OutputDirectory,
    [switch]$SkipBuild,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!(Test-Path -LiteralPath $GamePath)) { throw "CC game was not found: $GamePath" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $projectRoot '.tools\tiandidao-save-restart' }
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
if (!$AndroidSdkRoot) {
    $sdkLine = Get-Content (Join-Path $projectRoot 'local.properties') |
        Where-Object { $_ -match '^sdk\.dir=' } | Select-Object -First 1
    if ($sdkLine) { $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).Replace('\:', ':').Replace('\\', '\') }
}
if (!$AndroidSdkRoot) { $AndroidSdkRoot = $env:ANDROID_SDK_ROOT }
if (!$AndroidSdkRoot) { throw 'Android SDK is required.' }
if (!$AdbPath) { $AdbPath = Join-Path $AndroidSdkRoot 'platform-tools\adb.exe' }
$AdbPath = (Resolve-Path -LiteralPath $AdbPath).Path
$packageName = 'com.dingoopie.android'
$deviceGamePath = '/sdcard/Download/tiandidao-cc-automation.cc'

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $output = & $AdbPath -s $Serial @Arguments 2>&1; $exitCode = $LASTEXITCODE }
    finally { $ErrorActionPreference = $old }
    if ($exitCode -ne 0) { throw "ADB failed: adb $($Arguments -join ' ')`n$($output -join "`n")" }
    return $output
}

function Capture-Screenshot {
    param([string]$Name)
    $devicePath = "/sdcard/tiandidao-$Name.png"
    $hostPath = Join-Path $OutputDirectory "$Name.png"
    Invoke-Adb -Arguments @('shell', 'screencap', '-p', $devicePath) | Out-Null
    Invoke-Adb -Arguments @('pull', $devicePath, $hostPath) | Out-Null
    return $hostPath
}

function Tap-UiButton1 {
    $xmlDevicePath = '/sdcard/tiandidao-window.xml'
    $xmlHostPath = Join-Path $OutputDirectory 'window.xml'
    Invoke-Adb -Arguments @('shell', 'uiautomator', 'dump', $xmlDevicePath) | Out-Null
    Invoke-Adb -Arguments @('pull', $xmlDevicePath, $xmlHostPath) | Out-Null
    $xml = Get-Content -LiteralPath $xmlHostPath -Raw
    $match = [regex]::Match($xml, 'resource-id="android:id/button1"[^>]*bounds="(\[\d+,\d+\]\[\d+,\d+\])"')
    if (!$match.Success) { throw 'Android confirmation button was not found.' }
    if ($match.Groups[1].Value -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw 'Android confirmation button bounds are invalid.'
    }
    $x = [int](([int]$Matches[1] + [int]$Matches[3]) / 2)
    $y = [int](([int]$Matches[2] + [int]$Matches[4]) / 2)
    Invoke-Adb -Arguments @('shell', 'input', 'tap', $x, $y) | Out-Null
}

function Start-Game {
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', $packageName) | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'am', 'start', '-W', '-n', "$packageName/.DingooPieActivity",
        '--es', 'dingoopie.game_automation_path', $deviceGamePath) | Out-Null
}
$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
if (!$SkipBuild) {
    & (Join-Path $projectRoot 'gradlew.bat') --no-daemon assembleDebug
    if ($LASTEXITCODE -ne 0) { throw 'Debug APK build failed.' }
}
if (!$SkipInstall) { Invoke-Adb install -r $apk | Out-Null }
Invoke-Adb push $GamePath $deviceGamePath | Out-Null
Invoke-Adb -Arguments @('shell', 'run-as', $packageName, 'rm', '-rf', 'files/saves') | Out-Null
Invoke-Adb -Arguments @('shell', 'logcat', '-c') | Out-Null

Start-Game
Start-Sleep -Seconds 4
Invoke-Adb -Arguments @('shell', 'input', 'tap', 936, 270) | Out-Null
Start-Sleep -Seconds 30
Invoke-Adb -Arguments @('shell', 'input', 'tap', 936, 270) | Out-Null
Start-Sleep -Seconds 5
$playScreenshot = Capture-Screenshot 'playable'

Invoke-Adb -Arguments @('shell', 'input', 'keyevent', 4) | Out-Null
Start-Sleep -Milliseconds 700
Invoke-Adb -Arguments @('shell', 'input', 'tap', 480, 139) | Out-Null
Start-Sleep -Milliseconds 700
Invoke-Adb -Arguments @('shell', 'input', 'tap', 190, 139) | Out-Null
Invoke-Adb -Arguments @('shell', 'input', 'tap', 300, 399) | Out-Null
Start-Sleep -Milliseconds 500
Tap-UiButton1
Start-Sleep -Milliseconds 800
Tap-UiButton1
Start-Sleep -Seconds 2
$saveScreenshot = Capture-Screenshot 'saved'

Invoke-Adb -Arguments @('shell', 'input', 'keyevent', 4) | Out-Null
Start-Sleep -Milliseconds 700
Start-Game
Start-Sleep -Seconds 4
Invoke-Adb -Arguments @('shell', 'input', 'tap', 936, 270) | Out-Null
Start-Sleep -Seconds 30
Invoke-Adb -Arguments @('shell', 'input', 'tap', 936, 270) | Out-Null
Start-Sleep -Seconds 5
$restartedScreenshot = Capture-Screenshot 'restarted'

Invoke-Adb -Arguments @('shell', 'input', 'keyevent', 4) | Out-Null
Start-Sleep -Milliseconds 700
Invoke-Adb -Arguments @('shell', 'input', 'tap', 480, 139) | Out-Null
Start-Sleep -Milliseconds 700
Invoke-Adb -Arguments @('shell', 'input', 'tap', 190, 139) | Out-Null
Invoke-Adb -Arguments @('shell', 'input', 'tap', 650, 399) | Out-Null
Start-Sleep -Milliseconds 500
Tap-UiButton1
Start-Sleep -Seconds 3
$resultScreenshot = Capture-Screenshot 'loaded'
Tap-UiButton1
Start-Sleep -Seconds 5
$resumedScreenshot = Capture-Screenshot 'resumed'

$nativeLog = (Invoke-Adb -Arguments @('shell', 'run-as', $packageName, 'cat', 'logs/dingoopie-native.log')) -join "`n"
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'native.log'), $nativeLog, [Text.UTF8Encoding]::new($false))
$processId = ((Invoke-Adb -Arguments @('shell', 'pidof', $packageName)) -join '').Trim()
if (!$processId) { throw "DingooPie process exited after CC save-state load.`n$nativeLog" }
if ($nativeLog -notmatch 'cc-arm: save-state restored tasks=') {
    throw "Native log did not confirm CC save-state restoration.`n$nativeLog"
}
Write-Host 'TiandiDao CC save-restart-load automation passed.'
Write-Host "Playable screenshot: $playScreenshot"
Write-Host "Saved screenshot: $saveScreenshot"
Write-Host "Restarted screenshot: $restartedScreenshot"
Write-Host "Loaded screenshot: $resultScreenshot"
Write-Host "Resumed screenshot: $resumedScreenshot"
Write-Host "Native log: $(Join-Path $OutputDirectory 'native.log')"
Invoke-Adb -Arguments @('shell', 'am', 'force-stop', $packageName) | Out-Null
