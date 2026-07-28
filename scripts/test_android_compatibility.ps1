param(
    [string[]]$AvdNames = @('dingoo-api35'),
    [string]$AndroidSdkRoot,
    [int]$BootTimeoutSeconds = 300,
    [ValidateRange(1, 4)][int]$CpuCores = 2,
    [ValidateRange(512, 4096)][int]$MemoryMb = 2048,
    [int]$CooldownSeconds = 15,
    [switch]$SkipBuild,
    [switch]$WipeData,
    [switch]$RunMatrix,
    [switch]$IgnoreHostHardwareErrors
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$AndroidSdkRoot) {
    $AndroidSdkRoot = if ($env:ANDROID_SDK_ROOT) {
        $env:ANDROID_SDK_ROOT
    } elseif ($env:ANDROID_HOME) {
        $env:ANDROID_HOME
    } else {
        Join-Path $projectRoot '.tools\android\sdk'
    }
}

$adb = Join-Path $AndroidSdkRoot 'platform-tools\adb.exe'
$emulator = Join-Path $AndroidSdkRoot 'emulator\emulator.exe'
$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
$serial = 'emulator-5554'
$packageName = 'com.dingoopie.android'
$activityName = 'com.dingoopie.android.DingooPieActivity'
$results = [System.Collections.Generic.List[object]]::new()

if ($AvdNames.Count -gt 1 -and !$RunMatrix) {
    throw 'Running multiple AVDs requires -RunMatrix. Test one AVD at a time by default.'
}
if ($CooldownSeconds -lt 0) {
    throw 'CooldownSeconds cannot be negative.'
}
if (!$IgnoreHostHardwareErrors) {
    $recentHardwareErrors = @(Get-WinEvent -FilterHashtable @{
            LogName = 'System'
            ProviderName = 'Microsoft-Windows-WHEA-Logger'
            Level = 1, 2
            StartTime = (Get-Date).AddDays(-1)
        } -ErrorAction SilentlyContinue)
    if ($recentHardwareErrors.Count -gt 0) {
        throw ('Recent WHEA hardware errors were found. Emulator startup was blocked to protect ' +
                'the host. Diagnose the hardware or use -IgnoreHostHardwareErrors explicitly.')
    }
}

foreach ($requiredPath in @($adb, $emulator)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required Android SDK tool was not found: $requiredPath"
    }
}

if (!$SkipBuild) {
    & (Join-Path $PSScriptRoot 'build_android.ps1') -Configuration Debug
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
if (!(Test-Path -LiteralPath $apk)) {
    throw "Debug APK was not found: $apk"
}

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    & $adb -s $serial @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "adb command failed: adb -s $serial $($Arguments -join ' ')"
    }
}

function Invoke-AdbProbe {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $adb -s $serial @Arguments 2>$null
        return [pscustomobject]@{
            ExitCode = $LASTEXITCODE
            Output = $output
        }
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
}

function Wait-ForBoot {
    $deadline = [DateTime]::UtcNow.AddSeconds($BootTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $stateProbe = Invoke-AdbProbe -Arguments @('get-state')
        if ($stateProbe.ExitCode -eq 0 -and $stateProbe.Output -eq 'device') {
            $bootProbe = Invoke-AdbProbe -Arguments @('shell', 'getprop', 'sys.boot_completed')
            if ($bootProbe.ExitCode -eq 0 -and
                    (($bootProbe.Output -join '').Trim()) -eq '1') {
                return
            }
        }
        Start-Sleep -Seconds 2
    }
    throw "Timed out waiting for $serial to boot"
}

foreach ($avdName in $AvdNames) {
    $emulatorProcess = $null
    $startedAt = Get-Date
    try {
        Invoke-AdbProbe -Arguments @('emu', 'kill') | Out-Null
        Start-Sleep -Seconds 2
        $arguments = @(
            '-avd', $avdName,
            '-port', '5554',
            '-cores', $CpuCores,
            '-memory', $MemoryMb,
            '-no-window',
            '-no-audio',
            '-no-boot-anim',
            '-no-snapshot',
            '-gpu', 'swiftshader_indirect',
            '-no-metrics'
        )
        if ($WipeData) {
            $arguments += '-wipe-data'
        }
        $emulatorProcess = Start-Process -FilePath $emulator -ArgumentList $arguments `
            -PassThru -WindowStyle Hidden
        Wait-ForBoot

        $apiLevelOutput = Invoke-Adb -Arguments @(
            'shell', 'getprop', 'ro.build.version.sdk')
        $apiLevel = ($apiLevelOutput | Where-Object { $_ -match '^\d+$' } |
                Select-Object -Last 1).Trim()
        if (!$apiLevel) {
            throw 'Unable to determine the emulator API level'
        }
        Invoke-Adb -Arguments @('install', '-r', '-t', $apk) | Out-Null
        Invoke-AdbProbe -Arguments @('logcat', '-c') | Out-Null
        Invoke-Adb -Arguments @('shell', 'am', 'force-stop', $packageName)
        $launchOutput = Invoke-Adb -Arguments @(
            'shell', 'am', 'start', '-W', '-n', "$packageName/$activityName")
        Start-Sleep -Seconds 5

        $processId = (Invoke-Adb -Arguments @('shell', 'pidof', $packageName)).Trim()
        if (!$processId) {
            throw 'Application process is not running after launch'
        }

        $activityState = Invoke-Adb -Arguments @('shell', 'dumpsys', 'activity', 'activities')
        $activityDump = $activityState -join "`n"
        $folderPickerVisible = $activityDump -match 'DocumentsActivity|files\.FilesActivity|picker\.PickActivity'
        $activityVisible = $activityDump -match 'DingooPieActivity'
        if (!$folderPickerVisible -and !$activityVisible) {
            throw 'Neither the folder picker nor DingooPie activity is visible'
        }

        $logOutput = (Invoke-Adb -Arguments @('logcat', '-d', '-v', 'brief', '--pid', $processId)) -join "`n"
        $fatalPattern = 'FATAL EXCEPTION|Fatal signal|SIGSEGV|Abort message|hid_init threw an exception'
        if ($logOutput -match $fatalPattern) {
            throw "Fatal runtime entry found in logcat: $($Matches[0])"
        }

        Invoke-Adb -Arguments @('shell', 'input', 'keyevent', '4')
        Start-Sleep -Seconds 2
        if ([int]$apiLevel -ge 31) {
            Invoke-Adb -Arguments @(
                'shell', 'pm', 'grant', $packageName,
                'android.permission.BLUETOOTH_CONNECT')
            Invoke-Adb -Arguments @('shell', 'am', 'force-stop', $packageName)
            Invoke-AdbProbe -Arguments @('logcat', '-c') | Out-Null
            Invoke-Adb -Arguments @(
                'shell', 'am', 'start', '-W', '-n', "$packageName/$activityName") | Out-Null
            Start-Sleep -Seconds 3
        }

        $processId = (Invoke-Adb -Arguments @('shell', 'pidof', $packageName)).Trim()
        if (!$processId) {
            throw 'Application process stopped after returning from the folder picker'
        }
        $logOutput = (Invoke-Adb -Arguments @('logcat', '-d', '-v', 'brief')) -join "`n"
        if ($logOutput -match $fatalPattern) {
            throw "Fatal runtime entry found after returning: $($Matches[0])"
        }

        $results.Add([pscustomobject]@{
            AVD = $avdName
            API = $apiLevel
            Result = 'PASS'
            FolderPicker = $folderPickerVisible
            Seconds = [Math]::Round(((Get-Date) - $startedAt).TotalSeconds, 1)
            Details = ($launchOutput | Select-Object -Last 1)
        })
    } catch {
        $results.Add([pscustomobject]@{
            AVD = $avdName
            API = ''
            Result = 'FAIL'
            FolderPicker = $false
            Seconds = [Math]::Round(((Get-Date) - $startedAt).TotalSeconds, 1)
            Details = "$($_.Exception.Message) | $($_.ScriptStackTrace)"
        })
    } finally {
        Invoke-AdbProbe -Arguments @('emu', 'kill') | Out-Null
        if ($emulatorProcess -and !$emulatorProcess.HasExited) {
            $emulatorProcess.WaitForExit(15000) | Out-Null
            if (!$emulatorProcess.HasExited) {
                Stop-Process -Id $emulatorProcess.Id -Force
            }
        }
        Start-Sleep -Seconds $CooldownSeconds
    }
}

$results | Format-List
if ($results.Result -contains 'FAIL') {
    exit 1
}
