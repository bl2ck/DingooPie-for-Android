param(
    [string]$AndroidSdkRoot,
    [switch]$ForceReconfigure
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$toolsRoot = Join-Path $projectRoot '.tools'
$dynarmicSource = Join-Path $toolsRoot 'dynarmic-eval-20260810'
$buildDirectory = Join-Path $toolsRoot 'dynarmic-android-x86_64-build'
$boostPackage = Join-Path $toolsRoot 'boost.1.84.0.nupkg'
$boostDirectory = Join-Path $toolsRoot 'boost-nuget-1.84.0'
$dynarmicCommit = 'a41c380246d3d9f9874f0f792d234dc0cc17c180'
$boostSha512 = '5A3E5608BD724DEC4AB9B45BC329EAF227F16A05FE1CF6A012F9759A8811865DD7BE11D04CF2FFF959A17EF649043CAF285BCF8A932EDD92E50E2551E6173E06'
$cmakeVersion = '3.22.1'
$ndkVersion = '26.3.11579264'

if (!$AndroidSdkRoot) {
    $localProperties = Join-Path $projectRoot 'local.properties'
    if (Test-Path -LiteralPath $localProperties) {
        $sdkLine = Get-Content -LiteralPath $localProperties |
            Where-Object { $_ -like 'sdk.dir=*' } | Select-Object -First 1
        if ($sdkLine) {
            $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).
                Replace('\:', ':').Replace('\\', '\')
        }
    }
}
if (!$AndroidSdkRoot -or !(Test-Path -LiteralPath $AndroidSdkRoot)) {
    throw 'Android SDK is required. Pass -AndroidSdkRoot or configure local.properties.'
}
$AndroidSdkRoot = (Resolve-Path -LiteralPath $AndroidSdkRoot).Path
$null = New-Item -ItemType Directory -Force -Path $toolsRoot

if (!(Test-Path -LiteralPath $dynarmicSource)) {
    & git clone --filter=blob:none https://gitlab.com/suyu-emu/dynarmic.git $dynarmicSource
    if ($LASTEXITCODE -ne 0) { throw 'Could not clone Dynarmic.' }
    & git -C $dynarmicSource checkout --detach $dynarmicCommit
    if ($LASTEXITCODE -ne 0) { throw 'Could not check out the pinned Dynarmic commit.' }
}
$actualCommit = (& git -C $dynarmicSource rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $dynarmicCommit) {
    throw "Dynarmic source must be pinned to $dynarmicCommit; found $actualCommit"
}

if (!(Test-Path -LiteralPath $boostPackage) -or
    (Get-FileHash -LiteralPath $boostPackage -Algorithm SHA512).Hash -ne $boostSha512) {
    Invoke-WebRequest -Uri 'https://api.nuget.org/v3-flatcontainer/boost/1.84.0/boost.1.84.0.nupkg' `
        -OutFile $boostPackage
}
if ((Get-FileHash -LiteralPath $boostPackage -Algorithm SHA512).Hash -ne $boostSha512) {
    throw 'Downloaded Boost 1.84.0 package failed SHA-512 verification.'
}
$boostInclude = Join-Path $boostDirectory 'lib/native/include'
if (!(Test-Path -LiteralPath (Join-Path $boostInclude 'boost/version.hpp'))) {
    $boostZip = Join-Path $toolsRoot 'boost.1.84.0.zip'
    Copy-Item -LiteralPath $boostPackage -Destination $boostZip -Force
    if (Test-Path -LiteralPath $boostDirectory) {
        $resolvedBoost = (Resolve-Path -LiteralPath $boostDirectory).Path
        if (!$resolvedBoost.StartsWith($toolsRoot + '\')) {
            throw "Unsafe Boost extraction path: $resolvedBoost"
        }
        Remove-Item -LiteralPath $resolvedBoost -Recurse -Force
    }
    Expand-Archive -LiteralPath $boostZip -DestinationPath $boostDirectory -Force
}

$cmake = Join-Path $AndroidSdkRoot "cmake/$cmakeVersion/bin/cmake.exe"
$ninja = Join-Path $AndroidSdkRoot "cmake/$cmakeVersion/bin/ninja.exe"
if (!(Test-Path -LiteralPath $cmake) -or !(Test-Path -LiteralPath $ninja)) {
    $sdkManager = Join-Path $AndroidSdkRoot 'cmdline-tools/latest/bin/sdkmanager.bat'
    if (!(Test-Path -LiteralPath $sdkManager)) {
        throw 'sdkmanager.bat is required to install the pinned CMake version.'
    }
    & $sdkManager "--sdk_root=$AndroidSdkRoot" "cmake;$cmakeVersion"
    if ($LASTEXITCODE -ne 0) { throw "Could not install CMake $cmakeVersion." }
}
$toolchain = Join-Path $AndroidSdkRoot "ndk/$ndkVersion/build/cmake/android.toolchain.cmake"
if (!(Test-Path -LiteralPath $toolchain)) {
    throw "Android NDK $ndkVersion is required: $toolchain"
}
if ($ForceReconfigure -and (Test-Path -LiteralPath $buildDirectory)) {
    $resolvedBuild = (Resolve-Path -LiteralPath $buildDirectory).Path
    if (!$resolvedBuild.StartsWith($toolsRoot + '\')) {
        throw "Unsafe Dynarmic build path: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

& $cmake -S $dynarmicSource -B $buildDirectory -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-23 `
    -DCMAKE_BUILD_TYPE=Release -DDYNARMIC_FRONTENDS=A32 `
    -DDYNARMIC_TESTS=OFF -DBUILD_TESTING=OFF `
    -DDYNARMIC_USE_BUNDLED_EXTERNALS=ON `
    -DDYNARMIC_USE_PRECOMPILED_HEADERS=OFF `
    -DDYNARMIC_WARNINGS_AS_ERRORS=OFF `
    -DDYNARMIC_ENABLE_NO_EXECUTE_SUPPORT=ON `
    "-DBOOST_ROOT=$boostInclude" "-DBOOST_INCLUDEDIR=$boostInclude" `
    "-DBoost_INCLUDE_DIR=$boostInclude"
if ($LASTEXITCODE -ne 0) { throw 'Dynarmic CMake configuration failed.' }
& $cmake --build $buildDirectory --target dynarmic -j 8
if ($LASTEXITCODE -ne 0) { throw 'Dynarmic x86_64 static library build failed.' }

$library = Join-Path $buildDirectory 'src/dynarmic/libdynarmic.a'
if (!(Test-Path -LiteralPath $library)) {
    throw "Dynarmic build did not produce $library"
}
Write-Host "Dynarmic Android x86_64 ready: $library"
