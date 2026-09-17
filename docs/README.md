# DingooPie Android Documentation

## 中文

本目录保存 DingooPie Android 的开发、构建、测试和发行维护文档。面向用户的
安装、外部模拟器前端调用、默认设置、游戏库布局、系统工具、局域网文件管理、按键、金手指和即时存档
说明见仓库根目录的 `README.md`。

### 文档索引

- `ARCHITECTURE.md`：APP / CC 运行时边界、CC 包布局、共享服务、兼容性规则和配置顺序。
- `BUILDING.md`：依赖准备、Debug / Release 构建、签名参数和输出路径。
- `TESTING.md`：APK、运行时、界面、输入、金手指、即时存档、线程、资源、菜单和兼容性测试。
- `RELEASE_SIGNING.md`：正式签名证书、私有文件位置和发行包验证方法。
- `a320_dingoo_ib_chinese.pdf`：丁果 A320 中文使用说明书。

### 推荐维护流程

1. 阅读 `ARCHITECTURE.md`，确认修改所属层和 APP / CC 边界。
2. 按 `BUILDING.md` 构建 Debug APK。
3. 运行与改动相关的专项脚本，再执行 `scripts/test_android.ps1`。
4. 运行 Gradle Lint、`scripts/test_settings_order.ps1` 和 `scripts/check_text_format.ps1`。
5. 发布前按 `RELEASE_SIGNING.md` 验证签名、证书指纹和最终 APK。

仓库文本文件统一使用 UTF-8 无 BOM、CRLF 换行，并以 CRLF 结尾。

## English

This directory contains the maintained development, build, test, and release
documentation for DingooPie Android. See the repository `README.md` for the
user-facing installation, external emulator frontend integration, defaults, game
library layouts, System Tools, LAN file management, controls, cheats, and instant-save guide.

### Documentation Index

- `ARCHITECTURE.md`: APP / CC runtime boundaries, CC package layouts, shared services, compatibility rules, and configuration order.
- `BUILDING.md`: dependency setup, Debug / Release builds, signing inputs, and output paths.
- `TESTING.md`: APK, runtime, UI, input, cheat, instant-save, thread, resource, menu, and compatibility validation.
- `RELEASE_SIGNING.md`: official certificate identity, private local files, and release verification.
- `a320_dingoo_ib_chinese.pdf`: Chinese Dingoo A320 user manual.

### Recommended Maintenance Flow

1. Read `ARCHITECTURE.md` and identify the owning layer and APP / CC boundary.
2. Build the Debug APK by following `BUILDING.md`.
3. Run focused regression scripts, then run `scripts/test_android.ps1`.
4. Run Gradle Lint, `scripts/test_settings_order.ps1`, and `scripts/check_text_format.ps1`.
5. Verify release signing, the certificate fingerprint, and the final APK before distribution.

Repository text files use UTF-8 without BOM, CRLF line endings, and a final CRLF.
