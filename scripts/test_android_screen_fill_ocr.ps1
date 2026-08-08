param(
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555',
    [string]$OutputDirectory
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$AndroidSdkRoot) {
    $AndroidSdkRoot = $env:ANDROID_SDK_ROOT
}
if (!$AndroidSdkRoot) {
    $AndroidSdkRoot = Join-Path $projectRoot '.tools\android\sdk'
}
if (!$AdbPath) {
    $AdbPath = Join-Path $AndroidSdkRoot 'platform-tools\adb.exe'
}
if (!(Test-Path -LiteralPath $AdbPath)) {
    throw "ADB was not found: $AdbPath"
}
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot '.tools\screen-fill-ocr'
}
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$AdbPath = (Resolve-Path -LiteralPath $AdbPath).Path
$packageName = 'com.dingoopie.android'
$screenFillLabel = -join @(
    [char]0x753b, [char]0x9762, [char]0x586b, [char]0x5145)
$keepAspectText = -join @(
    [char]0x4fdd, [char]0x6301, [char]0x5bbd, [char]0x9ad8,
    [char]0x6bd4)
$blurredExtensionText = -join @(
    [char]0x6a21, [char]0x7cca, [char]0x5ef6, [char]0x5c55)
$stretchFullscreenText = -join @(
    [char]0x62c9, [char]0x4f38, [char]0x586b, [char]0x5145)

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $AdbPath -s $Serial @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        throw "ADB failed: adb $($Arguments -join ' ')`n$output"
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

function Get-ChineseOcrText {
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
    if ($decoder.PixelWidth -ne 960 -or $decoder.PixelHeight -ne 540) {
        throw "Expected 960x540 landscape output, got $($decoder.PixelWidth)x$($decoder.PixelHeight)."
    }
    $bitmap = Await-WinRt `
        ($decoder.GetSoftwareBitmapAsync()) `
        ([Windows.Graphics.Imaging.SoftwareBitmap])
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage(
        [Windows.Globalization.Language]::new('zh-Hans-CN'))
    if (!$engine) {
        throw 'Windows Simplified Chinese OCR language is unavailable.'
    }
    $result = Await-WinRt `
        ($engine.RecognizeAsync($bitmap)) `
        ([Windows.Media.Ocr.OcrResult])
    return (($result.Lines | ForEach-Object {
        ($_.Words | ForEach-Object { $_.Text }) -join ''
    }) -join "`n")
}

function Capture-Ocr {
    param([string]$Name)
    $devicePath = "/sdcard/$Name.png"
    $hostPath = Join-Path $OutputDirectory "$Name.png"
    Invoke-Adb -Arguments @('shell', 'screencap', '-p', $devicePath) | Out-Null
    Invoke-Adb -Arguments @('pull', $devicePath, $hostPath) | Out-Null
    return [pscustomobject]@{
        Path = $hostPath
        Text = Get-ChineseOcrText -ImagePath $hostPath
    }
}

function Assert-OcrContains {
    param($Capture, [string[]]$Expected)
    foreach ($text in $Expected) {
        if (!$Capture.Text.Contains($text)) {
            throw "OCR did not find '$text' in $($Capture.Path).`n$($Capture.Text)"
        }
    }
}

Invoke-Adb -Arguments @('shell', 'am', 'force-stop', $packageName) | Out-Null
Invoke-Adb -Arguments @(
    'shell', 'monkey', '-p', $packageName,
    '-c', 'android.intent.category.LAUNCHER', '1') | Out-Null
Start-Sleep -Seconds 2
Invoke-Adb -Arguments @('shell', 'input', 'tap', '675', '40') | Out-Null
Start-Sleep -Milliseconds 350
Invoke-Adb -Arguments @('shell', 'input', 'tap', '480', '190') | Out-Null
Start-Sleep -Milliseconds 350
Invoke-Adb -Arguments @('shell', 'input', 'tap', '480', '190') | Out-Null
Start-Sleep -Milliseconds 450
Invoke-Adb -Arguments @('shell', 'input', 'swipe', '480', '450', '480', '220', '400') | Out-Null
Start-Sleep -Milliseconds 450

$current = Capture-Ocr -Name 'screen-fill-current'
Assert-OcrContains -Capture $current -Expected @($screenFillLabel)
for ($attempt = 0; $attempt -lt 3 -and !$current.Text.Contains($keepAspectText); ++$attempt) {
    Invoke-Adb -Arguments @('shell', 'input', 'tap', '480', '372') | Out-Null
    Start-Sleep -Milliseconds 350
    $current = Capture-Ocr -Name "screen-fill-normalize-$attempt"
}
Assert-OcrContains -Capture $current -Expected @($screenFillLabel, $keepAspectText)

Invoke-Adb -Arguments @('shell', 'input', 'tap', '480', '372') | Out-Null
Start-Sleep -Milliseconds 350
$blurred = Capture-Ocr -Name 'screen-fill-blurred'
Assert-OcrContains -Capture $blurred -Expected @($screenFillLabel, $blurredExtensionText)

Invoke-Adb -Arguments @('shell', 'input', 'tap', '480', '372') | Out-Null
Start-Sleep -Milliseconds 350
$stretch = Capture-Ocr -Name 'screen-fill-stretch'
Assert-OcrContains -Capture $stretch -Expected @($screenFillLabel, $stretchFullscreenText)

Invoke-Adb -Arguments @('shell', 'input', 'tap', '480', '372') | Out-Null
Start-Sleep -Milliseconds 350
$restored = Capture-Ocr -Name 'screen-fill-restored'
Assert-OcrContains -Capture $restored -Expected @($screenFillLabel, $keepAspectText)

Write-Host 'Screen fill OCR automation passed.'
Write-Host "Keep aspect: $($current.Path)"
Write-Host "Blurred extension: $($blurred.Path)"
Write-Host "Stretch fullscreen: $($stretch.Path)"
Write-Host "Restored default: $($restored.Path)"
