param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('qiye', 'tiandidao')]
    [string]$Game,
    [Parameter(Mandatory = $true)]
    [string]$GamePath,
    [string]$AdbPath = 'D:\Program Files\MuMu\emulator\nemu\vmonitor\bin\adb_server.exe',
    [string]$Serial = '127.0.0.1:7555',
    [int]$Samples = 10,
    [string]$OutputDirectory,
    [string]$ExpectedBackend,
    [switch]$EnableProfile
)

$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $GamePath)) {
    throw "CC game was not found: $GamePath"
}
if ([System.IO.Path]::GetExtension($GamePath) -ine '.cc') {
    throw 'The 3D performance test only accepts CC package files.'
}
if (!(Test-Path -LiteralPath $AdbPath)) {
    throw "ADB was not found: $AdbPath"
}
if ($Samples -lt 3) {
    throw 'At least three FPS samples are required.'
}
if (!$OutputDirectory) {
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputDirectory = Join-Path $PSScriptRoot "..\.tools\cc-3d-fps-$Game-$timestamp"
}
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path
$AdbPath = (Resolve-Path -LiteralPath $AdbPath).Path
$profileBackupPath = Join-Path $OutputDirectory 'settings-backup.ini'
$profileDevicePath = '/data/local/tmp/dingoopie-cc-3d-profile.ini'
$profileDeviceBackupPath = 'files/DingooPie.profile-backup.ini'

function Set-DebugProfile {
    if (!$EnableProfile) { return }
    Invoke-Adb shell run-as com.dingoopie.android cp `
        files/DingooPie.ini $profileDeviceBackupPath | Out-Null
    $settings = (Invoke-Adb shell run-as com.dingoopie.android cat files/DingooPie.ini) -join "`r`n"
    [System.IO.File]::WriteAllText($profileBackupPath, $settings,
        [System.Text.UTF8Encoding]::new($false))
    $profileSettings = $settings -replace '(?m)^profile=.*$', 'profile=1'
    $profileHostPath = Join-Path $OutputDirectory 'settings-profile.ini'
    [System.IO.File]::WriteAllText($profileHostPath, $profileSettings,
        [System.Text.UTF8Encoding]::new($false))
    Invoke-Adb push $profileHostPath $profileDevicePath | Out-Null
    Invoke-Adb shell run-as com.dingoopie.android cp $profileDevicePath files/DingooPie.ini | Out-Null
}

function Restore-DebugProfile {
    if (!$EnableProfile -or !(Test-Path -LiteralPath $profileBackupPath)) { return }
    try {
        Invoke-Adb shell run-as com.dingoopie.android cp `
            $profileDeviceBackupPath files/DingooPie.ini | Out-Null
        Invoke-Adb shell run-as com.dingoopie.android rm `
            $profileDeviceBackupPath | Out-Null
        Invoke-Adb shell rm $profileDevicePath | Out-Null
    } catch {
        Write-Warning "Could not restore DingooPie.ini after profile run: $($_.Exception.Message)"
    }
}

trap {
    $failure = $_
    Restore-DebugProfile
    Write-Error "3D FPS test failed: $($failure.Exception.Message)"
    exit 1
}

$preset = if ($Game -eq 'qiye') {
    @{
        LibraryText = 'CC 3D QIYE'
        InitialConfirmations = 0
        Direction = @(96, 270)
    }
} else {
    @{
        LibraryText = 'CC 3D TIANDIDAO'
        InitialConfirmations = 4
        Direction = @(60, 306)
    }
}
$controls = @{
    A = @(936, 269)
    B = @(900, 305)
    X = @(900, 233)
    Y = @(864, 269)
}

function Invoke-Adb {
    $adbArguments = @($args)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $AdbPath -s $Serial @adbArguments 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    if ($exitCode -ne 0) {
        throw "ADB command failed: $($adbArguments -join ' ')`n$($output -join "`n")"
    }
    return $output
}

function Save-NativeTelemetry {
    param([string]$Prefix = 'native')
    $logcatPath = Join-Path $OutputDirectory "$Prefix-logcat.txt"
    $nativePath = Join-Path $OutputDirectory "$Prefix-native.log"
    $profilePath = Join-Path $OutputDirectory "$Prefix-profile.log"
    $logcat = (Invoke-Adb logcat -d -v threadtime) -join "`r`n"
    [System.IO.File]::WriteAllText($logcatPath, $logcat,
        [System.Text.UTF8Encoding]::new($false))
    $native = ''
    try {
        $native = (Invoke-Adb shell run-as com.dingoopie.android cat logs/dingoopie-native.log) -join "`r`n"
    } catch {
        $native = "Native log unavailable: $($_.Exception.Message)"
    }
    [System.IO.File]::WriteAllText($nativePath, $native,
        [System.Text.UTF8Encoding]::new($false))
    $profileLines = @(($native + "`r`n" + $logcat) -split "`r?`n" | ForEach-Object {
        if ($_ -match '(cc-profile:.*|profile:irjit .*|profile:frontend .*)$') {
            $Matches[1]
        }
    })
    [System.IO.File]::WriteAllLines($profilePath, $profileLines,
        [System.Text.UTF8Encoding]::new($false))
    return [pscustomobject]@{
        logcat_path = $logcatPath
        native_log_path = $nativePath
        profile_path = $profilePath
        profile_lines = $profileLines
        native_text = $native
    }
}

function Save-Screenshot {
    param([string]$Name)
    $devicePath = "/sdcard/$Name.png"
    $hostPath = Join-Path $OutputDirectory "$Name.png"
    Invoke-Adb shell screencap -p $devicePath | Out-Null
    Invoke-Adb pull $devicePath $hostPath | Out-Null
    Invoke-Adb shell rm $devicePath | Out-Null
    return $hostPath
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
    param([string]$ImagePath, [string]$Language = 'zh-Hans-CN')
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
    $null = [Windows.Storage.FileAccessMode, Windows.Storage, ContentType = WindowsRuntime]
    $null = [Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime]
    $null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
    $null = [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
    $null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
    $null = [Windows.Media.Ocr.OcrResult, Windows.Foundation, ContentType = WindowsRuntime]
    $null = [Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime]
    $file = Await-WinRt ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ImagePath)) ([Windows.Storage.StorageFile])
    $stream = Await-WinRt ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await-WinRt ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await-WinRt ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage(
        [Windows.Globalization.Language]::new($Language))
    if (!$engine) {
        throw "Windows OCR language is unavailable: $Language"
    }
    $result = Await-WinRt ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    return $result.Text
}

function Get-WindowsOcrLines {
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
    $file = Await-WinRt ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ImagePath)) ([Windows.Storage.StorageFile])
    $stream = Await-WinRt ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await-WinRt ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await-WinRt ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage(
        [Windows.Globalization.Language]::new('zh-Hans-CN'))
    if (!$engine) {
        throw 'Windows Simplified Chinese OCR language is unavailable.'
    }
    $result = Await-WinRt ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    foreach ($line in $result.Lines) {
        $top = [double]::PositiveInfinity
        $bottom = 0.0
        foreach ($word in $line.Words) {
            $top = [Math]::Min($top, $word.BoundingRect.Y)
            $bottom = [Math]::Max($bottom,
                $word.BoundingRect.Y + $word.BoundingRect.Height)
        }
        [pscustomobject]@{
            Text = $line.Text
            Top = $top
            Bottom = $bottom
        }
    }
}

function Save-FpsOcrCrop {
    param([string]$SourcePath, [string]$Name)
    Add-Type -AssemblyName System.Drawing
    $source = [System.Drawing.Bitmap]::FromFile($SourcePath)
    try {
        $target = [System.Drawing.Bitmap]::new(1000, 300)
        try {
            $graphics = [System.Drawing.Graphics]::FromImage($target)
            try {
                $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
                $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
                $graphics.DrawImage($source,
                    [System.Drawing.Rectangle]::new(0, 0, 1000, 300),
                    [System.Drawing.Rectangle]::new(0, 0, 100, 30),
                    [System.Drawing.GraphicsUnit]::Pixel)
            } finally {
                $graphics.Dispose()
            }
            $path = Join-Path $OutputDirectory "$Name-fps-ocr.png"
            $target.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
            return $path
        } finally {
            $target.Dispose()
        }
    } finally {
        $source.Dispose()
    }
}

function Get-FpsValue {
    param([string]$ImagePath)
    Add-Type -AssemblyName System.Drawing
    $templates = @{
        ' # /## / # / # / # / # /###' = 1
        ' ### /#   #/    #/   # /  #  / #   /#####' = 2
        '### /   #/   #/  # / #  /#   /####' = 2
        '#### /    #/    #/ ### /    #/    #/#### ' = 3
        '   # /  ## / # # /#  # /#####/   # /   # ' = 4
        '  # / ## /# # /  # /####/  # /  # ' = 4
        '#####/#    /#    /#### /    #/    #/#### ' = 5
        ' ### /#    /#    /#### /#   #/#   #/ ### ' = 6
        '#####/    #/   # /  #  / #   / #   / #   ' = 7
        '####/   #/  # / #  /#   /#   /#   ' = 7
        '### /   #/   #/### /   #/   #/### ' = 8
        '### /   #/   #/####/   #/   #/### ' = 9
        ' ### /#   #/#   #/ ####/    #/    #/ ### ' = 9
        ' ### /#   #/#  ##/# # #/##  #/#   #/ ### ' = 0
    }
    $bitmap = [System.Drawing.Bitmap]::FromFile($ImagePath)
    try {
        $rows = @()
        for ($row = 0; $row -lt 7; ++$row) {
            $values = @()
            for ($column = 0; $column -lt 60; ++$column) {
                $sum = 0
                for ($dy = 0; $dy -lt 2; ++$dy) {
                    for ($dx = 0; $dx -lt 2; ++$dx) {
                        $pixel = $bitmap.GetPixel($column * 2 + $dx, 6 + $row * 2 + $dy)
                        $sum += ($pixel.R + $pixel.G + $pixel.B) / 3
                    }
                }
                $values += ($sum / 4 -gt 190)
            }
            $rows += ,$values
        }
        $groups = @()
        $current = @()
        for ($column = 28; $column -lt 45; ++$column) {
            $active = $false
            for ($row = 0; $row -lt 7; ++$row) {
                if ($rows[$row][$column]) { $active = $true; break }
            }
            if ($active) {
                $current += $column
            } elseif ($current.Count) {
                $groups += ,$current
                $current = @()
            }
        }
        if ($current.Count) { $groups += ,$current }
        $digits = @()
        foreach ($group in $groups) {
            $lines = @()
            for ($row = 0; $row -lt 7; ++$row) {
                $line = ''
                foreach ($column in $group) {
                    $line += $(if ($rows[$row][$column]) { '#' } else { ' ' })
                }
                $lines += $line
            }
            $key = $lines -join '/'
            if ($templates.ContainsKey($key)) {
                $digits += $templates[$key]
                continue
            }
            $bestDigit = $null
            $bestScore = [int]::MaxValue
            foreach ($templateKey in $templates.Keys) {
                $templateLines = $templateKey -split '/'
                $score = 0
                for ($row = 0; $row -lt 7; ++$row) {
                    $width = [Math]::Max($lines[$row].Length,
                        $templateLines[$row].Length)
                    for ($column = 0; $column -lt $width; ++$column) {
                        $left = if ($column -lt $lines[$row].Length) {
                            $lines[$row][$column]
                        } else { ' ' }
                        $right = if ($column -lt $templateLines[$row].Length) {
                            $templateLines[$row][$column]
                        } else { ' ' }
                        if ($left -ne $right) { ++$score }
                    }
                }
                if ($score -lt $bestScore) {
                    $bestScore = $score
                    $bestDigit = $templates[$templateKey]
                }
            }
            if ($null -eq $bestDigit -or $bestScore -gt 4) {
                throw "Unrecognized FPS glyph in ${ImagePath}: $key score=$bestScore"
            }
            $digits += $bestDigit
        }
        if (!$digits.Count -or $digits.Count -gt 2) {
            throw "Invalid FPS digit count in ${ImagePath}: $($digits.Count)"
        }
        return [int](($digits -join ''))
    } finally {
        $bitmap.Dispose()
    }
}

function Get-ViewportMae {
    param([string]$FirstPath, [string]$SecondPath)
    Add-Type -AssemblyName System.Drawing
    $first = [System.Drawing.Bitmap]::FromFile($FirstPath)
    $second = [System.Drawing.Bitmap]::FromFile($SecondPath)
    try {
        $sum = 0.0
        $count = 0
        for ($y = 30; $y -lt 500; $y += 4) {
            for ($x = 120; $x -lt 840; $x += 4) {
                $a = $first.GetPixel($x, $y)
                $b = $second.GetPixel($x, $y)
                $sum += [Math]::Abs($a.R - $b.R) +
                    [Math]::Abs($a.G - $b.G) +
                    [Math]::Abs($a.B - $b.B)
                $count += 3
            }
        }
        return $sum / $count
    } finally {
        $first.Dispose()
        $second.Dispose()
    }
}

function Test-BlockingSceneText {
    param([string]$Text)
    $normalized = ($Text -replace '\s+', '')
    $blockingPatterns = @(
        '\u5173\u95ed\u5bf9\u8bdd',
        '\u6253\u5f00\u89d2\u8272',
        '\u786e\u8ba4\u952e',
        '\u8bf7\u6309',
        '\u6ca1\u6709\u8bb0\u5f55',
        '\u83dc\u5355\u952e',
        '\u6559\u7a0b'
    )
    foreach ($pattern in $blockingPatterns) {
        if ($normalized -match $pattern) {
            return $true
        }
    }
    return $false
}

function Test-TiandiDaoDialogueText {
    param([string]$Text)
    $normalized = ($Text -replace '\s+', '')
    return $normalized -match
        '\u7533\u5143\u9053|\u5929\u7f6a\u5f1f\u5b50|\u4e3a\u5e08|\u4fee\u4e3a\u5c1a\u6d45'
}

function Test-TutorialMenuPrompt {
    param([string]$Text)
    $normalized = ($Text -replace '\s+', '')
    return $normalized -match
        '\u5173\u95ed\u5bf9\u8bdd.*\u6253\u5f00.*\u754c\u9762.*\u72b6\u6001|\u6253\u5f00\u89d2\u8272|\u83dc\u5355\u952e'
}

function Hold-Control {
    param([int]$X, [int]$Y, [int]$Duration = 1200)
    Invoke-Adb shell input swipe $X $Y $X $Y $Duration | Out-Null
}

function Press-Control {
    param([int]$X, [int]$Y)
    Hold-Control $X $Y 180
}

function Press-NamedControl {
    param([string]$Name)
    $control = $controls[$Name]
    if (!$control) {
        throw "Unknown virtual control: $Name"
    }
    Press-Control $control[0] $control[1]
}

Set-DebugProfile
Invoke-Adb logcat -c | Out-Null
Invoke-Adb shell am force-stop com.dingoopie.android | Out-Null
Invoke-Adb shell am start -W -n com.dingoopie.android/.DingooPieActivity | Out-Null
Start-Sleep -Seconds 3
$listPath = Save-Screenshot 'library'
$listOcr = Get-WindowsOcrText $listPath
$launchMode = 'library-ocr'
if ($listOcr -like "*$($preset.LibraryText)*") {
    $libraryLine = Get-WindowsOcrLines $listPath |
        Where-Object { $_.Text -like "*$($preset.LibraryText)*" } |
        Select-Object -First 1
    if (!$libraryLine -or [double]::IsInfinity($libraryLine.Top)) {
        throw "OCR found the title text but not its clickable row: $($preset.LibraryText)"
    }
    $libraryY = [int](($libraryLine.Top + $libraryLine.Bottom) / 2.0)
    Invoke-Adb shell input tap 400 $libraryY | Out-Null
} else {
    $launchMode = 'debug-direct'
    $deviceGamePath = "/sdcard/Download/dingoopie-cc-3d-$Game.cc"
    Invoke-Adb push $GamePath $deviceGamePath | Out-Null
    Invoke-Adb shell am force-stop com.dingoopie.android | Out-Null
    Invoke-Adb shell am start -W -n com.dingoopie.android/.DingooPieActivity `
        --es dingoopie.game_automation_path $deviceGamePath | Out-Null
}
Start-Sleep -Seconds 20

if ($Game -eq 'qiye') {
    Press-NamedControl 'A'
    Start-Sleep -Seconds 10
    Press-Control 60 234
    Start-Sleep -Milliseconds 800
    Press-NamedControl 'A'
    Start-Sleep -Seconds 10
} else {
    for ($index = 0; $index -lt $preset.InitialConfirmations; ++$index) {
        Press-NamedControl 'A'
        Start-Sleep -Seconds 10
    }
}

$interactive = $false
$tiandidaoTutorialRecoveryAttempted = $false
$tiandidaoPostTutorialAdvanceCount = 0
for ($attempt = 1; $attempt -le 24 -and !$interactive; ++$attempt) {
    if ($Game -eq 'qiye') {
        Press-NamedControl 'A'
        Start-Sleep -Seconds 4
    } else {
        Start-Sleep -Seconds 2
    }
    $precheck = Save-Screenshot ('scene-{0}-precheck' -f $attempt)
    $precheckOcr = Get-WindowsOcrText $precheck
    if ($Game -eq 'tiandidao' -and
        (Test-TutorialMenuPrompt $precheckOcr)) {
        Press-NamedControl 'A'
        Start-Sleep -Seconds 2
        Press-NamedControl 'Y'
        Start-Sleep -Seconds 2
        Press-NamedControl 'B'
        Start-Sleep -Seconds 2
        Press-NamedControl 'A'
        Start-Sleep -Seconds 2
        continue
    }
    if ($Game -eq 'tiandidao' -and (Test-TiandiDaoDialogueText $precheckOcr)) {
        Press-NamedControl 'A'
        Start-Sleep -Seconds 2
        continue
    }
    $before = Save-Screenshot "scene-$attempt-before"
    $beforeOcr = Get-WindowsOcrText $before
    if ($Game -eq 'tiandidao' -and (Test-TiandiDaoDialogueText $beforeOcr)) {
        Press-NamedControl 'A'
        Start-Sleep -Seconds 2
        continue
    }
    if (Test-BlockingSceneText $beforeOcr) {
        continue
    }
    Hold-Control $preset.Direction[0] $preset.Direction[1]
    Start-Sleep -Seconds 1
    $after = Save-Screenshot "scene-$attempt-after"
    $afterOcr = Get-WindowsOcrText $after
    if ($Game -eq 'tiandidao' -and (Test-TiandiDaoDialogueText $afterOcr)) {
        Press-NamedControl 'A'
        Start-Sleep -Seconds 2
        continue
    }
    if (Test-BlockingSceneText $afterOcr) {
        continue
    }
    $mae = Get-ViewportMae $before $after
    if ($Game -eq 'tiandidao' -and $mae -lt 2.0) {
        if (!$tiandidaoTutorialRecoveryAttempted) {
            Press-NamedControl 'Y'
            Start-Sleep -Seconds 2
            Press-NamedControl 'B'
            Start-Sleep -Seconds 2
            Press-NamedControl 'A'
            Start-Sleep -Seconds 3
            $tiandidaoTutorialRecoveryAttempted = $true
            continue
        }
        if ($tiandidaoPostTutorialAdvanceCount -lt 4) {
            Press-NamedControl 'A'
            Start-Sleep -Seconds 8
            ++$tiandidaoPostTutorialAdvanceCount
            continue
        }
    }
    $secondDirection = if ($preset.Direction[0] -lt 60) { @(96, 270) } else { @(24, 270) }
    Hold-Control $secondDirection[0] $secondDirection[1]
    Start-Sleep -Seconds 1
    $afterSecond = Save-Screenshot ('scene-{0}-after-second' -f $attempt)
    $afterSecondOcr = Get-WindowsOcrText $afterSecond
    if (Test-BlockingSceneText $afterSecondOcr) {
        continue
    }
    $secondMae = Get-ViewportMae $after $afterSecond
    $fpsCrop = Save-FpsOcrCrop $after "scene-$attempt"
    $fpsOcr = Get-WindowsOcrText $fpsCrop 'en-US'
    if ($mae -ge 2.0 -and $secondMae -ge 2.0 -and $fpsOcr -match 'FPS') {
        $interactive = $true
        "scene_attempt=$attempt`nscene_mae=$mae`nscene_ocr=$fpsOcr" |
            Set-Content -Encoding utf8 (Join-Path $OutputDirectory 'scene-confirmation.txt')
    }
}
if (!$interactive) {
    throw 'OCR and directional-frame comparison did not confirm an interactive 3D scene.'
}

$directions = @(@(60, 234), @(96, 270), @(60, 306), @(24, 270))
$values = @()
for ($index = 1; $index -le $Samples; ++$index) {
    $direction = $directions[($index - 1) % $directions.Count]
    Hold-Control $direction[0] $direction[1] 4000
    Start-Sleep -Seconds 2
    $path = Save-Screenshot "fps-$index"
    $values += Get-FpsValue $path
}
$sorted = $values | Sort-Object
$middle = [int][Math]::Floor($sorted.Count / 2.0)
$median = if (($sorted.Count % 2) -eq 0) {
    ($sorted[$middle - 1] + $sorted[$middle]) / 2.0
} else {
    $sorted[$middle]
}
$rows = for ($index = 0; $index -lt $values.Count; ++$index) {
    [pscustomobject]@{ sample = $index + 1; fps = $values[$index] }
}
$rows | Export-Csv -NoTypeInformation -Encoding utf8 (Join-Path $OutputDirectory 'fps.csv')
$telemetry = Save-NativeTelemetry
$backendMatches = [regex]::Matches($telemetry.native_text,
    'effective_backend=([^\s]+).*?profile=(\d+)')
$executionBackend = if ($backendMatches.Count) {
    $backendMatches[$backendMatches.Count - 1].Groups[1].Value
} else { '' }
$profileActive = if ($backendMatches.Count) {
    [int]$backendMatches[$backendMatches.Count - 1].Groups[2].Value
} else { -1 }
if ($ExpectedBackend -and $executionBackend -ne $ExpectedBackend) {
    throw "Expected execution backend '$ExpectedBackend', found '$executionBackend'."
}
$profileSummary = [pscustomobject]@{
    cc_profile_lines = @($telemetry.profile_lines | Where-Object { $_ -like 'cc-profile:*' }).Count
    irjit_profile_lines = @($telemetry.profile_lines | Where-Object { $_ -like 'profile:irjit *' }).Count
    frontend_profile_lines = @($telemetry.profile_lines | Where-Object { $_ -like 'profile:frontend *' }).Count
    last_cc_profile = @($telemetry.profile_lines | Where-Object { $_ -like 'cc-profile:*' } | Select-Object -Last 1)
    last_irjit_profile = @($telemetry.profile_lines | Where-Object { $_ -like 'profile:irjit *' } | Select-Object -Last 1)
    last_frontend_profile = @($telemetry.profile_lines | Where-Object { $_ -like 'profile:frontend *' } | Select-Object -Last 1)
}
$summary = [pscustomobject]@{
    game = $Game
    cc_path = (Resolve-Path -LiteralPath $GamePath).Path
    samples = $values -join ','
    minimum = ($sorted | Select-Object -First 1)
    median = $median
    maximum = ($sorted | Select-Object -Last 1)
    ocr_game = $preset.LibraryText
    launch_mode = $launchMode
    interactive_3d_confirmed = $true
    execution_backend = $executionBackend
    profile_active = $profileActive
    profile_requested = [bool]$EnableProfile
    telemetry = [pscustomobject]@{
        logcat_path = $telemetry.logcat_path
        native_log_path = $telemetry.native_log_path
        profile_path = $telemetry.profile_path
        profile = $profileSummary
    }
}
$summary | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $OutputDirectory 'summary.json')
$summary | Format-List
Restore-DebugProfile
