# Tux IME（Tux 输入法）

> 工作名，独立 Windows 输入法项目，基于 Fcitx5 的输入法核心和 Windows TSF 前端。
> 不是小狼毫改版，也不会复用 Weasel 的输入法身份。

## 目标

做一个 Windows 上功能接近 Fcitx5 的输入法：

- 拼音/双拼等输入方案
- 候选窗、预编辑、分页、主题
- 插件式能力：剪贴板历史/同步、快捷短语、符号、Emoji
- 设置属于输入法本身，不是一个独立的设置中心
- 独立 TSF CLSID、独立安装目录、独立用户目录，可与官方小狼毫并存

## 当前状态

**早期脚手架，不可日常使用。**

- 上游 `fcitx5-windows` 的 Windows TSF 骨架已拉入
- 目前 TSF 只做注册/组合文本的验证，尚未接入 Fcitx5 的输入上下文和候选窗
- 不会安装到当前用户的日常输入法；开发/测试应在独立虚拟机或测试账号里进行

## 技术路线

- 输入引擎：Fcitx5（pinyin、table、clipboard、quickphrase 等 addon）
- Windows 接入：TSF text service
- UI：优先复用/移植 Fcitx5 的 UI 组件；没有可用的 Windows UI 时再补最小候选窗
- 设置：作为输入法内置模块，不另做独立设置中心

## 构建

上游工程的构建方式见 `CMakeLists.txt` 和 `.github/workflows/ci.yml`。Windows TSF 部分在 `win32/`，需要 CMake、Ninja 和 Clang（MSYS2 clang64）。

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 上游与授权

- [fcitx5](https://github.com/fcitx/fcitx5)：LGPL-2.1-or-later
- [weasel](https://github.com/rime/weasel)：GPL-3.0-only（上游 fcitx5-windows 的 TSF 参考来源）
- 本仓库保留上游 LICENSE 和版权声明

## 非目标

- 不伪装成 Fcitx 官方 Windows 版
- 不复用官方小狼毫的 CLSID、DLL 名、命名管道或注册表键
- 不在用户日常输入法上直接实验
