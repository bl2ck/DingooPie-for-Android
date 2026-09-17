# DingooPie Android
## 中文
丁果派 DingooPie Android 是 Android 平台的 `.app` / `.cc` / `.c2m` / `.c2s` / `.c3s` 游戏模拟器，用于运行
丁果 A320、歌美 X760+ 和歌美 A330 掌机游戏。游戏文件格式归原厂商所有；
本项目和发布包不包含游戏样本，请使用自行合法取得的文件。
其中，APP 游戏使用 Ingenic JZ4732 SoC 的 XBurst/MIPS 架构，CC 游戏使用 ChinaChip
CC1800 SoC 的 ARM11 架构。模拟器分别通过 APP MIPS 运行时和 CC ARM32 运行时执行这两类游戏。

### 支持的游戏样本后缀

| 后缀 | 类型与处理方式 |
| --- | --- |
| `.app` | 丁果 APP 游戏包，使用 APP MIPS 运行时。 |
| `.cc` | 歌美 CC 系列游戏包，使用 CC ARM32 运行时。 |
| `.c2m` | 使用的 CC 系列容器后缀，按 `.cc` 同类格式加载。 |
| `.c2s` | 使用的 CC 系列容器后缀，按 `.cc` 同类格式加载。 |
| `.c3s` | 使用的 CC 系列容器后缀，按 `.cc` 同类格式加载。 |

后缀用于识别原始游戏样本的容器形式；`.c2m`、`.c2s` 和 `.c3s` 不使用独立模拟内核。

- 文件说明：DingooPie Android Game Emulator
- 产品名称：丁果派 DingooPie Android
- Powered by：BL2CK Software
- 版权：Copyright (c) 2026 BL2CK

### 模拟内核方案

- APP 游戏使用独立 MIPS 运行时：自动模式使用 PPSSPP IR JIT，兼容或兼容性配置要求时使用内置 MIPS 解释器。
- CC 游戏使用独立 ARM32 运行时：自动模式优先使用 Dynarmic A32 JIT，兼容、需要解释器指令采样或 JIT 不可用时使用内置 ARM32 解释器；普通性能日志仍可保留 Dynarmic。
- 两类运行时各自处理 CPU 执行与 HLE，共享通用视频、输入、配置、存档和其他前端基础设施，再由 Android 平台层完成界面与系统集成。

### 快速使用
1. 安装 `DingooPie.apk` 并启动。
2. 在游戏库中添加游戏目录，或导入单个 `.app` / `.cc` / `.c2m` / `.c2s` / `.c3s` 文件。
3. 授予 Android 文件访问权限，等待扫描完成后点击游戏启动。
4. 游戏中按 Android 返回键打开暂停菜单，可即时存档、切换游戏、重启、进入选项或设置、退出应用，或返回游戏。
从可写目录导入的游戏会优先把存档保存在游戏旁边。单文件导入、只读目录或
目录授权失效时，模拟器会改用应用私有存档目录。移除游戏库条目不会删除原始游戏文件。

### 外部模拟器前端调用

DingooPie 支持由天马 G、Daijishō、Pegasus、自动化工具及其他能够发送 Android
Intent 的模拟器前端直接启动 `.app` / `.cc` / `.c2m` / `.c2s` / `.c3s` 游戏。前端应配置以下组件：

- 包名：`com.dingoopie.android`
- Activity：`com.dingoopie.android.DingooPieActivity`
- 推荐方式：使用 `android.intent.action.VIEW` 传入 `file://` 或 `content://` URI。
- 显式调用：使用 `com.dingoopie.android.action.LAUNCH_GAME`，并通过 `gamePath`、
  `path`、`rom`、`ROM`、`game`、`GAME`、`file`、`FILE`、`filename` 或 `fullPath`
  传入游戏路径。
- 自定义 URI：使用 `dingoopie://launch?path=<编码后的路径>`；查询参数也可使用
  `gamePath` 或 `rom`。
- `content://` URI 必须授予临时或持久读取权限。
- 游戏运行中收到新的外部启动请求时，会正常停止当前游戏并在同一 Activity 中启动新游戏。

RetroArch 通过 libretro 核心加载游戏，而 DingooPie 当前是独立 Android 应用，
不是 libretro 核心，因此标准 RetroArch 不能直接加载或调用 DingooPie。
只有能够额外发送上述 Android Intent 的定制版本或配套启动工具才能调用。

示例：

```powershell
adb shell am start -n com.dingoopie.android/.DingooPieActivity `
  -a android.intent.action.VIEW `
  -d "file:///storage/emulated/0/Games/demo.app"

adb shell am start -n com.dingoopie.android/.DingooPieActivity `
  -a com.dingoopie.android.action.LAUNCH_GAME `
  --es gamePath "/storage/emulated/0/Games/demo.cc"
```

配置第三方前端时，将示例路径替换为前端提供的游戏文件占位符。通过 Android
文档提供器访问文件时，应优先使用前端提供的 URI 占位符。

### 局域网文件管理

点击游戏库左侧的四方块系统工具图标，可依次选择切换单列/多列显示、刷新游戏列表和文件管理服务。只有选择“文件管理服务”才会启动服务；正常启动模拟器不会自动开启文件管理服务。
手机或模拟器与电脑连接同一网络后，在电脑浏览器中打开提示的 IPv4 地址即可访问。
地址中包含 6 位小写字母与数字令牌，服务优先显示 `192.168.*` 地址。

- 可访问模拟器私有目录和用户持续授权的目录。
- 授权目录可从根目录列表中移除；该操作只撤销模拟器访问权限，不会删除目录或文件。
- 支持多文件累计选择、上传、下载、重命名和删除。
- 点击文件夹名称进入目录；文件下载使用右侧下载按钮。
- 为避免误删，非空文件夹不能删除。
- 关闭地址提示窗口后服务继续在后台运行；选择“停止服务”才会关闭监听。

### 默认设置

| 项目 | 默认值 |
| --- | --- |
| 抗锯齿 | 关闭 |
| 滤镜 | 正常 |
| 亮度 | 100% |
| 对比度 | 100% |
| 伽马 | 100% |
| 饱和度 | 100% |
| 最小化时 | 自动暂停 |
| 屏幕方向 | 横屏 |
| 画面填充 | 保持宽高比 |
| 显示 FPS | 关闭 |
| 主音量 | 100% |
| 音频缓冲 | 2048 采样 |
| 音频缓冲延迟 | 自动 |
| 音频效果 | 关闭 |
| 数字降噪 | 高 |
| 禁用音频 | 关闭 |
| 禁用系统输入法 | 开启 |
| 显示虚拟按键 | 开启 |
| 虚拟按键大小 | 100% |
| 虚拟按键透明度 | 100% |
| 方向键类型 | 摇杆 |
| 手柄按键映射 | 默认映射 |
| CPU 执行模式 | 自动 |
| CPU 时钟 | 自动 |
| 游戏速度 | 自动 |
| 系统延迟比例 | 自动 |
| 金手指管理器 | 禁用金手指 |
| 游戏列表布局 | 单列 |
| 语言 | 中文 |

### 菜单与配置

- `游戏库`：添加、启动、移除及刷新游戏，可在系统工具中手动切换单列或多列显示；单列下方向键选择上一个或下一个游戏，多列下方向键按网格移动，`Enter` / `Space` 进入选中游戏，`Delete` 移除选中游戏，`Esc` 打开主菜单，`Insert` 添加游戏；方向键支持按住连续选择。
- `主菜单`：选项、设置、关于、退出应用和返回。
- `暂停菜单`：即时存档、切换游戏、重启游戏、选项、设置、退出应用和返回游戏。
- `选项`：视频、音频、输入、恢复默认设置和返回。
- `选项 > 视频`：抗锯齿、滤镜、亮度、对比度、伽马、饱和度、最小化时、屏幕方向、画面填充和显示 FPS。
- `选项 > 音频`：主音量、音频缓冲、音频缓冲延迟、音频效果、数字降噪和禁用音频。
- `选项 > 输入`：禁用系统输入法、显示虚拟按键、虚拟按键大小、虚拟按键透明度、方向键类型、手柄按键映射和手柄校准。
- `设置`：CPU 执行模式、CPU 时钟、游戏速度、系统延迟比例、金手指管理器、语言、恢复默认设置和返回。
- `关于`：版本、适用机型、游戏文件格式归属、作者主页、项目主页和软件信息。

设置会自动保存到应用私有目录中的 `DingooPie.ini`。

### 按键映射

触摸屏默认显示虚拟方向键、A/B/X/Y、START、SELECT 和肩键，并支持多点触控与组合按键。
支持 SDL GameController 兼容手柄，可在 `选项 > 输入 > 手柄按键映射` 中设置按键。

连接 USB 或蓝牙物理键盘时使用以下默认映射。表中的 Home 指物理键盘导航区的
Home 键，不是 Android 系统主页键：

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

### 金手指

金手指默认关闭，并按游戏格式优先加载同名文件：

```text
Game.app -> Game.app.cht
Game.cc  -> Game.cc.cht
```

格式专用文件不存在时才回退到 `Game.cht`，因此同名 APP 与 CC 游戏的金手指文件和
勾选状态不会互相覆盖。进入 `设置 > 金手指管理器` 后可启用金手指、选择功能、全部启用、
全部停用、应用或刷新。文件缺失或不适用于当前游戏时，模拟器会保持金手指不可用。

### 即时存档

每个游戏提供 15 个即时存档档位。存档保存在对应游戏存档目录中的
`savestates/<游戏名>.slotN.dps`，缩略图使用
`savestates/<游戏名>.slotN.thumb.bmp`。如果当前游戏阶段与保存时不同，
请先返回相同场景再读取。

### 构建

要求：Windows PowerShell 5.1 或 PowerShell 7、JDK 17、Android SDK Platform 35，
以及 Android NDK `26.3.11579264`。

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_settings_order.ps1
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
```

文档入口见 `docs/README.md`。发布构建与签名步骤见 `docs/BUILDING.md` 和
`docs/RELEASE_SIGNING.md`；测试矩阵、模拟器兼容性、音频、金手指、输入法和
存档自动化见 `docs/TESTING.md`。

签名发布版可使用以下命令生成并复制到桌面：

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1 `
    -Configuration Release -OutputDirectory "$HOME\Desktop"
```

发布前必须确认 APK 签名验证、文本格式检查和相关回归测试全部通过。

### 代码架构

原生代码按共享模拟核心与 Android 平台集成分层：

- `native/core/app/`：APP/MIPS 运行时、CPU 后端、HLE 和 APP 存档。
- `native/core/cc/`：CC/ARM32 运行时、解释器、Dynarmic 后端、HLE 和 CC 存档。
- `native/core/shared/`：格式选择、执行协调、访客服务、通用存档和诊断契约。
- `native/core/frontend/`：跨平台的视频、输入和其他 SDL 前端基础设施。
- `native/core/config/`：设置、兼容性配置和金手指。
- `native/android/`：Android 入口、前端外壳、菜单、游戏库、平台服务、诊断和兼容层。

完整架构和依赖边界见 `docs/ARCHITECTURE.md`。

## English

DingooPie Android is an Android emulator for `.app`, `.cc`, `.c2m`, `.c2s`, and
`.c3s` games made for the Dingoo A320, Gemei X760+, and Gemei A330 handhelds. The
game file formats belong to their original vendors. No game images are included;
use only files obtained legally.
APP games use the XBurst/MIPS architecture of the Ingenic JZ4732 SoC, while CC
games use the ARM11 architecture of the ChinaChip CC1800 SoC. They are executed
by the dedicated APP MIPS and CC ARM32 runtimes, respectively.

### Supported Game Sample Suffixes

| Suffix | Type And Handling |
| --- | --- |
| `.app` | Dingoo APP game package handled by the APP MIPS runtime. |
| `.cc` | CC-family game package handled by the CC ARM32 runtime. |
| `.c2m` | CC-family container suffix used by Gemei samples; loaded like `.cc`. |
| `.c2s` | CC-family container suffix used by Gemei samples; loaded like `.cc`. |
| `.c3s` | CC-family container suffix used by Gemei samples; loaded like `.cc`. |

The suffix identifies the original sample container. `.c2m`, `.c2s`, and `.c3s`
do not use separate emulation cores.

- File description: DingooPie Android Game Emulator
- Product name: DingooPie Android
- Powered by: BL2CK Software
- Copyright: Copyright (c) 2026 BL2CK

### Emulation Core Scheme

- APP games use a dedicated MIPS runtime. Automatic mode uses PPSSPP IR JIT; Compatibility or a compatibility profile uses the built-in MIPS interpreter.
- CC games use a dedicated ARM32 runtime. Automatic mode prefers Dynarmic A32 JIT; Compatibility, interpreter instruction sampling, or an unavailable JIT uses the built-in ARM32 interpreter. Normal performance logging can keep Dynarmic enabled.
- Each runtime owns CPU execution and HLE. Common video, input, configuration, save, and other frontend infrastructure is shared, then integrated with the UI and system through the Android platform layer.

### Quick Start

1. Install and launch `DingooPie.apk`.
2. Add a game folder or import one `.app` / `.cc` / `.c2m` / `.c2s` / `.c3s` file from the game library.
3. Grant Android file access and wait for the library scan to finish.
4. Tap a game to start it. Press Android Back during play to open the pause menu,
   then use instant saves, switch game, restart, options, settings, exit, or return.

Games imported from a writable folder keep saves beside the game. Single-file imports,
read-only locations, and expired grants use application-private save storage. Removing
an entry never deletes the game.

### External Emulator Frontends

DingooPie accepts `.app`, `.cc`, `.c2m`, `.c2s`, and `.c3s` launch requests from
Tianma G, Daijishō, Pegasus-based frontends, automation tools, and other Android
emulator frontends that can send Android intents.
Use package `com.dingoopie.android` and activity
`com.dingoopie.android.DingooPieActivity`.

- Preferred: send `android.intent.action.VIEW` with a `file://` or `content://` URI.
- Explicit launchers may send action `com.dingoopie.android.action.LAUNCH_GAME`
  with a path extra named `gamePath`, `path`, `rom`, `ROM`, `game`, `GAME`, `file`,
  `FILE`, `filename`, or `fullPath`.
- The URI form `dingoopie://launch?path=<encoded-path>` is also supported; its query
  key may be `path`, `gamePath`, or `rom`.
- Content URIs must include temporary or persistable read permission.
- A new request received while a game is running stops the current game cleanly and
  launches the requested APP or CC game in the same activity.

RetroArch loads games through libretro cores, while DingooPie is currently a standalone
Android application rather than a libretro core. Standard RetroArch therefore cannot
directly load or invoke DingooPie; only a custom build or companion launcher that emits
the Android intents above can do so.

Example commands:

```powershell
adb shell am start -n com.dingoopie.android/.DingooPieActivity `
  -a android.intent.action.VIEW `
  -d "file:///storage/emulated/0/Games/demo.app"

adb shell am start -n com.dingoopie.android/.DingooPieActivity `
  -a com.dingoopie.android.action.LAUNCH_GAME `
  --es gamePath "/storage/emulated/0/Games/demo.cc"
```

When configuring a third-party frontend, substitute its ROM placeholder for the
example path. Prefer its URI placeholder when Android storage access is provided
through a document provider.

### LAN File Management

Press the four-square System Tools icon on the left side of the game library. Its
items are ordered as layout switching, Refresh Game List, and File Manager Service.
Only choosing File Manager Service starts the service; launching the emulator does
not start it automatically.
Connect the Android device or emulator and the computer to the same network, then
open the displayed IPv4 address. Its URL includes a six-character lowercase letter
and digit token. Addresses in the `192.168.*` range are listed first.

- Access application-private files and directories with persisted user permission.
- Authorized folders can be removed from the root list; this only revokes emulator access and does not delete the folder or its files.
- Select multiple files cumulatively, upload, download, rename, and delete entries.
- Open folders by clicking their names; download files with the action button.
- Non-empty folders cannot be deleted, which prevents accidental recursive removal.
- Closing the address dialog keeps the service running; use `Stop Service` to stop it.

### Default Settings

| Option | Default |
| --- | --- |
| Anti-aliasing | Off |
| Filter | Normal |
| Brightness | 100% |
| Contrast | 100% |
| Gamma | 100% |
| Saturation | 100% |
| When minimized | Auto Pause |
| Screen orientation | Landscape |
| Screen fill | Keep aspect ratio |
| Show FPS | Off |
| Master volume | 100% |
| Audio buffer | 2048 samples |
| Audio buffer latency | Auto |
| Audio effect | Off |
| Digital noise reduction | High |
| Disable audio | Off |
| Disable system IME | On |
| Show virtual controls | On |
| Virtual control size | 100% |
| Virtual control opacity | 100% |
| D-pad type | Joystick |
| Controller mapping | Default mapping |
| CPU execution mode | Auto |
| CPU clock | Auto |
| Game speed | Auto |
| System delay scale | Auto |
| Cheat manager | Cheats disabled |
| Game library layout | Single column |
| Language | Chinese |

### Menu And Configuration

- `Game Library`: add, launch, remove, and refresh games, with a manual single-column or multi-column switch in System Tools. Directional keys select the previous or next game in single-column mode and move through the grid in multi-column mode. `Enter` / `Space` launches the selected game, `Delete` removes it, `Esc` opens the main menu, and `Insert` adds a game. Directional keys support hold-to-repeat selection.
- `Main Menu`: Options, Settings, About, Exit App, and Back.
- `Pause Menu`: instant saves, switch game, restart, options, settings, exit, and return to the game.
- `Options`: Video, Audio, Input, Restore Default Settings, and Back.
- `Options > Video`: Anti-aliasing, Filter, Brightness, Contrast, Gamma, Saturation, When Minimized, Screen Orientation, Screen Fill, and Show FPS.
- `Options > Audio`: master volume, buffer size, audio buffer latency, audio effect, digital noise reduction, and audio disable.
- `Options > Input`: Disable System IME, Show Virtual Controls, Virtual Control Size, Virtual Control Opacity, D-pad Type, Controller Mapping, and Controller Calibration.
- `Settings`: CPU Execution Mode, CPU Clock, Game Speed, System Delay Scale, Cheat Manager, Language, Restore Default Settings, and Back.
- `About`: version, supported devices, game file format ownership, author homepage, project homepage, and software information.

Settings are saved automatically in the application-private `DingooPie.ini`.

### Keyboard Mapping

The touch screen shows a virtual D-pad, A/B/X/Y, START, SELECT, and shoulder buttons
by default, with multi-touch holds and combinations.
SDL GameController-compatible devices can be remapped under
`Options > Input > Controller Mapping`.
USB and Bluetooth physical keyboards use the defaults below. Home means the
physical keyboard navigation key, not the Android system Home button.

| Keyboard | Dingoo control |
| --- | --- |
| Arrow keys / WASD | D-pad |
| L | A |
| K | B |
| I | X |
| J | Y |
| 1 / Q | SELECT |
| 0 / O | START |
| Left Shift | Left shoulder |
| Right Shift | Right shoulder |
| Backspace / Home | POWER |
| Android Back | Open the pause menu |

### Cheats

Cheats are disabled by default and prefer a matching format-specific file:

```text
Game.app -> Game.app.cht
Game.cc  -> Game.cc.cht
```

`Game.cht` is used only when the format-specific file is absent, so APP and CC
games with the same base name keep separate cheat files and selections. Under
`Settings > Cheat Manager`, cheats can be enabled, selected, enabled all, disabled
all, applied, or refreshed. Cheats remain unavailable when no compatible file exists.

### Instant Saves

Each game provides 15 instant save slots. State files use
`savestates/<game>.slotN.dps`, and previews use
`savestates/<game>.slotN.thumb.bmp` under the active game save directory. If the current
game phase differs from the saved phase, return to the same scene before loading.

### Build

Requires Windows PowerShell 5.1 or PowerShell 7, JDK 17, Android SDK Platform 35,
and Android NDK `26.3.11579264`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_settings_order.ps1
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
```

Start with `docs/README.md`. See `docs/BUILDING.md` and `docs/RELEASE_SIGNING.md`
for release builds and signing. See `docs/TESTING.md` for the test matrix, emulator
compatibility, audio, cheats, IME, and save-state automation.

Build a signed release and copy it to the desktop with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1 `
    -Configuration Release -OutputDirectory "$HOME\Desktop"
```

Before publishing, verify the APK signature, text format, and relevant regressions.

### Code Architecture

Native code is divided between the shared emulator core and Android platform integration:

- `native/core/app/`: APP/MIPS runtime, CPU backends, HLE, and APP save states.
- `native/core/cc/`: CC/ARM32 runtime, interpreter, Dynarmic backend, HLE, and CC save states.
- `native/core/shared/`: format selection, execution coordination, guest services, common save infrastructure, and diagnostic contracts.
- `native/core/frontend/`: cross-platform video, input, and other SDL frontend infrastructure.
- `native/core/config/`: settings, compatibility configuration, and cheats.
- `native/android/`: Android entry point, frontend shell, menus, game library, platform services, diagnostics, and compatibility shims.

See `docs/ARCHITECTURE.md` for the complete architecture and dependency boundaries.
