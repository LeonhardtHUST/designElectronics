<p align="center"><img src="docs/assets/video/555-blinker.gif" width="720" alt="designElectronics 仿真器中的 555 定时器闪烁 LED"></p>

# designElectronics

**免费、离线的面包板 / 原理图 / PCB 设计与仿真工具。** 支持桌面端、手机和平板浏览器。

> 本仓库是 [LeonhardtHUST/designElectronics](https://github.com/LeonhardtHUST/designElectronics)，fork 自[上游/原始项目](https://github.com/rORrEtboLt/designElectronics)。上游作者为 Nesara Hippla；代码遵循 MIT 许可，详见 [CREDITS.md](CREDITS.md) 与 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

- **面包板 → 原理图 → PCB**：三视图联动，可导出 Gerber。
- **实时仿真**：555、LED、逻辑器件、仪表以及 Arduino / ESP32 草图。
- **免费、离线、无需账号**：项目保存为可分享的 `.dez` 文件。

**[▶ 浏览器试用](https://LeonhardtHUST.github.io/designElectronics/app/)**（Pages 首次成功部署后可用） · [项目文档与截图](https://LeonhardtHUST.github.io/designElectronics/) · [中文介绍](https://LeonhardtHUST.github.io/designElectronics/zh/)

英文说明：[README.en.md](README.en.md)

## 下载

桌面构建可从本仓库下载： [macOS](https://github.com/LeonhardtHUST/designElectronics/releases/latest/download/designElectronics-macOS.zip) · [Windows x64](https://github.com/LeonhardtHUST/designElectronics/releases/latest/download/designElectronics-Windows-x64.zip) · [Linux x86_64](https://github.com/LeonhardtHUST/designElectronics/releases/latest/download/designElectronics-Linux-x86_64.tar.gz)。完整版本与校验信息见 [Releases](https://github.com/LeonhardtHUST/designElectronics/releases)。未签名构建在 macOS 首次打开需右键选择“打开”，Windows 需选择“更多信息 → 仍要运行”。网页版与移动端构建状态见 [docs/PAGES.md](docs/PAGES.md)（Pages 部署指南）。

## 构建与运行（桌面）

需要 CMake ≥ 3.20、C++17 编译器、SDL3 与 SDL3_ttf。详见 [docs/BUILDING.md](docs/BUILDING.md)。

```sh
# macOS
brew install cmake sdl3 sdl3_ttf
# Debian/Ubuntu
sudo apt install build-essential cmake libsdl3-dev libsdl3-ttf-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# macOS
open build/designElectronics.app
# Linux
./build/designElectronics
# Windows (PowerShell)
.\build\Release\designElectronics.exe
```

打开示例：`File ▸ Examples`，或运行 `designElectronics examples/LED_with_battery.dez`。iOS / Android 构建路径见 [docs/BUILDING.md](docs/BUILDING.md)。

### AI 助手（MCP）

```sh
claude mcp add designElectronics -- "$PWD/build/designElectronics.app/Contents/MacOS/designElectronics" --mcp
```

Claude Code、Codex、Gemini CLI、Cursor 等客户端可通过 MCP 搜索元件、布线、仿真、自动布线、DRC 和导出。详见 [docs/MCP.md](docs/MCP.md)。

### 自测

```sh
./build/designElectronics --selftest
```

## 使用与功能

1. 在面包板视图拖放元件并连接引脚；点击“仿真”运行电路。
2. 在原理图视图整理符号，把飞线转换为导线。
3. 在 PCB 视图设置板框、摆放封装、自动布线并运行 DRC。
4. 通过“文件 ▸ 导出”输出 Gerber、BOM、网表、SPICE、SVG、PNG 或视频/GIF。

支持多种常用元件、开发板、传感器、显示器件和自定义 `.dezpart` 模型；提供撤销/重做、复制粘贴、版本历史、可选 GitHub 同步、Arduino 草图解释器、网页嵌入和 AI 标签页。项目为 C++17 + SDL3 构建，详细功能列表见 [README.en.md](README.en.md)。

### 字号、引脚与快捷键

在 **视图 ▸ 设置 ▸ 外观 ▸ 界面字号** 选择 12、14（默认）、16、18 或 20，立即生效并保存为 `config.toml` 的 `appearance.font_size`；控件随文字调整，不改变电路尺寸。色环计算器保持字号，内容放不下时滚动。元件型号、阻值、容值等丝印使用等宽字体并随元件值同步更新。快捷键和鼠标按键可在设置中修改，面板会显示可用引脚与连接状态。

原理图中按 `Alt+W` 进入走线模式，依次单击起点和终点，引脚会自动吸附；右键或 `Esc` 退出。选中元件后轻按 `Space` 旋转，`Shift+Space` 反向旋转；按住 `Space` 加拖拽仍用于平移。自定义快捷键优先，`R` 旋转保留。

顶部 **工具 ▸ 仿真** 集中提供启动/停止、重新开始和仿真详情；底部仿真按钮仍可使用。

## 仿真边界

可选 ngspice 精度分析支持 `.tran`、`.ac`、`.dc` 等模式；内置实时仿真仍是默认路径。请阅读 [docs/NGSPICE.md](docs/NGSPICE.md) 了解安装、兼容性与已知边界；本项目不宣称数值或精度问题已全部修复。

## 参与贡献

欢迎提交本仓库的 [Issues](https://github.com/LeonhardtHUST/designElectronics/issues) 与 [Pull Requests](https://github.com/LeonhardtHUST/designElectronics/pulls)，见 [CONTRIBUTING.md](CONTRIBUTING.md) 与 [CLA.md](CLA.md)。

## 许可与致谢

代码采用 MIT，原作者 Nesara Hippla，见 [LICENSE](LICENSE)、[CREDITS.md](CREDITS.md) 和 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。字体及第三方组件的许可证随仓库分发；字体文件位于 `assets/fonts/`。

- Sarasa Gothic SC：Renzhi Li（Belleve Invis），SIL OFL 1.1。
- JetBrains Maple Mono：Space Time，基于 JetBrains Mono 与 Maple Mono，SIL OFL 1.1。
