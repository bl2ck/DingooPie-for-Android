param([switch]$Force)

$ErrorActionPreference = 'Stop'
$projectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$toolsRoot = Join-Path $projectRoot '.tools\android'
$thirdPartyRoot = Join-Path $projectRoot 'third_party'
$patchFile = Join-Path $projectRoot 'patches\ppsspp-irjit-dingoo.patch'

$dependencies = @(
    @{
        Name = 'SDL2-2.26.5'
        File = 'SDL2-2.26.5.zip'
        Url = 'https://www.libsdl.org/release/SDL2-2.26.5.zip'
        Sha256 = 'd88362fc3ee350a037e31381db00df764a294244bac8e427b8c67c6ca4d7e6fd'
    },
    @{
        Name = 'ppsspp-master'
        File = 'ppsspp-dffde6e18902a17d9c3b36806c0a0f94455eef8d.zip'
        Url = 'https://codeload.github.com/hrydgard/ppsspp/zip/dffde6e18902a17d9c3b36806c0a0f94455eef8d'
        Sha256 = 'a3bf710623430f10744dcd9adabe2a704ef2101e6895702af82fd5814a4216e0'
    }
)

New-Item -ItemType Directory -Force -Path $toolsRoot, $thirdPartyRoot | Out-Null
foreach ($dependency in $dependencies) {
    $destination = Join-Path $toolsRoot $dependency.File
    if ($Force -or !(Test-Path -LiteralPath $destination) -or
        (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $dependency.Sha256) {
        $temporary = "$destination.download"
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        Invoke-WebRequest -Uri $dependency.Url -OutFile $temporary -UseBasicParsing
        $hash = (Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $dependency.Sha256) { throw "SHA256 mismatch for $($dependency.File): $hash" }
        Move-Item -LiteralPath $temporary -Destination $destination -Force
    }

    $destinationDirectory = Join-Path $thirdPartyRoot $dependency.Name
    if ($Force -or !(Test-Path -LiteralPath $destinationDirectory)) {
        if (Test-Path -LiteralPath $destinationDirectory) { Remove-Item -LiteralPath $destinationDirectory -Recurse -Force }
        Expand-Archive -LiteralPath $destination -DestinationPath $thirdPartyRoot -Force
        if ($dependency.Name -eq 'ppsspp-master') {
            $extracted = Get-ChildItem -LiteralPath $thirdPartyRoot -Directory | Where-Object { $_.Name -like 'ppsspp-*' } | Select-Object -First 1
            if (!$extracted) { throw 'PPSSPP source extraction failed.' }
            if ($extracted.FullName -ne $destinationDirectory) { Rename-Item -LiteralPath $extracted.FullName -NewName 'ppsspp-master' }
        }
    }
}

$ppssppRoot = Join-Path $thirdPartyRoot 'ppsspp-master'
$patchMarker = Join-Path $ppssppRoot '.dingoopie-patch-applied'
$patchProbe = Join-Path $ppssppRoot 'Core\MIPS\ARM64\Arm64IRJit.h'
$patchComplete = (Test-Path -LiteralPath $patchMarker) -and
    (Test-Path -LiteralPath $patchProbe) -and
    (Select-String -LiteralPath $patchProbe -Pattern 'dingooRead8Fallback_' -SimpleMatch -Quiet)
if ($Force -or !$patchComplete) {
    if (Test-Path -LiteralPath $patchMarker) { Remove-Item -LiteralPath $patchMarker -Force }
    Push-Location $projectRoot
    try {
        git apply --check --unidiff-zero --ignore-space-change --whitespace=nowarn '--directory=third_party/ppsspp-master' $patchFile
        if ($LASTEXITCODE -ne 0) { throw 'PPSSPP integration patch check failed. Run with -Force to refresh a partial dependency tree.' }
        git apply --unidiff-zero --ignore-space-change --whitespace=nowarn '--directory=third_party/ppsspp-master' $patchFile
        if ($LASTEXITCODE -ne 0) { throw 'PPSSPP integration patch apply failed.' }
        if (!(Select-String -LiteralPath $patchProbe -Pattern 'dingooRead8Fallback_' -SimpleMatch -Quiet)) {
            throw 'PPSSPP integration patch verification failed.'
        }
        Set-Content -LiteralPath $patchMarker -Value 'Applied ppsspp-irjit-dingoo.patch'
    }
    finally { Pop-Location }
}

Write-Host "SDL: $(Join-Path $thirdPartyRoot 'SDL2-2.26.5')"
Write-Host "PPSSPP: $ppssppRoot"
