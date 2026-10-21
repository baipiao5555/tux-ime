# Roadmap

## M0 — 工程与构建

- [x] Fork `fcitx5-windows`
- [x] 拉取 Fcitx5 / windows-cross 子模块
- [ ] 确认 GitHub Actions 能构建 Windows TSF DLL
- [ ] 输出独立的 TSF DLL 和注册/卸载脚本
- [ ] 在测试虚拟机里注册，不影响官方小狼毫

## M1 — TSF 最小输入

- [ ] 按键事件进入 TSF
- [ ] 创建 composition、写入测试文本、提交
- [ ] 验证 32/64 位应用、记事本、浏览器
- [ ] 崩溃/退出时清理 composition

## M2 — 接入 Fcitx5 输入上下文

- [ ] 在 DLL/服务进程内初始化 Fcitx5 Instance
- [ ] 加载 pinyin addon 和词典
- [ ] 把按键转为 Fcitx5 KeyEvent
- [ ] 把 Fcitx5 的 preedit/候选返回给 TSF
- [ ] 候选选择、翻页、提交

## M3 — 候选窗与输入体验

- [ ] 候选窗定位、DPI、多屏
- [ ] 横排/竖排、字体、颜色、圆角
- [ ] 分页、数字键选择、鼠标
- [ ] 中英文/标点/简繁状态

## M4 — 设置与插件

- [ ] 设置作为输入法内置窗口/模块
- [ ] 方案管理、快捷键、候选数、主题
- [ ] 剪贴板历史与同步
- [ ] 快捷短语、符号、Emoji
- [ ] 词典管理/导入

## M5 — 打包与发布

- [ ] 独立 TSF CLSID / Profile GUID
- [ ] 独立安装目录、用户目录、命名管道
- [ ] 安装器、卸载器、升级
- [ ] 测试矩阵：Win10/Win11、32/64 位、常见应用

## 约束

- 不碰用户正在使用的小狼毫/Weasel
- 不在日常机器上直接测试未验证的 TSF DLL
- 所有发布物保留上游授权和来源说明
