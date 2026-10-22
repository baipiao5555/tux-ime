# Tux IME（Tux 输入法）

> 独立 Windows 输入法项目，基于 Fcitx5 输入法核心和 Windows TSF 前端。
> 不复用小狼毫/Weasel 的输入法身份，可与官方小狼毫并存。

## 目标

做一个 Windows 上功能接近 Fcitx5 的输入法：

- 拼音/双拼等输入方案
- 候选窗、预编辑、分页、主题
- 插件式能力：剪贴板历史/同步、快捷短语、符号、Emoji
- 设置属于输入法本身，不是一个独立的设置中心
- 独立 TSF CLSID、独立安装目录、独立用户目录

## 当前状态

**0.21.0 测试版。** 安装器会自动注册 TSF 组件并把 “Tux IME” 加入
简体中文键盘列表，安装后无需手工改注册表。这个版本还不是正式版；
正式版要求见下方“距离正式版还缺什么”。

已经完成：

- Fcitx5 5.1.22 核心、libime 1.1.17、fcitx5-chinese-addons 5.1.15 的 Windows 构建
- 拼音引擎、用户词典、模糊拼音配置（Fcitx5 addon 配置）
- 多输入法：拼音、双拼、五笔、五笔拼音、仓颉、二笔、自然码、冰蟾、电报码、晚风；可在托盘和设置里切换
- `tuxime-server.exe`：嵌入 Fcitx5，提供命名管道服务；并发启动时只保留一个服务进程
- `tuxime-x86_64.dll`：独立 TSF CLSID/Profile，支持组合文本、提交、候选选择和回车/退格/ESC
- 候选窗：GDI+ 分层窗口，跟随光标，数字键选择，支持鼠标点选
- 服务端与 TSF 前端通过按用户 SID 隔离的命名管道通信
- 托盘菜单和内置设置窗口：默认输入法、每页候选数、模糊拼音、剪贴板同步
- 中文标点、全角/半角、简体/繁体切换（托盘、设置窗口和快捷键）
- 快捷短语/Emoji：输入 `;` 后继续输入关键词
- Unicode 输入：`Ctrl+Alt+Shift+U`
- 候选窗显示当前输入法、标点、全角、简繁状态和页码
- 候选窗通过 TSF `GetTextExt` 定位，兼容 Chromium/Electron 等没有原生光标的应用
- 候选窗外观：字号、深色模式、横向/纵向候选列表、自定义背景/文字/选中/边框/状态栏颜色、状态栏显示（设置 → 外观）
- 剪贴板文本、图片和单个文件同步：SyncClipboard HTTP 协议、WebDAV 图片目录自动创建、DPAPI 保存密码、URL 主机和解析地址校验（拒绝本机/环回/私有/保留地址，不跟随重定向）；兼容服务器返回 GBK/ANSI 文本
- 剪贴板历史：自动记录最近 50 条文本，设置里双击复制、可清空
- 拼音候选项开关：英文候选、符号、拆字、扩展 B 区、预测、完整拼音预编辑
- 自定义短语：编码 + 文本，保存后立即生效（`%APPDATA%\Fcitx5\pinyin\customphrase`），支持导入/导出 TSV
- 拼音用户词典导入/导出：文本格式，导入后自动备份并重启输入法
- 双拼方案图形化选择：自然码、微软、紫光、智能 ABC、中文之星、拼音加加、小鹤、国标
- 码表输入法基础选项：五笔/仓颉/二笔等的每页候选数、编码提示、全角字符，支持保存并重启
- NSIS 安装器自动注册/注销语言配置、卸载器和开始菜单“Tux IME 设置”快捷方式
- 关于页面支持检查 GitHub 更新、下载安装包并启动安装程序
- 服务端日志写入 `%APPDATA%\TuxIME\tuxime.log`，关于页面可打开日志目录
- 诊断页面：检查 64/32 位 TSF 注册、语言列表、同步配置、词典数据和日志

尚未完成：

- ARM64（32 位程序已支持，需运行在 64 位 Windows 上）
- 更多主题预设（目前支持自定义颜色和主题文件导入导出）
- 自定义码表导入和键位编辑（基础候选数/提示已支持）
- 云拼音

## 距离正式版还缺什么

正式版不会只改版本号。需要完成并验证：

- Windows 10/11、常见应用（记事本、Office、Chrome/Edge、微信、VSCode）测试矩阵
- ARM64 支持
- 自定义码表导入、长时间输入和高负载稳定性测试
- 安装/卸载异常恢复
- 设置界面的更多主题预设

## 技术路线

- 输入引擎：Fcitx5 + libime + fcitx5-chinese-addons
- Windows 接入：TSF text service
- 进程模型：TSF DLL 通过命名管道连接常驻的 `tuxime-server.exe`，引擎和词典只在服务端加载
- UI：GDI+ 候选窗；托盘和设置窗口在服务端进程内

## 构建

依赖：Windows 10/11 x64、Visual Studio 2022 Build Tools（C++ 桌面开发 + ATL）、CMake、Ninja、MSYS2 clang64（clang、CMake、Ninja、pkgconf、gettext、boost、libuv、zstd）。

推荐直接运行构建脚本（会自动应用补丁、构建全部依赖、组装 `dist/TuxIME-final`）：

```powershell
git submodule update --init --recursive
.\scripts\build-all.ps1
```

脚本里的 TSF 步骤需要在 Visual Studio 开发者命令行中运行（需要 `cl.exe`）。也可以按下面的手动步骤构建：

```powershell
# 1. 初始化子模块（含 libime 的 kenlm 子模块）
git submodule update --init --recursive

# 2. 应用 Windows 构建补丁
git -C libime apply ../patches/libime-windows.patch
git -C fcitx5-chinese-addons apply ../patches/fcitx5-chinese-addons-windows.patch

# 3. 在 MSYS2 clang64 环境下构建并安装核心依赖
$env:MSYS2_ROOT = "C:/msys64"
$env:PATH = "C:/msys64/clang64/bin;$env:PATH"
$env:PKG_CONFIG_PATH = "C:/msys64/clang64/lib/pkgconfig"

cmake -B build/core -G Ninja -S . -DCMAKE_BUILD_TYPE=Release -DARCH=AMD64 `
  -DCMAKE_INSTALL_PREFIX="$PWD/prefix"
cmake --build build/core
cmake --install build/core

# libime / fcitx5-chinese-addons 的配置参数见 win32/CMakeLists.txt 和 CI
# 4. 构建 TSF DLL（在 Visual Studio 开发者命令行里）
cmake -B build/win32 -G Ninja -S win32 -DCMAKE_BUILD_TYPE=Release
cmake --build build/win32
```

构建产物布局（运行时期望）：

```
TuxIME/
  bin/tuxime-server.exe
  bin/tuxime-x86_64.dll
  bin/*.dll
  lib/fcitx5/*.dll
  lib/libime/*
  data/fcitx5/*
  data/libime/*
```

## 安装/测试

推荐使用 Release 里的 `TuxIME-Setup-*.exe`。安装器会：

1. 安装到 `C:\Program Files\TuxIME`（需要管理员权限）。
2. 注册 64 位和 32 位 TSF 组件，并把 “Tux IME” 加入简体中文键盘列表。
3. 把 Tux IME 设为默认中文输入法，同时保留系统里已有输入法。
4. 创建开始菜单 “Tux IME 设置” 快捷方式。

安装完成后直接打开任何支持 TSF 的程序即可输入；托盘图标右键可打开设置、
立即同步剪贴板或退出服务。卸载时安装器会从语言列表和注册表中移除 Tux IME。

如果使用绿色版（不运行安装器），把 `dist/TuxIME-final` 放到测试目录，以管理员身份运行
`regsvr32 /s <路径>\bin\tuxime-x86_64.dll` 和
`C:\Windows\SysWOW64\regsvr32.exe /s <路径>\bin\tuxime-x86.dll`，然后运行
`installer/setup-language.ps1` 完成语言列表注册。卸载时先运行
`installer/setup-language.ps1 -Remove`，再反注册两个 DLL。

建议首次安装前备份当前输入法设置。卸载前请关闭正在使用输入法的程序；被占用的
TSF DLL 会在下次重启时自动删除。

## 上游与授权

- [fcitx5](https://github.com/fcitx/fcitx5)：LGPL-2.1-or-later
- [libime](https://github.com/fcitx/libime)：LGPL-2.1-or-later
- [fcitx5-chinese-addons](https://github.com/fcitx/fcitx5-chinese-addons)：LGPL-2.1-or-later
- [fcitx5-windows](https://github.com/fcitx-contrib/fcitx5-windows)：TSF 骨架来源
- 本仓库保留上游 LICENSE 和版权声明

## 非目标

- 不伪装成 Fcitx 官方 Windows 版
- 不复用官方小狼毫的 CLSID、DLL 名、命名管道或注册表键
