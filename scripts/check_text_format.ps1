param([switch]$Fix)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$binaryExtensions = @(
    '.aab', '.apk', '.jar', '.pdf', '.png', '.zip'
)
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)

Push-Location $projectRoot
try {
    $files = @(& git ls-files --cached --others --exclude-standard)
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to enumerate repository files.'
    }

    $invalidFiles = [System.Collections.Generic.List[string]]::new()
    foreach ($relativePath in $files) {
        $extension = [System.IO.Path]::GetExtension($relativePath).ToLowerInvariant()
        if ($binaryExtensions -contains $extension) {
            continue
        }

        $path = Join-Path $projectRoot $relativePath
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) {
            continue
        }
        $bytes = [System.IO.File]::ReadAllBytes($path)
        $hasUtf8Bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and
            $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
        if ($hasUtf8Bom) {
            if ($Fix) {
                $bytes = $bytes[3..($bytes.Length - 1)]
            } else {
                $invalidFiles.Add("$relativePath`: UTF-8 BOM is not allowed")
                continue
            }
        }

        try {
            $text = $utf8.GetString($bytes)
        } catch {
            $invalidFiles.Add("$relativePath`: not valid UTF-8")
            continue
        }

        $hasInvalidLineEnding = $text -match '(?<!\r)\n|\r(?!\n)'
        $hasFinalLineEnding = $text.Length -eq 0 -or $text.EndsWith("`r`n")
        if (!$hasInvalidLineEnding -and $hasFinalLineEnding) {
            continue
        }

        if ($Fix) {
            $normalized = $text -replace "`r`n", "`n" -replace "`r", "`n"
            $normalized = $normalized.TrimEnd("`n") -replace "`n", "`r`n"
            $normalized += "`r`n"
            [System.IO.File]::WriteAllText($path, $normalized, $utf8)
        } else {
            if ($hasInvalidLineEnding) {
                $invalidFiles.Add("$relativePath`: expected CRLF line endings")
            }
            if (!$hasFinalLineEnding) {
                $invalidFiles.Add("$relativePath`: missing final CRLF")
            }
        }
    }

    if ($invalidFiles.Count -gt 0) {
        $invalidFiles | ForEach-Object { Write-Error $_ }
        exit 1
    }
} finally {
    Pop-Location
}

Write-Host $(if ($Fix) { 'Text files normalized.' } else { 'Text format validation passed.' })
