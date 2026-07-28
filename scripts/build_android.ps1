param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$AndroidSdkRoot,
    [string]$OutputDirectory,
    [string]$ReleaseSigningProperties,
    [switch]$AllowUnsignedRelease
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Resolve-AndroidSdkRoot {
    $candidates = @(
        $AndroidSdkRoot,
        $env:ANDROID_SDK_ROOT,
        $env:ANDROID_HOME,
        (Join-Path $projectRoot '.tools\android\sdk')
    )
    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        $adb = Join-Path $candidate 'platform-tools\adb.exe'
        if (Test-Path -LiteralPath $adb) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'Android SDK platform-tools are required. Pass -AndroidSdkRoot or set ANDROID_SDK_ROOT.'
}

function Resolve-JavaHome {
    $candidates = @($env:JAVA_HOME)
    $java = Get-Command java.exe -ErrorAction SilentlyContinue
    if ($java) {
        $candidates += Split-Path -Parent (Split-Path -Parent $java.Source)
    }
    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'bin\java.exe')) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    throw 'JDK 17 is required. Set JAVA_HOME to a valid JDK installation.'
}

function Import-ReleaseSigningProperties {
    param([string]$Path)

    if (!$Path) {
        return
    }
    if (!(Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Release signing properties were not found: $Path"
    }

    $environmentNames = @{
        storeFile = 'DINGOO_PIE_RELEASE_STORE_FILE'
        storePassword = 'DINGOO_PIE_RELEASE_STORE_PASSWORD'
        keyAlias = 'DINGOO_PIE_RELEASE_KEY_ALIAS'
        keyPassword = 'DINGOO_PIE_RELEASE_KEY_PASSWORD'
    }
    foreach ($line in Get-Content -LiteralPath $Path) {
        $trimmed = $line.Trim()
        if (!$trimmed -or $trimmed.StartsWith('#') -or $trimmed.StartsWith('!')) {
            continue
        }
        $separator = $trimmed.IndexOf('=')
        if ($separator -le 0) {
            continue
        }
        $name = $trimmed.Substring(0, $separator).Trim()
        if (!$environmentNames.ContainsKey($name)) {
            continue
        }
        $environmentName = $environmentNames[$name]
        if ([Environment]::GetEnvironmentVariable($environmentName, 'Process')) {
            continue
        }
        $value = $trimmed.Substring($separator + 1).Trim()
        if ($name -eq 'storeFile' -and ![System.IO.Path]::IsPathRooted($value)) {
            $value = [System.IO.Path]::GetFullPath((Join-Path $projectRoot $value))
        }
        [Environment]::SetEnvironmentVariable($environmentName, $value, 'Process')
    }
}

$sdkRoot = Resolve-AndroidSdkRoot
$releaseSigningVariables = @(
    'DINGOO_PIE_RELEASE_STORE_FILE',
    'DINGOO_PIE_RELEASE_STORE_PASSWORD',
    'DINGOO_PIE_RELEASE_KEY_ALIAS',
    'DINGOO_PIE_RELEASE_KEY_PASSWORD'
)
$previousReleaseSigningValues = @{}
foreach ($variableName in $releaseSigningVariables) {
    $previousReleaseSigningValues[$variableName] =
        [Environment]::GetEnvironmentVariable($variableName, 'Process')
}
if ($Configuration -eq 'Release') {
    $signingPropertiesPath = $ReleaseSigningProperties
    if (!$signingPropertiesPath) {
        $defaultSigningProperties = Join-Path $projectRoot '.tools\signing\release-signing.properties'
        if (Test-Path -LiteralPath $defaultSigningProperties -PathType Leaf) {
            $signingPropertiesPath = $defaultSigningProperties
        }
    }
    Import-ReleaseSigningProperties -Path $signingPropertiesPath
}
$releaseSigningAvailable = $true
foreach ($variableName in $releaseSigningVariables) {
    if (!(Get-Item -LiteralPath "Env:$variableName" -ErrorAction SilentlyContinue)) {
        $releaseSigningAvailable = $false
        break
    }
}
if ($Configuration -eq 'Release' -and !$releaseSigningAvailable -and !$AllowUnsignedRelease) {
    throw 'Release signing variables are missing. Configure signing or pass -AllowUnsignedRelease.'
}
if (!(Test-Path -LiteralPath (Join-Path $projectRoot 'third_party\SDL2-2.26.5\Android.mk'))) {
    throw 'Android native dependencies are missing. Run scripts\bootstrap_android.ps1 first.'
}

$env:JAVA_HOME = Resolve-JavaHome
$env:ANDROID_HOME = $sdkRoot
$env:ANDROID_SDK_ROOT = $sdkRoot
$previousUnsignedRelease = $env:DINGOO_PIE_ALLOW_UNSIGNED_RELEASE
if ($Configuration -eq 'Release' -and !$releaseSigningAvailable) {
    $env:DINGOO_PIE_ALLOW_UNSIGNED_RELEASE = '1'
}

$task = if ($Configuration -eq 'Release') { 'assembleRelease' } else { 'assembleDebug' }
try {
    Push-Location $projectRoot
    try {
        & '.\gradlew.bat' --no-daemon $task
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    } finally {
        Pop-Location
    }
} finally {
    $env:DINGOO_PIE_ALLOW_UNSIGNED_RELEASE = $previousUnsignedRelease
    foreach ($variableName in $releaseSigningVariables) {
        [Environment]::SetEnvironmentVariable(
            $variableName, $previousReleaseSigningValues[$variableName], 'Process')
    }
}

$variant = $Configuration.ToLowerInvariant()
$metadataPath = Join-Path $projectRoot "app\build\outputs\apk\$variant\output-metadata.json"
if (!(Test-Path -LiteralPath $metadataPath)) {
    throw "APK metadata was not produced: $metadataPath"
}
$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
$outputFile = $metadata.elements[0].outputFile
$apk = Join-Path (Split-Path -Parent $metadataPath) $outputFile
if (!(Test-Path -LiteralPath $apk)) {
    throw "APK was not produced: $apk"
}

if ($OutputDirectory) {
    $resolvedOutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
    New-Item -ItemType Directory -Path $resolvedOutputDirectory -Force | Out-Null
    $unsignedSuffix = if ($Configuration -eq 'Release' -and !$releaseSigningAvailable) {
        '-unsigned'
    } else {
        ''
    }
    $artifactName = "DingooPie-Android-v$($metadata.elements[0].versionName)-$variant$unsignedSuffix.apk"
    $destination = Join-Path $resolvedOutputDirectory $artifactName
    Copy-Item -LiteralPath $apk -Destination $destination -Force
    $apk = $destination
}

Write-Host "APK: $apk"
