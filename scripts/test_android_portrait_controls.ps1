param(
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555',
    [Parameter(Mandatory = $true)][string]$GamePath,
    [string]$OutputDirectory,
    [switch]$SkipBuild,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (!$AndroidSdkRoot) {
    $AndroidSdkRoot = $env:ANDROID_SDK_ROOT
}
if (!$AndroidSdkRoot) {
    throw 'Android SDK is required. Pass -AndroidSdkRoot or set ANDROID_SDK_ROOT.'
}
if (!(Test-Path -LiteralPath $GamePath)) {
    throw "Game was not found: $GamePath"
}
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot '.tools\portrait-controls'
}
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$resolvedOutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

$adb = $AdbPath
if (!$adb -and $Serial -eq '127.0.0.1:7555') {
    $adb = 'D:\Program Files\MuMu\emulator\nemu\vmonitor\bin\adb_server.exe'
}
if (!$adb) {
    $adb = Join-Path $AndroidSdkRoot 'platform-tools\adb.exe'
}
if (!(Test-Path -LiteralPath $adb)) {
    throw "ADB was not found: $adb"
}
$adb = (Resolve-Path -LiteralPath $adb).Path

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

function Get-EnglishOcrWords {
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
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage(
        [Windows.Globalization.Language]::new('en-US'))
    if (!$engine) {
        throw 'Windows English OCR language is unavailable.'
    }
    $result = Await-WinRt `
        ($engine.RecognizeAsync($bitmap)) `
        ([Windows.Media.Ocr.OcrResult])
    $words = foreach ($line in $result.Lines) {
        foreach ($word in $line.Words) {
            [pscustomobject]@{
                Text = $word.Text.ToUpperInvariant()
                X = [double]$word.BoundingRect.X
                Y = [double]$word.BoundingRect.Y
                Width = [double]$word.BoundingRect.Width
                Height = [double]$word.BoundingRect.Height
            }
        }
    }
    return [pscustomobject]@{
        Width = [int]$decoder.PixelWidth
        Height = [int]$decoder.PixelHeight
        Words = @($words)
    }
}

$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
if (!$SkipBuild) {
    $env:ANDROID_HOME = (Resolve-Path -LiteralPath $AndroidSdkRoot).Path
    $env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
    & (Join-Path $projectRoot 'gradlew.bat') --no-daemon assembleDebug
    if ($LASTEXITCODE -ne 0) {
        throw 'Android debug build failed.'
    }
}
if (!$SkipInstall) {
    Invoke-Adb -Arguments @('install', '-r', $apk) | Out-Null
}

$packageName = 'com.dingoopie.android'
$settingsFile = Join-Path $resolvedOutputDirectory 'portrait-settings.ini'
[System.IO.File]::WriteAllText(
    $settingsFile,
    "[video]`r`nscreen_orientation=portrait`r`n",
    [System.Text.UTF8Encoding]::new($false))
$extension = [System.IO.Path]::GetExtension($GamePath)
$deviceGamePath = "/sdcard/Download/dingoopie-portrait-controls$extension"
$screenshot = Join-Path $resolvedOutputDirectory 'portrait-controls.png'
$settingsBackup = 'files/DingooPie.portrait-controls-backup.ini'
$null = & $adb -s $Serial shell run-as $packageName cp `
    'files/DingooPie.ini' $settingsBackup 2>&1
$settingsBackupCreated = $LASTEXITCODE -eq 0

try {
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', $packageName) | Out-Null
    Invoke-Adb -Arguments @('push', $settingsFile, '/data/local/tmp/DingooPie.ini') | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'run-as', $packageName, 'mkdir', '-p', 'files') | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'run-as', $packageName, 'cp',
        '/data/local/tmp/DingooPie.ini', 'files/DingooPie.ini') | Out-Null
    Invoke-Adb -Arguments @(
        'push', (Resolve-Path -LiteralPath $GamePath).Path, $deviceGamePath) | Out-Null
    Invoke-Adb -Arguments @(
        'shell', 'am', 'start', '-W', '-n', "$packageName/.DingooPieActivity",
        '--es', 'dingoopie.game_automation_path', $deviceGamePath) | Out-Null
    Start-Sleep -Seconds 12
    Invoke-Adb -Arguments @(
        'shell', 'screencap', '-p', '/sdcard/portrait-controls.png') | Out-Null
    Invoke-Adb -Arguments @(
        'pull', '/sdcard/portrait-controls.png', $screenshot) | Out-Null

    $ocr = Get-EnglishOcrWords -ImagePath $screenshot
    if ($ocr.Width -ge $ocr.Height) {
        throw "Expected a portrait screenshot, got $($ocr.Width)x$($ocr.Height)."
    }
    $labels = @{}
    foreach ($name in @('SELECT', 'MENU', 'START')) {
        $matches = @($ocr.Words | Where-Object { $_.Text -eq $name })
        if ($matches.Count -eq 0) {
            throw "Portrait OCR did not recognize $name. Screenshot: $screenshot"
        }
        if ($name -eq 'MENU' -and $matches.Count -ne 1) {
            throw "Portrait OCR found $($matches.Count) MENU labels; expected one."
        }
        $labels[$name] = $matches[0]
    }
    $select = $labels.SELECT
    $menu = $labels.MENU
    $start = $labels.START
    $centerY = @($select, $menu, $start) |
        ForEach-Object { $_.Y + $_.Height / 2 }
    if (($centerY | Measure-Object -Maximum).Maximum -
        ($centerY | Measure-Object -Minimum).Minimum -gt 6) {
        throw 'SELECT, MENU, and START are not vertically centered on one row.'
    }
    if (!($select.X -lt $menu.X -and $menu.X -lt $start.X)) {
        throw 'Portrait utility button order is not SELECT, MENU, START.'
    }
    $menuCenter = $menu.X + $menu.Width / 2
    if ([Math]::Abs($menuCenter - $ocr.Width / 2) -gt 12) {
        throw 'MENU is not centered horizontally in portrait mode.'
    }
    $labelBottomMargin = $ocr.Height -
        (@($select, $menu, $start) | ForEach-Object { $_.Y + $_.Height } |
            Measure-Object -Maximum).Maximum
    if ($labelBottomMargin -lt 16 -or $labelBottomMargin -gt 48) {
        throw "Portrait utility row has an unexpected bottom margin: $labelBottomMargin."
    }

    Write-Host 'Portrait virtual control visual automation passed.'
    Write-Host "Screenshot: $screenshot"
    Write-Host ("Labels: SELECT={0:N0},{1:N0} MENU={2:N0},{3:N0} START={4:N0},{5:N0}" -f `
        $select.X, $select.Y, $menu.X, $menu.Y, $start.X, $start.Y)
}
finally {
    $null = & $adb -s $Serial shell am force-stop $packageName 2>&1
    if ($settingsBackupCreated) {
        $null = & $adb -s $Serial shell run-as $packageName cp `
            $settingsBackup 'files/DingooPie.ini' 2>&1
        $null = & $adb -s $Serial shell run-as $packageName rm `
            '-f' $settingsBackup 2>&1
    }
    else {
        $null = & $adb -s $Serial shell run-as $packageName rm `
            '-f' 'files/DingooPie.ini' 2>&1
    }
    $null = & $adb -s $Serial shell rm '-f' `
        '/data/local/tmp/DingooPie.ini' 2>&1
}
