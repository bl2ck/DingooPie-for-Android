DingooPie Android
=================

中文
----

丁果派 DingooPie Android 是 Android 平台的 `.app` / `.cc` 游戏模拟器，用于运行
丁果 A320、歌美 X760+ 和歌美 A330 掌机游戏。`.app` 与 `.cc` 格式文件归丁果科技所有；
本项目和发布包不包含游戏样本，请使用自行合法取得的文件。

版本：1.0
Powered by BL2CK Software
版权：Copyright (c) 2026 BL2CK

## 快速使用

1. 安装 `DingooPie.apk` 并启动。
2. 在游戏库中添加游戏目录，或导入单个 `.app` / `.cc` 文件。
3. 授予 Android 文件访问权限，等待扫描完成后点击游戏启动。
4. 游戏中按 Android 返回键打开暂停菜单，可恢复、重启、切换游戏或调整设置。

从可写目录导入的游戏会优先把存档和诊断日志保存在游戏旁边。单文件导入、只读目录或
目录授权失效时，模拟器会改用应用私有的 `app-saves` 或 `cc-saves` 目录。移除游戏库
条目不会删除原始游戏文件。

## 默认设置

| 项目 | 默认值 |
| --- | --- |
| 抗锯齿 | 关闭 |
| 滤镜 | 正常 |
| 亮度 / 对比度 / 伽马 / 饱和度 | 100% / 100% / 100% / 100% |
| 最小化时 | 暂停 |
| 屏幕方向 | 横屏 |
| FPS 显示 | 关闭 |
| 主音量 | 100% |
| 音频缓冲 | 2048 采样 |
| 音频效果 | 关闭 |
| 禁用音频 | 关闭 |
| 禁用系统输入法 | 开启 |
| 虚拟按键 | 开启 |
| CPU 后端 | 自动；APP 使用 PPSSPP IR JIT，CC 使用优化 ARM32 解释器 |
| CPU 时钟 | 自动，基线 336 MHz |
| 运行速度 | 自动，基线 65% |
| 延迟比例 | 自动，基线 1.0 |
| 金手指 | 关闭 |
| 界面语言 | 中文 |
| 性能日志 | 关闭 |

兼容性配置可按游戏覆盖自动后端、时钟、运行速度或延迟比例，因此实际运行值可能与基线不同。

## 菜单与配置

- `游戏库`：添加游戏目录、导入单个游戏、启动游戏、移除条目和刷新扫描结果。
- `暂停菜单`：恢复游戏、重启游戏、切换游戏、选项、设置和退出应用。
- `选项 > 视频`：抗锯齿、滤镜、色彩参数、最小化行为、屏幕方向和 FPS 显示。
- `选项 > 音频`：主音量、音频缓冲、音频效果和禁用音频。
- `选项 > 输入`：系统输入法、虚拟按键和手柄按键映射。
- `设置`：CPU 后端、CPU 时钟、运行速度、延迟比例、金手指管理器、语言和恢复默认设置。
- `关于`：版本、支持格式和软件信息。

设置会自动保存到应用私有目录中的 `DingooPie.ini`。视频、音频、输入、运行参数、
金手指和语言设置由 APP 与 CC 运行时共享；自动与兼容模式仍会选择各格式对应的实现。

## 按键

触摸屏默认显示虚拟方向键、A/B/X/Y、START、SELECT 和肩键，并支持多点触控与组合按键。
支持 SDL GameController 兼容手柄，可在 `选项 > 输入 > 手柄按键映射` 中设置按键。

连接物理键盘时使用以下默认映射：

| 键盘按键 | 丁果 A320 / 歌美 X760+ / 歌美 A330 控制 |
| --- | --- |
| 方向键 / WASD | 方向键 |
| L | A |
| K | B |
| I | X |
| J | Y |
| 1 / Q | SELECT |
| 0 / O | START |
| 左 Shift | 左肩键 |
| 右 Shift | 右肩键 |
| Backspace / Home | POWER |
| Android 返回键 | 打开暂停菜单 |

## 金手指

金手指默认关闭，并按游戏格式优先加载同名文件：

```text
Game.app -> Game.app.cht
Game.cc  -> Game.cc.cht
```

格式专用文件不存在时才回退到 `Game.cht`，因此同名 APP 与 CC 游戏的金手指文件和
勾选状态不会互相覆盖。进入 `设置 > 金手指管理器` 后可启用金手指、选择功能、全部启用、
全部停用、应用或刷新。文件缺失或不适用于当前游戏时，模拟器会保持金手指不可用。

## 调试与存档

- APP 使用 MIPS 运行时；自动模式优先使用 PPSSPP IR JIT，兼容模式使用基础解释器。
- CC 使用 ARM32 运行时；自动模式使用优化解释器路径，兼容模式使用基础路径。
- 两种格式共享视频、音频、输入、设置、存档、金手指和崩溃日志服务，但运行时彼此隔离。
- 客体执行失败会生成 `DingooPie-crash-*.log`；无法写入游戏目录时保存到对应格式的私有存档目录。
- 原生运行日志用于诊断启动、文件访问、包解析和运行时问题。

## 构建

要求：Windows PowerShell 5.1 或 PowerShell 7、JDK 17、Android SDK Platform 35，
以及 Android NDK `26.3.11579264`。

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
```

发布构建需要配置签名环境变量；详细步骤见 `docs/BUILDING.md`。测试矩阵、模拟器兼容性、
音频、金手指、输入法和存档自动化见 `docs/TESTING.md`。

## 源码目录

```text
app/                    Android Gradle 应用、SDL Activity、资源和 Java 平台服务
native/android/         Android JNI、文件描述符和平台适配层
native/core/app/        APP 专用 MIPS 运行时与 PPSSPP IR JIT 集成
native/core/cc/         CC 专用 ARM32 运行时与兼容辅助代码
native/core/config/     设置、兼容性配置和金手指运行时
native/core/frontend/   SDL 视频、音频、菜单和虚拟按键
native/core/game/       格式检测、路径处理、历史记录和运行时分派
native/core/guest/      两种客体共享的软件包、文件、音频和文本服务
native/core/runtime/    执行后端、暂停、日志、调试和崩溃处理
scripts/                依赖、构建、发布、验证和回归脚本
tests/                  原生兼容性回归测试
docs/                   架构、构建和测试维护文档
```

架构边界和命名规则见 `docs/ARCHITECTURE.md`。仓库文本文件统一使用 UTF-8 无 BOM、
CRLF 换行并以 CRLF 结尾。

English
-------

DingooPie Android is an Android emulator for Dingoo `.app` and `.cc` games made for
the Dingoo A320, Gemei X760+, and Gemei A330 handhelds. The package formats belong
to Dingoo Technology. No game images are included; use only files obtained legally.

Version: 1.0
Powered by BL2CK Software
Copyright: Copyright (c) 2026 BL2CK

## Quick Start

1. Install and launch `DingooPie.apk`.
2. Add a game folder or import one `.app` / `.cc` file from the game library.
3. Grant Android file access and wait for the library scan to finish.
4. Tap a game to start it. Press Android Back during play to open the pause menu.

Games imported from a writable folder keep saves and diagnostic logs beside the game.
Single-file imports, read-only locations, and expired grants use the format-isolated
private `app-saves` or `cc-saves` directory. Removing an entry never deletes the game.

## Default Settings

| Option | Default |
| --- | --- |
| Anti-aliasing | Off |
| Color effect | Normal |
| Brightness / contrast / gamma / saturation | 100% / 100% / 100% / 100% |
| When minimized | Pause |
| Screen orientation | Landscape |
| FPS display | Off |
| Master volume | 100% |
| Audio buffer | 2048 samples |
| Audio effect | Off |
| Disable audio | Off |
| Disable system IME | On |
| Virtual controls | On |
| CPU backend | Auto; PPSSPP IR JIT for APP, optimized ARM32 interpreter for CC |
| CPU clock | Auto, 336 MHz baseline |
| Runtime speed | Auto, 65% baseline |
| Delay scale | Auto, 1.0 baseline |
| Cheats | Off |
| UI language | Chinese |
| Performance logging | Off |

Per-game compatibility profiles may override automatic backend, clock, speed, or delay values.

## Menus And Configuration

- `Game Library`: add folders, import a game, launch games, remove entries, and refresh scans.
- `Pause Menu`: resume, restart, switch games, open options or settings, and exit.
- `Options > Video`: anti-aliasing, color effect and controls, minimized behavior, orientation, and FPS.
- `Options > Audio`: master volume, buffer size, audio effect, and audio disable.
- `Options > Input`: system IME policy, virtual controls, and controller mapping.
- `Settings`: execution mode, CPU clock, runtime speed, delay scale, cheat manager, language, and reset.
- `About`: version, supported formats, and software information.

Settings are saved automatically in the application-private `DingooPie.ini`. APP and CC
share frontend and platform services while keeping their execution implementations isolated.

## Keys

Virtual controls are enabled by default and support multi-touch holds and combinations.
SDL GameController-compatible devices can be remapped under `Options > Input`.

| Keyboard | Dingoo control |
| --- | --- |
| Arrow keys / WASD | D-pad |
| L / K / I / J | A / B / X / Y |
| 1 / Q | SELECT |
| 0 / O | START |
| Left / Right Shift | Left / right shoulder |
| Backspace / Home | POWER |
| Android Back | Open the pause menu |

## Cheats

Cheats are disabled by default. `Game.app.cht` and `Game.cc.cht` are preferred;
`Game.cht` is used only when the format-specific file is absent. Use
`Settings > Cheat Manager` to select, apply, disable, or refresh cheat features.

## Debugging And Saves

- APP Auto prefers PPSSPP IR JIT; Compatibility uses the base MIPS interpreter.
- CC Auto uses the optimized ARM32 interpreter; Compatibility uses the base path.
- Guest failures write `DingooPie-crash-*.log` beside the game or in private saves.
- Native logs diagnose startup, file access, package parsing, and execution problems.

## Build

Requires PowerShell, JDK 17, Android SDK Platform 35, and NDK `26.3.11579264`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
```

See `docs/BUILDING.md`, `docs/TESTING.md`, and `docs/ARCHITECTURE.md` for maintained details.

## Source Layout

```text
app/                    Android Gradle app, SDL activity, resources, and Java services
native/android/         Android JNI and platform adapters
native/core/app/        APP-only MIPS runtime and PPSSPP IR JIT integration
native/core/cc/         CC-only ARM32 runtime and compatibility helpers
native/core/config/     Settings, compatibility profiles, and cheat runtime
native/core/frontend/   SDL video, audio, menus, and virtual controls
native/core/game/       Format detection, paths, history, and runtime dispatch
native/core/guest/      Package, filesystem, audio, and text services shared by guests
native/core/runtime/    Execution backends, pause, logs, debugging, and crash handling
scripts/                Dependency, build, release, validation, and regression scripts
tests/                  Native compatibility regressions
docs/                   Architecture, build, and test documentation
```

Repository text files use UTF-8 without BOM, CRLF line endings, and a final CRLF.
