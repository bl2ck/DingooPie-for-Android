param(
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial
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
            $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).Replace('\\:', ':')
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
$adb = if ($AdbPath) {
    (Resolve-Path -LiteralPath $AdbPath).Path
} else {
    Join-Path $resolvedSdkRoot 'platform-tools\adb.exe'
}
if (!(Test-Path -LiteralPath $adb)) {
    throw "ADB was not found: $adb"
}

if (!$Serial) {
    $connectedDevices = @(& $adb devices |
        Select-Object -Skip 1 |
        ForEach-Object { if ($_ -match '^(\S+)\s+device$') { $Matches[1] } } |
        Where-Object { $_ })
    if ($connectedDevices.Count -ne 1) {
        throw "Exactly one Android device must be connected; found $($connectedDevices.Count)."
    }
    $Serial = $connectedDevices[0]
}

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & $adb -s $Serial @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "ADB command failed: $($Arguments -join ' ')"
    }
}

function Test-ImeMode {
    param(
        [bool]$Disabled,
        [string]$ExpectedResult
    )
    # Triggers the debug-only IME automation path in the activity.
    Invoke-Adb -Arguments @('logcat', '-c')
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android')
    $disabledValue = if ($Disabled) { 'true' } else { 'false' }
    Invoke-Adb -Arguments @(
        'shell', 'am', 'start', '-W',
        '-n', 'com.dingoopie.android/.DingooPieActivity',
        '--ez', 'dingoopie.ime_automation', 'true',
        '--ez', 'dingoopie.ime_disabled', $disabledValue) | Out-Null
    Start-Sleep -Seconds 3
    $result = Invoke-Adb -Arguments @(
        'logcat', '-d', '-v', 'brief', '-s', 'DingooPie:I', 'SDL:I', '*:S') |
        Select-String 'IME_AUTOMATION' |
        Select-Object -Last 1
    if (!$result -or $result.Line -notlike "*$ExpectedResult*") {
        throw "Unexpected IME automation result for disabled=${disabledValue}: $result"
    }
    Write-Host $result.Line
}

$env:ANDROID_HOME = $resolvedSdkRoot
$env:ANDROID_SDK_ROOT = $resolvedSdkRoot
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

$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
Invoke-Adb -Arguments @('install', '-r', $apk) | Out-Null
try {
    Test-ImeMode -Disabled $false `
        -ExpectedResult 'disabled=false request_accepted=true keyboard_shown=true'
    Test-ImeMode -Disabled $true `
        -ExpectedResult 'disabled=true request_accepted=false keyboard_shown=false'

    $imeState = Invoke-Adb -Arguments @('shell', 'dumpsys', 'input_method') |
        Select-String 'mShowRequested=.*mInputShown=false' |
        Select-Object -First 1
    if (!$imeState) {
        throw 'Android input method remained visible after IME was disabled.'
    }
}
finally {
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android')
    Invoke-Adb -Arguments @(
        'shell', 'monkey', '-p', 'com.dingoopie.android',
        '-c', 'android.intent.category.LAUNCHER', '1') | Out-Null
}

Write-Host 'Android system IME automation passed.'
