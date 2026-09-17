param(
    [string]$AndroidSdkRoot
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ndkVersion = '26.3.11579264'

if (!$AndroidSdkRoot) {
    $localProperties = Join-Path $projectRoot 'local.properties'
    if (Test-Path -LiteralPath $localProperties) {
        $sdkLine = Get-Content -LiteralPath $localProperties |
            Where-Object { $_ -match '^sdk\.dir=' } |
            Select-Object -First 1
        if ($sdkLine) {
            $AndroidSdkRoot = $sdkLine.Substring('sdk.dir='.Length).
                Replace('\:', ':').Replace('\\', '\')
        }
    }
}
if (!$AndroidSdkRoot) { $AndroidSdkRoot = $env:ANDROID_SDK_ROOT }
if (!$AndroidSdkRoot) {
    throw 'Android SDK is required. Pass -AndroidSdkRoot or configure local.properties.'
}

$resolvedSdkRoot = (Resolve-Path -LiteralPath $AndroidSdkRoot).Path
$compiler = Join-Path $resolvedSdkRoot `
    "ndk\$ndkVersion\toolchains\llvm\prebuilt\windows-x86_64\bin\x86_64-linux-android23-clang++.cmd"
if (!(Test-Path -LiteralPath $compiler)) {
    throw "Android NDK compiler was not found: $compiler"
}

$coreInclude = Join-Path $projectRoot 'native\core'
$androidInclude = Join-Path $projectRoot 'native\android'
$testSource = Join-Path $projectRoot 'tests\keyboard_mapping_test.cpp'
& $compiler -std=c++17 -fsyntax-only -I $coreInclude -I $androidInclude $testSource
if ($LASTEXITCODE -ne 0) {
    throw 'Default keyboard mapping regression test failed.'
}

function Assert-ContainsLiteral([string]$Path, [string]$Expected) {
    $content = [System.IO.File]::ReadAllText($Path)
    if (!$content.Contains($Expected)) {
        throw "Expected text was not found in $Path`: $Expected"
    }
}

$sdlKeymap = Join-Path $projectRoot `
    'third_party\SDL2-2.26.5\src\video\android\SDL_androidkeyboard.c'
foreach ($mapping in @(
        'SDL_SCANCODE_A, /* AKEYCODE_A */',
        'SDL_SCANCODE_D, /* AKEYCODE_D */',
        'SDL_SCANCODE_I, /* AKEYCODE_I */',
        'SDL_SCANCODE_J, /* AKEYCODE_J */',
        'SDL_SCANCODE_K, /* AKEYCODE_K */',
        'SDL_SCANCODE_L, /* AKEYCODE_L */',
        'SDL_SCANCODE_O, /* AKEYCODE_O */',
        'SDL_SCANCODE_Q, /* AKEYCODE_Q */',
        'SDL_SCANCODE_S, /* AKEYCODE_S */',
        'SDL_SCANCODE_W, /* AKEYCODE_W */',
        'SDL_SCANCODE_0, /* AKEYCODE_0 */',
        'SDL_SCANCODE_1, /* AKEYCODE_1 */',
        'SDL_SCANCODE_UP, /* AKEYCODE_DPAD_UP */',
        'SDL_SCANCODE_DOWN, /* AKEYCODE_DPAD_DOWN */',
        'SDL_SCANCODE_LEFT, /* AKEYCODE_DPAD_LEFT */',
        'SDL_SCANCODE_RIGHT, /* AKEYCODE_DPAD_RIGHT */',
        'SDL_SCANCODE_LSHIFT, /* AKEYCODE_SHIFT_LEFT */',
        'SDL_SCANCODE_RSHIFT, /* AKEYCODE_SHIFT_RIGHT */',
        'SDL_SCANCODE_BACKSPACE, /* AKEYCODE_DEL */',
        'SDL_SCANCODE_HOME, /* AKEYCODE_MOVE_HOME */')) {
    Assert-ContainsLiteral $sdlKeymap $mapping
}

$sdlActivity = Join-Path $projectRoot 'app\src\main\java\org\libsdl\app\SDLActivity.java'
Assert-ContainsLiteral $sdlActivity 'InputDevice.SOURCE_KEYBOARD'
Assert-ContainsLiteral $sdlActivity 'onNativeKeyDown(keyCode);'
Assert-ContainsLiteral $sdlActivity 'onNativeKeyUp(keyCode);'

$sdlSurface = Join-Path $projectRoot 'app\src\main\java\org\libsdl\app\SDLSurface.java'
Assert-ContainsLiteral $sdlSurface 'SDLActivity.handleKeyEvent(v, keyCode, event, null)'

$activity = Join-Path $projectRoot `
    'app\src\main\java\com\dingoopie\android\DingooPieActivity.java'
Assert-ContainsLiteral $activity 'SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);'
Assert-ContainsLiteral $activity 'SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);'

$frontendShell = Join-Path $projectRoot 'native\android\frontend\frontend_shell.cpp'
Assert-ContainsLiteral $frontendShell 'SDL_SCANCODE_AC_BACK)'

Write-Host 'Android physical keyboard mapping regression passed.'
