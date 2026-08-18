param(
    [Parameter(Mandatory = $true)][string]$GamePath,
    [string]$Serial = '127.0.0.1:7555',
    [string]$AdbPath,
    [int]$DurationSeconds = 30,
    [string]$InputSequence,
    [int]$InputStartDelaySeconds = 3,
    [int]$InputIntervalMilliseconds = 700,
    [string]$OutputDirectory,
    [switch]$SkipBuild,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!(Test-Path -LiteralPath $GamePath -PathType Leaf)) {
    throw "Game was not found: $GamePath"
}
if (!$OutputDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path $projectRoot ".tools\audio-validation-$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

if (!$AdbPath) {
    $candidates = @(
        'D:\Program Files\MuMu\emulator\nemu\vmonitor\bin\adb_server.exe',
        (Get-Command adb.exe -ErrorAction SilentlyContinue).Source
    ) | Where-Object { $_ }
    $AdbPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if (!$AdbPath) {
    throw 'ADB was not found. Pass -AdbPath.'
}

function Invoke-Adb {
    param([string[]]$Arguments)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $AdbPath -s $Serial @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "ADB failed: $($Arguments -join ' ')`n$($output -join "`n")"
    }
    return $output
}

function Save-RunAsFile {
    param([string]$DeviceRelativePath, [string]$HostPath)
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $AdbPath
    $startInfo.Arguments = "-s $Serial exec-out run-as com.dingoopie.android cat $DeviceRelativePath"
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    [void]$process.Start()
    $file = [System.IO.File]::Create($HostPath)
    try {
        $process.StandardOutput.BaseStream.CopyTo($file)
    } finally {
        $file.Dispose()
    }
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "Could not read $DeviceRelativePath`: $errorText"
    }
}

if (!$SkipBuild) {
    Push-Location $projectRoot
    try {
        & '.\gradlew.bat' --no-daemon assembleDebug
        if ($LASTEXITCODE -ne 0) {
            throw 'Debug APK build failed.'
        }
    } finally {
        Pop-Location
    }
}

$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
if (!$SkipInstall) {
    Invoke-Adb -Arguments @('install', '-r', $apk) | Out-Null
}

$extension = [System.IO.Path]::GetExtension($GamePath).ToLowerInvariant()
if ($extension -notin @('.app', '.cc')) {
    throw 'Audio validation supports APP and CC games.'
}
$deviceGamePath = "/sdcard/Download/dingoopie-audio-validation$extension"
Invoke-Adb -Arguments @('push', (Resolve-Path -LiteralPath $GamePath).Path, $deviceGamePath) | Out-Null
Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null
Invoke-Adb -Arguments @(
    'shell', 'run-as', 'com.dingoopie.android', 'rm', '-f',
    'logs/dingoopie-audio-validation.wav',
    'logs/dingoopie-audio-validation.csv',
    'logs/dingoopie-native.log') | Out-Null
Invoke-Adb -Arguments @(
    'shell', 'am', 'start', '-W',
    '-n', 'com.dingoopie.android/.DingooPieActivity',
    '--es', 'dingoopie.game_automation_path', $deviceGamePath,
    '--ez', 'dingoopie.audio_validation', 'true') | Out-Null
$captureStart = Get-Date
if ($InputSequence) {
    $points = @{
        UP = @(60, 234)
        DOWN = @(60, 307)
        LEFT = @(24, 270)
        RIGHT = @(96, 270)
        A = @(936, 270)
        B = @(900, 307)
        X = @(900, 234)
        Y = @(864, 270)
        L = @(60, 25)
        R = @(900, 25)
        START = @(900, 517)
        SELECT = @(60, 517)
    }
    Start-Sleep -Seconds $InputStartDelaySeconds
    foreach ($button in ($InputSequence -split '[,;\s]+' | Where-Object { $_ })) {
        $name = $button.ToUpperInvariant()
        if (!$points.ContainsKey($name)) {
            throw "Unknown virtual button in input sequence: $button"
        }
        $point = $points[$name]
        Invoke-Adb -Arguments @(
            'shell', 'input', 'tap', $point[0].ToString(), $point[1].ToString()) | Out-Null
        Start-Sleep -Milliseconds $InputIntervalMilliseconds
    }
}
$remainingSeconds = $DurationSeconds - [int]((Get-Date) - $captureStart).TotalSeconds
if ($remainingSeconds -gt 0) {
    Start-Sleep -Seconds $remainingSeconds
}
Start-Sleep -Seconds 2

$wavePath = Join-Path $OutputDirectory 'audio-validation.wav'
$eventsPath = Join-Path $OutputDirectory 'audio-validation.csv'
$logPath = Join-Path $OutputDirectory 'native.log'
Save-RunAsFile -DeviceRelativePath 'logs/dingoopie-audio-validation.wav' -HostPath $wavePath
Save-RunAsFile -DeviceRelativePath 'logs/dingoopie-audio-validation.csv' -HostPath $eventsPath
$nativeLogOutput = & $AdbPath -s $Serial shell run-as com.dingoopie.android `
    cat 'logs/dingoopie-native.log' 2>&1
if ($LASTEXITCODE -eq 0) {
    $nativeLogOutput | Set-Content -LiteralPath $logPath -Encoding utf8
}
else {
    [System.IO.File]::WriteAllText($logPath, "", [System.Text.UTF8Encoding]::new($false))
}
Invoke-Adb -Arguments @('shell', 'am', 'force-stop', 'com.dingoopie.android') | Out-Null

$waveBytes = [System.IO.File]::ReadAllBytes($wavePath)
if ($waveBytes.Length -lt 12 -or
    [System.Text.Encoding]::ASCII.GetString($waveBytes, 0, 4) -ne 'RIFF' -or
    [System.Text.Encoding]::ASCII.GetString($waveBytes, 8, 4) -ne 'WAVE') {
    throw "Audio validation WAV was not created. See $logPath"
}
$eventHeader = Get-Content -LiteralPath $eventsPath -TotalCount 1
if ($eventHeader -notmatch '^elapsed_ms,event,bytes,queued_bytes,pending_bytes,wait_ms') {
    throw "Audio validation event log was not created. See $logPath"
}

& python (Join-Path $PSScriptRoot 'analyze_audio_validation.py') `
    --wav $wavePath --events $eventsPath --output $OutputDirectory
if ($LASTEXITCODE -ne 0) {
    throw "Audio validation failed. See $OutputDirectory"
}
Write-Host "Audio validation report: $(Join-Path $OutputDirectory 'audio-validation-report.html')"
