param(
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$coreRoot = Join-Path $ProjectRoot 'native\core'
$violations = New-Object System.Collections.Generic.List[string]

function Test-Includes {
    param([string]$RelativeRoot, [string[]]$ForbiddenPrefixes)
    $root = Join-Path $coreRoot $RelativeRoot
    if (-not (Test-Path $root)) { return }
    Get-ChildItem $root -Recurse -File -Include *.h,*.hpp,*.c,*.cc,*.cpp | ForEach-Object {
        $file = $_
        $relativeFile = $file.FullName.Substring($ProjectRoot.Length + 1).Replace('\', '/')
        if ($relativeFile -eq 'native/core/shared/game/game_runtime.cpp') { return }
        $lineNumber = 0
        Get-Content $file.FullName | ForEach-Object {
            $lineNumber++
            if ($_ -match '^\s*#\s*include\s*[<"]([^>"]+)[>"]') {
                $include = $Matches[1].Replace('\', '/')
                foreach ($prefix in $ForbiddenPrefixes) {
                    if ($include.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                        $relative = $file.FullName.Substring($ProjectRoot.Length + 1)
                        $violations.Add("${relative}:$lineNumber forbids include '$include'")
                    }
                }
            }
        }
    }
}

Test-Includes 'app' @('cc/')
Test-Includes 'cc' @('app/')
Test-Includes 'shared' @('app/', 'cc/', 'frontend/')
Test-Includes 'frontend' @('app/', 'cc/')
Test-Includes 'config' @('app/', 'cc/', 'frontend/')

Get-ChildItem $coreRoot -Recurse -File -Include *.h,*.hpp,*.c,*.cc,*.cpp | ForEach-Object {
    $file = $_
    $lineNumber = 0
    Get-Content $file.FullName | ForEach-Object {
        $lineNumber++
        if ($_ -match '^\s*#\s*include\s*[<"]([^>"]+\.cpp)[>"]') {
            $relative = $file.FullName.Substring($ProjectRoot.Length + 1)
            $violations.Add("${relative}:$lineNumber must not include source file '$($Matches[1])'")
        }
    }
}

$forbiddenNames = @(
    'emulator_core.h', 'emulator_core.cpp',
    'save_state.h', 'save_state.cpp',
    'sdk_hle.h', 'sdk_hle.cpp',
    'menu_overlay.inl', 'platform_services.h'
)
Get-ChildItem $coreRoot -Recurse -File | Where-Object {
    $forbiddenNames -contains $_.Name.ToLowerInvariant()
} | ForEach-Object {
    $relative = $_.FullName.Substring($ProjectRoot.Length + 1)
    $violations.Add("$relative uses a forbidden ambiguous file name")
}

$oldDirectories = @('game', 'guest', 'runtime')
foreach ($directory in $oldDirectories) {
    $path = Join-Path $coreRoot $directory
    if (Test-Path $path) {
        $files = @(Get-ChildItem $path -Recurse -File)
        if ($files.Count -gt 0) {
            $violations.Add("native/core/$directory must remain empty or be removed")
        }
    }
}

$forbiddenIdentifiers = @(
    'ccArmRuntime', 'CcArmRuntimeStats',
    'EmulatorRuntimeState', 'EmulatorRuntimeRegisterSnapshot',
    'memTypeStr', 'dumpREG', 'dumpStackCall', 'dumpAsm', 'dumpMem',
    'my_sprintf', 'dingoo_debug', 'mixerOpen', 'mixerClose'
)
Get-ChildItem $coreRoot -Recurse -File -Include *.h,*.hpp,*.c,*.cc,*.cpp | ForEach-Object {
    $file = $_
    $content = Get-Content $file.FullName -Raw
    foreach ($identifier in $forbiddenIdentifiers) {
        if ($content -match "\b$([regex]::Escape($identifier))\b") {
            $relative = $file.FullName.Substring($ProjectRoot.Length + 1)
            $violations.Add("$relative uses forbidden legacy identifier '$identifier'")
        }
    }
}

Get-ChildItem $coreRoot -Recurse -File -Filter *.h | ForEach-Object {
    $relative = $_.FullName.Substring($coreRoot.Length + 1).Replace('\', '/')
    $expected = 'DINGOO_PIE_' + (($relative -replace '[^A-Za-z0-9]', '_').ToUpperInvariant())
    $lines = @(Get-Content $_.FullName -TotalCount 2)
    if ($lines.Count -lt 2 -or $lines[0] -ne "#ifndef $expected" -or
            $lines[1] -ne "#define $expected") {
        $projectRelative = $_.FullName.Substring($ProjectRoot.Length + 1)
        $violations.Add("$projectRelative has a stale or non-canonical include guard")
    }
}

if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output 'Core architecture validation passed.'
