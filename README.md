# DingooPie Android
## 中文
丁果派 DingooPie Android 是 Android 平台的 `.app` / `.cc` 游戏模拟器，用于运行
丁果 A320、歌美 X760+ 和歌美 A330 掌机游戏。`.app` 与 `.cc` 格式文件归丁果科技所有；
本项目和发布包不包含游戏样本，请使用自行合法取得的文件。
其中，APP 游戏使用 Ingenic JZ4732 SoC 的 XBurst/MIPS 架构，CC 游戏使用 ChinaChip
CC1800 SoC 的 ARM11 架构。模拟器分别通过 APP MIPS 运行时和 CC ARM32 运行时执行这两类游戏。

- 文件说明：DingooPie Android Game Emulator
- 产品名称：丁果派 DingooPie Android
- Powered by：BL2CK Software
- 版权：Copyright (c) 2026 BL2CK

### 快速使用
1. 安装 `DingooPie.apk` 并启动。
2. 在游戏库中添加游戏目录，或导入单个 `.app` / `.cc` 文件。
3. 授予 Android 文件访问权限，等待扫描完成后点击游戏启动。
4. 游戏中按 Android 返回键打开暂停菜单，可恢复、重启、切换游戏或调整设置。
从可写目录导入的游戏会优先把存档保存在游戏旁边。单文件导入、只读目录或
目录授权失效时，模拟器会改用应用私有存档目录。移除游戏库条目不会删除原始游戏文件。
### 局域网文件管理

点击游戏库左侧的文件夹按钮才会启动文件管理服务；正常启动模拟器不会自动开启。
手机或模拟器与电脑连接同一网络后，在电脑浏览器中打开提示的 IPv4 地址即可访问。
地址中包含 6 位小写字母与数字令牌，服务优先显示 `192.168.*` 地址。

- 可访问模拟器私有目录和用户持续授权的目录。
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
| 音频效果 | 关闭 |
| 数字降噪 | 高 |
| 禁用音频 | 关闭 |
| 禁用系统输入法 | 开启 |
| 显示虚拟按键 | 开启 |
| 虚拟按键大小 | 100% |
| 方向键类型 | 摇杆 |
| 手柄按键映射 | 默认映射 |
| CPU 执行模式 | 自动 |
| CPU 时钟 | 自动 |
| 游戏速度 | 自动 |
| 系统延迟比例 | 自动 |
| 金手指管理器 | 禁用金手指 |
| 语言 | 中文 |

### 菜单与配置

- `游戏库`：添加游戏目录、导入单个游戏、启动游戏、移除条目和刷新扫描结果。
- `暂停菜单`：即时存档、切换游戏、重启游戏、选项、设置、退出应用和返回游戏。
- `选项 > 视频`：抗锯齿、滤镜、亮度、对比度、伽马、饱和度、最小化时、屏幕方向、画面填充和显示 FPS。
- `选项 > 音频`：主音量、音频缓冲、音频效果、数字降噪和禁用音频。
- `选项 > 输入`：禁用系统输入法、显示虚拟按键、虚拟按键大小、方向键类型和手柄按键映射。
- `设置`：CPU 执行模式、CPU 时钟、游戏速度、系统延迟比例、金手指管理器、语言和恢复默认设置。
- `关于`：版本、支持格式和软件信息。

设置会自动保存到应用私有目录中的 `DingooPie.ini`。

### 按键映射

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

### ????

?????? `native/core/`?????????

- `app/`?APP/MIPS ????CPU????HLE ? APP ???
- `cc/`?CC/ARM32 ???????? Dynarmic????HLE ? CC ???
- `shared/`??????????????????????????????????????
- `frontend/`?SDL shell?video?input?audio?menu ? library ?????
- `config/`?`settings/`?`compatibility/` ? `cheats/` ?????

???????????? `docs/ARCHITECTURE.md`?

## English

DingooPie Android is an Android emulator for Dingoo `.app` and `.cc` games made for
the Dingoo A320, Gemei X760+, and Gemei A330 handhelds. The package formats belong
to Dingoo Technology. No game images are included; use only files obtained legally.
APP games use the XBurst/MIPS architecture of the Ingenic JZ4732 SoC, while CC
games use the ARM11 architecture of the ChinaChip CC1800 SoC. They are executed
by the dedicated APP MIPS and CC ARM32 runtimes, respectively.

- File description: DingooPie Android Game Emulator
- Product name: DingooPie Android
- Powered by: BL2CK Software
- Copyright: Copyright (c) 2026 BL2CK

### Quick Start

1. Install and launch `DingooPie.apk`.
2. Add a game folder or import one `.app` / `.cc` file from the game library.
3. Grant Android file access and wait for the library scan to finish.
4. Tap a game to start it. Press Android Back during play to open the pause menu.

Games imported from a writable folder keep saves beside the game. Single-file imports,
read-only locations, and expired grants use application-private save storage. Removing
an entry never deletes the game.

### LAN File Management

The file service starts only after the folder button on the left side of the game
library is pressed; launching the emulator does not start it automatically. Connect
the Android device or emulator and the computer to the same network, then open the
displayed IPv4 address. Its URL includes a six-character lowercase letter and digit
token. Addresses in the `192.168.*` range are listed first.

- Access application-private files and directories with persisted user permission.
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
| Audio effect | Off |
| Digital noise reduction | High |
| Disable audio | Off |
| Disable system IME | On |
| Show virtual controls | On |
| Virtual control size | 100% |
| D-pad type | Joystick |
| Controller mapping | Default mapping |
| CPU execution mode | Auto |
| CPU clock | Auto |
| Game speed | Auto |
| System delay scale | Auto |
| Cheat manager | Cheats disabled |
| Language | Chinese |

### Menu And Configuration

- `Game Library`: add folders, import a game, launch games, remove entries, and refresh scans.
- `Pause Menu`: instant saves, switch game, restart, options, settings, exit, and return to the game.
- `Options > Video`: Anti-aliasing, Filter, Brightness, Contrast, Gamma, Saturation, When Minimized, Screen Orientation, Screen Fill, and Show FPS.
- `Options > Audio`: master volume, buffer size, audio effect, digital noise reduction, and audio disable.
- `Options > Input`: Disable System IME, Show Virtual Controls, Virtual Control Size, D-pad Type, and Controller Mapping.
- `Settings`: CPU Execution Mode, CPU Clock, Game Speed, System Delay Scale, Cheat Manager, Language, and Restore Default Settings.
- `About`: version, supported formats, and software information.

Settings are saved automatically in the application-private `DingooPie.ini`.

### Keyboard Mapping

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

### Cheats

Cheats are disabled by default. `Game.app.cht` and `Game.cc.cht` are preferred;
`Game.cht` is used only when the format-specific file is absent. Use
`Settings > Cheat Manager` to select, apply, disable, or refresh cheat features.

### Instant Saves

Each game provides 15 instant save slots. State files use
`savestates/<game>.slotN.dps`, and previews use
`savestates/<game>.slotN.thumb.bmp` under the active game save directory. If the current
game phase differs from the saved phase, return to the same scene before loading.

### Build

Requires PowerShell, JDK 17, Android SDK Platform 35, and NDK `26.3.11579264`.

```powershell
powershell -ExecutionPolicy Bypass -File scripts/bootstrap_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_android.ps1
powershell -ExecutionPolicy Bypass -File scripts/test_settings_order.ps1
powershell -ExecutionPolicy Bypass -File scripts/check_text_format.ps1
```

Start with `docs/README.md`. See `docs/BUILDING.md`, `docs/TESTING.md`,
`docs/ARCHITECTURE.md`, and `docs/RELEASE_SIGNING.md` for maintained details.

Build a signed release and copy it to the desktop with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/build_android.ps1 `
    -Configuration Release -OutputDirectory "$HOME\Desktop"
```

Before publishing, verify the APK signature, text format, and relevant regressions.
