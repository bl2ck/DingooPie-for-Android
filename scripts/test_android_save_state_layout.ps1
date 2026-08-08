param(
    [string]$AndroidSdkRoot,
    [string]$AdbPath,
    [string]$Serial = '127.0.0.1:7555',
    [string]$DeviceGamePath = '/sdcard/Download/dingoopie-menu-pop.app',
    [string]$OutputDirectory,
    [switch]$SkipBuild,
    [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (!$AndroidSdkRoot) {
    $sdkLine = Get-Content -LiteralPath (Join-Path $projectRoot 'local.properties') |
        Where-Object { $_ -match '^sdk\.dir=' } |
        Select-Object -First 1
    if ($sdkLine) {
        $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).
            Replace('\:', ':').Replace('\\', '\')
    }
}
if (!$AndroidSdkRoot) { throw 'Android SDK is required.' }
if (!$AdbPath) { $AdbPath = Join-Path $AndroidSdkRoot 'platform-tools\adb.exe' }
if (!(Test-Path -LiteralPath $AdbPath)) { throw "ADB was not found: $AdbPath" }
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $projectRoot '.tools\save-state-layout'
}
$null = New-Item -ItemType Directory -Force -Path $OutputDirectory
$OutputDirectory = (Resolve-Path -LiteralPath $OutputDirectory).Path

function Invoke-Adb {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Arguments)
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $AdbPath -s $Serial @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousErrorActionPreference
    if ($exitCode -ne 0) {
        throw "ADB command failed: $($Arguments -join ' ')`n$($output -join "`n")"
    }
    return $output
}

function Capture-Screenshot {
    param([string]$DeviceName, [string]$HostPath)
    Invoke-Adb -Arguments @('shell', 'screencap', '-p', $DeviceName) | Out-Null
    Invoke-Adb -Arguments @('pull', $DeviceName, $HostPath) | Out-Null
}

function Get-ColorRunBounds {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$X,
        [int]$Y,
        [int]$BorderY = -1
    )
    $sample = $Bitmap.GetPixel($X, $Y)
    $border = if ($BorderY -ge 0) { $Bitmap.GetPixel($X, $BorderY) } else { $null }
    $matches = {
        param([int]$PixelY)
        $color = $Bitmap.GetPixel($X, $PixelY)
        $fillMatch = $color.R -eq $sample.R -and $color.G -eq $sample.G -and
            $color.B -eq $sample.B
        $borderMatch = $border -and $color.R -eq $border.R -and
            $color.G -eq $border.G -and $color.B -eq $border.B
        return $fillMatch -or $borderMatch
    }
    $top = $Y
    while ($top -gt 0 -and (& $matches ($top - 1))) { $top-- }
    $bottom = $Y
    while ($bottom + 1 -lt $Bitmap.Height -and (& $matches ($bottom + 1))) { $bottom++ }
    return [pscustomobject]@{
        Top = $top
        Bottom = $bottom
        Height = $bottom - $top + 1
    }
}

function Get-ColorRunHeight {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$X,
        [int]$Y,
        [int]$BorderY = -1
    )
    return (Get-ColorRunBounds -Bitmap $Bitmap -X $X -Y $Y `
        -BorderY $BorderY).Height
}

function Get-ColorRunWidth {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$X,
        [int]$Y
    )
    $sample = $Bitmap.GetPixel($X, $Y)
    $matches = {
        param([int]$PixelX)
        $color = $Bitmap.GetPixel($PixelX, $Y)
        return $color.R -eq $sample.R -and $color.G -eq $sample.G -and
            $color.B -eq $sample.B
    }
    $left = $X
    while ($left -gt 0 -and (& $matches ($left - 1))) { $left-- }
    $right = $X
    while ($right + 1 -lt $Bitmap.Width -and (& $matches ($right + 1))) { $right++ }
    return $right - $left + 1
}

function Get-ColorRunHorizontalBounds {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$X,
        [int]$Y
    )
    $sample = $Bitmap.GetPixel($X, $Y)
    $matches = {
        param([int]$PixelX)
        $color = $Bitmap.GetPixel($PixelX, $Y)
        return $color.R -eq $sample.R -and $color.G -eq $sample.G -and
            $color.B -eq $sample.B
    }
    $left = $X
    while ($left -gt 0 -and (& $matches ($left - 1))) { $left-- }
    $right = $X
    while ($right + 1 -lt $Bitmap.Width -and (& $matches ($right + 1))) { $right++ }
    return [pscustomobject]@{
        Left = $left
        Right = $right
        Width = $right - $left + 1
    }
}

function Assert-SameColor {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$FirstX,
        [int]$FirstY,
        [int]$SecondX,
        [int]$SecondY,
        [string]$Description
    )
    $first = $Bitmap.GetPixel($FirstX, $FirstY)
    $second = $Bitmap.GetPixel($SecondX, $SecondY)
    if ($first.R -ne $second.R -or $first.G -ne $second.G -or
        $first.B -ne $second.B) {
        throw "$Description has a visible border."
    }
}

function Get-BrightPixelCount {
    param(
        [System.Drawing.Bitmap]$Bitmap,
        [int]$Left,
        [int]$Top,
        [int]$Width,
        [int]$Height
    )
    $count = 0
    for ($y = $Top; $y -lt $Top + $Height; $y++) {
        for ($x = $Left; $x -lt $Left + $Width; $x++) {
            $color = $Bitmap.GetPixel($x, $y)
            if ($color.R -ge 150 -and $color.G -ge 150 -and $color.B -ge 150) {
                $count++
            }
        }
    }
    return $count
}

$apk = Join-Path $projectRoot 'app\build\outputs\apk\debug\DingooPie.apk'
if (!$SkipBuild) {
    & (Join-Path $projectRoot 'gradlew.bat') :app:assembleDebug --no-daemon --console=plain
    if ($LASTEXITCODE -ne 0) { throw 'Android debug build failed.' }
}
if (!$SkipInstall) {
    Invoke-Adb -Arguments @('install', '-r', $apk) | Out-Null
}

$packageName = 'com.dingoopie.android'
$pauseScreenshot = Join-Path $OutputDirectory 'pause-menu.png'
$saveScreenshot = Join-Path $OutputDirectory 'save-state-menu.png'
$settingsFile = Join-Path $OutputDirectory 'visual-settings.ini'
$settingsBackup = 'files/DingooPie.save-state-layout-backup.ini'
[System.IO.File]::WriteAllText($settingsFile,
    "[video]`r`nscreen_orientation=landscape`r`n[ui]`r`nlanguage=english`r`n",
    [System.Text.UTF8Encoding]::new($false))

$null = & $AdbPath -s $Serial shell run-as $packageName cp `
    'files/DingooPie.ini' $settingsBackup 2>&1
$settingsBackupCreated = $LASTEXITCODE -eq 0

try {
    Invoke-Adb -Arguments @('shell', 'ls', $DeviceGamePath) | Out-Null
    Invoke-Adb -Arguments @('shell', 'am', 'force-stop', $packageName) | Out-Null
    Invoke-Adb -Arguments @('push', $settingsFile, '/data/local/tmp/DingooPie.ini') | Out-Null
    Invoke-Adb -Arguments @('shell', 'run-as', $packageName,
        'cp', '/data/local/tmp/DingooPie.ini', 'files/DingooPie.ini') | Out-Null
    Invoke-Adb -Arguments @('shell', 'am', 'start', '-W', '-n',
        "$packageName/.DingooPieActivity", '--es',
        'dingoopie.game_automation_path', $DeviceGamePath) | Out-Null
    Start-Sleep -Seconds 10

    Invoke-Adb -Arguments @('shell', 'input', 'keyevent', '4') | Out-Null
    Start-Sleep -Milliseconds 800
    Capture-Screenshot -DeviceName '/sdcard/dingoopie-pause-menu.png' `
        -HostPath $pauseScreenshot

    Add-Type -AssemblyName System.Drawing
    $pauseBitmap = [System.Drawing.Bitmap]::new($pauseScreenshot)
    try {
        if ($pauseBitmap.Width -ne 960 -or $pauseBitmap.Height -ne 540) {
            throw "Expected 960x540 landscape output, got $($pauseBitmap.Width)x$($pauseBitmap.Height)."
        }
        $panelX = 100
        $panelY = 50
        $standardRowHeight = Get-ColorRunHeight -Bitmap $pauseBitmap `
            -X ($panelX + 34) -Y ($panelY + 66 + 23)
    }
    finally {
        $pauseBitmap.Dispose()
    }

    Invoke-Adb -Arguments @('shell', 'input', 'tap', '480', '139') | Out-Null
    Start-Sleep -Milliseconds 800
    Capture-Screenshot -DeviceName '/sdcard/dingoopie-save-state-menu.png' `
        -HostPath $saveScreenshot

    $bitmap = [System.Drawing.Bitmap]::new($saveScreenshot)
    try {
        if ($bitmap.Width -ne 960 -or $bitmap.Height -ne 540) {
            throw "Expected 960x540 landscape output, got $($bitmap.Width)x$($bitmap.Height)."
        }
        $gridTop = 116
        $slotBounds = for ($row = 0; $row -lt 5; $row++) {
            $top = $gridTop + $row * 52
            Get-ColorRunBounds -Bitmap $bitmap -X 134 -Y ($top + 23)
        }
        $slotColumns = @(
            Get-ColorRunHorizontalBounds -Bitmap $bitmap -X 190 -Y ($gridTop + 4)
            Get-ColorRunHorizontalBounds -Bitmap $bitmap -X 335 -Y ($gridTop + 4)
            Get-ColorRunHorizontalBounds -Bitmap $bitmap -X 479 -Y ($gridTop + 4)
        )
        $saveBounds = Get-ColorRunBounds -Bitmap $bitmap -X 134 -Y 399
        $loadBounds = Get-ColorRunBounds -Bitmap $bitmap -X 500 -Y 399
        $deleteBounds = Get-ColorRunBounds -Bitmap $bitmap -X 190 -Y 451
        $backBounds = Get-ColorRunBounds -Bitmap $bitmap -X 650 -Y 451
        $slotHeight = $slotBounds[0].Height
        $saveHeight = $saveBounds.Height
        $loadHeight = $loadBounds.Height
        $deleteHeight = $deleteBounds.Height
        $backHeight = $backBounds.Height
        $previewWidth = Get-ColorRunWidth -Bitmap $bitmap `
            -X 567 -Y 116
        $previewHeight = Get-ColorRunHeight -Bitmap $bitmap `
            -X 567 -Y 116
        $heights = @($standardRowHeight, $slotHeight, $saveHeight, $loadHeight,
            $deleteHeight, $backHeight)
        if (($heights | Select-Object -Unique).Count -ne 1) {
            throw "Button heights differ: standard=$standardRowHeight slot=$slotHeight " +
                "save=$saveHeight load=$loadHeight " +
                "delete=$deleteHeight back=$backHeight"
        }
        if ($standardRowHeight -ne 46) {
            throw "Expected the standard row height to be 46 pixels, got $standardRowHeight."
        }
        $verticalGaps = @()
        for ($row = 1; $row -lt $slotBounds.Count; $row++) {
            $verticalGaps += $slotBounds[$row].Top - $slotBounds[$row - 1].Bottom - 1
        }
        $verticalGaps += $saveBounds.Top - $slotBounds[-1].Bottom - 1
        $verticalGaps += $deleteBounds.Top - $saveBounds.Bottom - 1
        if (($verticalGaps | Where-Object { $_ -ne 6 }).Count -ne 0) {
            throw "Vertical gaps differ from 6 pixels: $($verticalGaps -join ',')."
        }
        $slotWidths = @($slotColumns | ForEach-Object { $_.Width })
        if (($slotWidths | Select-Object -Unique).Count -ne 1) {
            throw "Slot widths are not equal: $($slotWidths -join ',')."
        }
        $slotGaps = @(
            $slotColumns[1].Left - $slotColumns[0].Right - 1
            $slotColumns[2].Left - $slotColumns[1].Right - 1
        )
        if (($slotGaps | Where-Object { $_ -ne 6 }).Count -ne 0) {
            throw "Slot column gaps differ from 6 pixels: $($slotGaps -join ',')."
        }
        if ($previewWidth -ne 269 -or $previewHeight -ne 202) {
            throw "Preview aspect box differs: width=$previewWidth height=$previewHeight."
        }
        if ([Math]::Abs(($previewWidth / $previewHeight) - (4 / 3)) -gt 0.01) {
            throw "Preview box is not 4:3: width=$previewWidth height=$previewHeight."
        }
        if ($gridTop -ne $slotBounds[0].Top) {
            throw "Preview and first slot row tops are not aligned."
        }
        Assert-SameColor -Bitmap $bitmap -FirstX 124 -FirstY $gridTop `
            -SecondX 126 -SecondY ($gridTop + 2) -Description 'Save slot'
        Assert-SameColor -Bitmap $bitmap -FirstX 300 -FirstY 399 `
            -SecondX 500 -SecondY 399 -Description 'Save/load action colors'
        $timePixelCount = Get-BrightPixelCount -Bitmap $bitmap `
            -Left 567 -Top 324 -Width 269 -Height 46
        if ($timePixelCount -lt 20) {
            throw "Save timestamp text was not visible beside the final slot row."
        }
    }
    finally {
        $bitmap.Dispose()
    }

    Write-Host 'Instant save-state visual layout automation passed.'
    Write-Host "Button heights: standard=$standardRowHeight slot=$slotHeight save=$saveHeight load=$loadHeight delete=$deleteHeight back=$backHeight"
    Write-Host "Vertical gaps: $($verticalGaps -join ',')"
    Write-Host "Slot widths: $($slotWidths -join ','); gaps: $($slotGaps -join ',')"
    Write-Host "Preview box: width=$previewWidth height=$previewHeight aspect=4:3"
    Write-Host "Timestamp bright pixels: $timePixelCount"
    Write-Host "Pause screenshot: $pauseScreenshot"
    Write-Host "Save-state screenshot: $saveScreenshot"
}
finally {
    $null = & $AdbPath -s $Serial shell am force-stop $packageName 2>&1
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
    $null = & $AdbPath -s $Serial shell rm '-f' `
        '/data/local/tmp/DingooPie.ini' `
        '/sdcard/dingoopie-pause-menu.png' `
        '/sdcard/dingoopie-save-state-menu.png' 2>&1
}
