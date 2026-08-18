param(
    [Parameter(Mandatory = $true)]
    [string]$SampleRoot,
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555',
    [string]$OutputDirectory,
    [int]$StartupSeconds = 12,
    [switch]$SkipBuild,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sampleRootPath = (Resolve-Path -LiteralPath $SampleRoot).Path
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot '.tools\save-state-all-samples'
}
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

if (!$AndroidSdkRoot) {
    $sdkLine = Get-Content -LiteralPath (Join-Path $projectRoot 'local.properties') |
        Where-Object { $_ -match '^sdk\.dir=' } | Select-Object -First 1
    if ($sdkLine) {
        $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).
            Replace('\:', ':').Replace('\\', '\')
    }
}
if (!$AndroidSdkRoot) { $AndroidSdkRoot = $env:ANDROID_SDK_ROOT }
if (!$AndroidSdkRoot) { throw 'Android SDK is required.' }
if (!$AdbPath) { $AdbPath = Join-Path $AndroidSdkRoot 'platform-tools\adb.exe' }
if (!(Test-Path -LiteralPath $AdbPath)) { throw "ADB was not found: $AdbPath" }

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $AdbPath -s $Serial @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        $ErrorActionPreference = $previousErrorActionPreference
        throw "ADB command failed: $($Arguments -join ' ')`n$($output -join "`n")"
    }
    $ErrorActionPreference = $previousErrorActionPreference
    return $output
}

function Capture-Screenshot {
    param([string]$DevicePath, [string]$HostPath)
    Invoke-Adb shell screencap '-p' $DevicePath | Out-Null
    Invoke-Adb pull $DevicePath $HostPath | Out-Null
}

function Get-ChangedPixelRatio {
    param([string]$FirstPath, [string]$SecondPath)
    Add-Type -AssemblyName System.Drawing
    $first = [System.Drawing.Bitmap]::new($FirstPath)
    $second = [System.Drawing.Bitmap]::new($SecondPath)
    try {
        if ($first.Width -ne $second.Width -or $first.Height -ne $second.Height) {
            return 1.0
        }
        $changed = 0L
        $total = [long]$first.Width * $first.Height
        for ($y = 0; $y -lt $first.Height; $y += 2) {
            for ($x = 0; $x -lt $first.Width; $x += 2) {
                $firstColor = $first.GetPixel($x, $y)
                $secondColor = $second.GetPixel($x, $y)
                if ([Math]::Abs($firstColor.R - $secondColor.R) -gt 12 -or
                    [Math]::Abs($firstColor.G - $secondColor.G) -gt 12 -or
                    [Math]::Abs($firstColor.B - $secondColor.B) -gt 12) {
                    $changed += 4
                }
            }
        }
        return [Math]::Min(1.0, $changed / $total)
    }
    finally {
        $first.Dispose()
        $second.Dispose()
    }
}

function Wait-UiResource {
    param([string]$ResourceId, [int]$TimeoutSeconds = 60)
    $dumpPath = '/sdcard/dingoopie-save-state-window.xml'
    $hostDumpPath = Join-Path $OutputDirectory 'window.xml'
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        Invoke-Adb shell uiautomator dump $dumpPath | Out-Null
        Invoke-Adb pull $dumpPath $hostDumpPath | Out-Null
        $content = Get-Content -LiteralPath $hostDumpPath -Raw
        $pattern = 'resource-id="' + [regex]::Escape($ResourceId) +
            '"[^>]*bounds="(\[\d+,\d+\]\[\d+,\d+\])"'
        $match = [regex]::Match($content, $pattern)
        if ($match.Success) {
            return $match.Groups[1].Value
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    throw "Timed out waiting for UI resource: $ResourceId"
}

function Tap-UiResource {
    param([string]$ResourceId, [int]$TimeoutSeconds = 60)
    $bounds = Wait-UiResource -ResourceId $ResourceId `
        -TimeoutSeconds $TimeoutSeconds
    if ($bounds -notmatch '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw "Invalid bounds for UI resource '$ResourceId': $bounds"
    }
    $x = [int](($Matches[1] + $Matches[3]) / 2)
    $y = [int](($Matches[2] + $Matches[4]) / 2)
    Invoke-Adb shell input tap $x $y | Out-Null
}

function Invoke-SampleTest {
    param([System.IO.FileInfo]$Sample, [int]$Index)
    $devicePath = "/sdcard/Download/dingoopie-save-state-sample-$Index$($Sample.Extension.ToLowerInvariant())"
    $automationPath = $devicePath.Replace(' ', '_')
    $sampleLog = Join-Path $OutputDirectory ("{0:D3}-{1}.log" -f $Index, $Sample.BaseName)
    $menuScreenshot = Join-Path $OutputDirectory ("{0:D3}-save-menu.png" -f $Index)
    $resumedScreenshot = Join-Path $OutputDirectory ("{0:D3}-resumed.png" -f $Index)
    $transcript = [System.Collections.Generic.List[string]]::new()
    try {
        $transcript.Add("sample=$($Sample.FullName)")
        Invoke-Adb shell am force-stop $packageName | Out-Null
        Invoke-Adb push $Sample.FullName $devicePath | Out-Null
        Invoke-Adb logcat -c | Out-Null
        Invoke-Adb -Arguments @('shell', 'am', 'start', '-W', '-n',
            "$packageName/.DingooPieActivity", '--es',
            'dingoopie.game_automation_path', $automationPath) | Out-Null
        Start-Sleep -Seconds $StartupSeconds

        Invoke-Adb shell input keyevent 4 | Out-Null
        Start-Sleep -Milliseconds 800
        Invoke-Adb shell input tap 480 139 | Out-Null
        Start-Sleep -Milliseconds 500
        Capture-Screenshot -DevicePath '/sdcard/dingoopie-save-menu.png' `
            -HostPath $menuScreenshot

        Invoke-Adb shell input tap 300 399 | Out-Null
        Tap-UiResource -ResourceId 'android:id/button1'
        Start-Sleep -Milliseconds 750
        Tap-UiResource -ResourceId 'android:id/button1'

        Invoke-Adb shell input tap 500 399 | Out-Null
        Tap-UiResource -ResourceId 'android:id/button1'
        Start-Sleep -Milliseconds 750
        Tap-UiResource -ResourceId 'android:id/button1'
        Start-Sleep -Seconds 2
        Capture-Screenshot -DevicePath '/sdcard/dingoopie-resumed.png' `
            -HostPath $resumedScreenshot
        $changedPixelRatio = Get-ChangedPixelRatio -FirstPath $menuScreenshot `
            -SecondPath $resumedScreenshot
        if ($changedPixelRatio -lt 0.15) {
            throw ("Save-state menu remained visible after load; changed pixel ratio={0:P2}." -f
                $changedPixelRatio)
        }

        $transcript.Add(("changed_pixel_ratio={0:F4}" -f $changedPixelRatio))
        $transcript.Add('result=passed')
        $transcript | Set-Content -LiteralPath $sampleLog -Encoding utf8
        return [pscustomobject]@{ Index = $Index; Sample = $Sample.FullName; Result = 'passed' }
    }
    catch {
        $transcript.Add("result=failed")
        $transcript.Add($_.Exception.Message)
        $transcript | Set-Content -LiteralPath $sampleLog -Encoding utf8
        return [pscustomobject]@{ Index = $Index; Sample = $Sample.FullName; Result = 'failed'; Error = $_.Exception.Message }
    }
    finally {
        $null = & $AdbPath -s $Serial shell am force-stop $packageName 2>&1
        $null = & $AdbPath -s $Serial shell rm '-f' $devicePath `
            '/sdcard/dingoopie-save-state-window.xml' `
            '/sdcard/dingoopie-save-menu.png' '/sdcard/dingoopie-resumed.png' 2>&1
    }
}

$packageName = 'com.dingoopie.android'
$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
if (!$SkipBuild) {
    & (Join-Path $projectRoot 'gradlew.bat') :app:assembleDebug --no-daemon --console=plain
    if ($LASTEXITCODE -ne 0) { throw 'Android debug build failed.' }
}
if (!$SkipInstall) {
    Invoke-Adb install '-r' $apk | Out-Null
}

$settingsFile = Join-Path $OutputDirectory 'automation-settings.ini'
$settingsBackup = 'files/DingooPie.save-state-all-samples-backup.ini'
[System.IO.File]::WriteAllText($settingsFile,
    "[video]`r`nscreen_orientation=landscape`r`n[ui]`r`nlanguage=english`r`n",
    [System.Text.UTF8Encoding]::new($false))
$settingsBackupCreated = $false
$null = & $AdbPath -s $Serial shell run-as $packageName cp `
    'files/DingooPie.ini' $settingsBackup 2>&1
$settingsBackupCreated = $LASTEXITCODE -eq 0
Invoke-Adb push $settingsFile '/data/local/tmp/DingooPie.ini' | Out-Null
Invoke-Adb shell run-as $packageName cp '/data/local/tmp/DingooPie.ini' `
    'files/DingooPie.ini' | Out-Null

$samples = @(Get-ChildItem -LiteralPath $sampleRootPath -Recurse -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in @('.app', '.cc') } |
    Sort-Object FullName)
if ($samples.Count -eq 0) { throw "No .app or .cc samples found under $sampleRootPath." }

try {
    $results = for ($index = 0; $index -lt $samples.Count; $index++) {
        $number = $index + 1
        Write-Host ("[{0}/{1}] {2}" -f $number, $samples.Count, $samples[$index].FullName)
        Invoke-SampleTest -Sample $samples[$index] -Index $number
    }
    $results | Format-Table -AutoSize
    $results | ConvertTo-Csv -NoTypeInformation | Set-Content `
        -LiteralPath (Join-Path $OutputDirectory 'results.csv') -Encoding utf8
    $failed = @($results | Where-Object Result -ne 'passed')
    if ($failed.Count -ne 0) {
        throw "$($failed.Count) sample(s) failed save-state return-to-game validation."
    }
    Write-Host "All $($samples.Count) game samples passed save-state return-to-game validation."
}
finally {
    if ($settingsBackupCreated) {
        $null = & $AdbPath -s $Serial shell run-as $packageName cp `
            $settingsBackup 'files/DingooPie.ini' 2>&1
        $null = & $AdbPath -s $Serial shell run-as $packageName rm `
            '-f' $settingsBackup 2>&1
    }
    else {
        $null = & $AdbPath -s $Serial shell run-as $packageName rm `
            '-f' 'files/DingooPie.ini' 2>&1
    }
    $null = & $AdbPath -s $Serial shell rm '-f' '/data/local/tmp/DingooPie.ini' `
        '/sdcard/dingoopie-save-state-window.xml' 2>&1
}
