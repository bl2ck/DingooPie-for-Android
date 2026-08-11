param(
    [string]$CcDirectory,
    [string]$OutputDirectory,
    [int]$Samples = 10,
    [string]$AdbPath = 'D:\Program Files\MuMu\emulator\nemu\vmonitor\bin\adb_server.exe',
    [string]$Serial = '127.0.0.1:7555',
    [switch]$AllowBelowTarget,
    [int]$MaxAttempts = 3
)

$ErrorActionPreference = 'Stop'
if (!$CcDirectory) {
    $CcDirectory = Join-Path $env:USERPROFILE ('Desktop\\' + [char]0x65b0 + [char]0x5efa + [char]0x6587 + [char]0x4ef6 + [char]0x5939 + '\\cc')
}
$scriptPath = Join-Path $PSScriptRoot 'test_android_cc_3d_fps.ps1'
if (!(Test-Path -LiteralPath $scriptPath)) { throw "3D FPS script was not found: $scriptPath" }
if (!(Test-Path -LiteralPath $CcDirectory)) { throw "CC directory was not found: $CcDirectory" }
if ($Samples -lt 3) { throw 'At least three samples are required.' }
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot ("..\.tools\cc-3d-regression-{0}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$OutputDirectory = (New-Item -ItemType Directory -Force -Path $OutputDirectory).FullName

$qiyeFile = @([char]0x4e03, [char]0x591c, [char]0x6b63, [char]0x5f0f,
    [char]0x7248, '.') -join ''
$qiyeFile += 'cc'
$tiandidaoFile = @([char]0x5929, [char]0x5730, [char]0x9053, '.') -join ''
$tiandidaoFile += 'cc'
$cases = @(
    @{ Name = 'qiye'; File = $qiyeFile; MedianTarget = 12; MinimumTarget = 10 },
    @{ Name = 'tiandidao'; File = $tiandidaoFile; MedianTarget = 15; MinimumTarget = 12 }
)
$results = @()
foreach ($case in $cases) {
    $gamePath = Join-Path $CcDirectory $case.File
    if (!(Test-Path -LiteralPath $gamePath)) { throw "Required 3D CC game was not found: $gamePath" }
    $summary = $null
    for ($attempt = 1; $attempt -le $MaxAttempts -and !$summary; ++$attempt) {
        $caseOutput = Join-Path $OutputDirectory ("{0}-attempt-{1}" -f $case.Name, $attempt)
        & powershell -NoProfile -ExecutionPolicy Bypass -File $scriptPath `
            -Game $case.Name -GamePath $gamePath -AdbPath $AdbPath -Serial $Serial `
            -Samples $Samples -OutputDirectory $caseOutput
        if ($LASTEXITCODE -eq 0) {
            $summaryPath = Join-Path $caseOutput 'summary.json'
            if (Test-Path -LiteralPath $summaryPath) {
                $summary = Get-Content -LiteralPath $summaryPath -Raw | ConvertFrom-Json
            }
        }
        if (!$summary -and $attempt -lt $MaxAttempts) { Start-Sleep -Seconds 2 }
    }
    if (!$summary) { throw "3D FPS test failed for $($case.Name) after $MaxAttempts attempts." }
    $summary | Add-Member -NotePropertyName median_target -NotePropertyValue $case.MedianTarget
    $summary | Add-Member -NotePropertyName minimum_target -NotePropertyValue $case.MinimumTarget
    $summary | Add-Member -NotePropertyName target_met -NotePropertyValue `
        ([bool]($summary.median -ge $case.MedianTarget -and $summary.minimum -ge $case.MinimumTarget))
    $results += $summary
}
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding UTF8
$results | Format-Table game, samples, minimum, median, maximum, median_target, minimum_target, target_met
if (!$AllowBelowTarget -and ($results | Where-Object { !$_.target_met })) {
    throw 'One or more CC 3D games are below the performance target.'
}
