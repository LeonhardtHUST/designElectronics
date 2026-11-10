<p align="center"><img src="docs/assets/video/555-blinker.gif" width="720" alt="在 designElectronics 仿真器里，555 定时器在虚拟面包板上让 LED 闪烁"></p>

# designElectronics

**一个跑在手机上的免费面包板 / 原理图 / PCB 设计工具。** 浏览器和笔记本电脑上同样能跑。

- **面包板 → 原理图 → PCB。** 三个视图始终联动，导出的 Gerber 可直接交给任何 PCB 厂。
- **实时仿真。** 555 会闪、LED 会亮、万用表有读数；Arduino 与 ESP32 草图逐行执行。
- **免费、离线、无需账号。** 一个设计就是一个 `.dez` 文件，随便发。MIT 许可。

**[▶ 在浏览器里试用](https://rorretbolt.github.io/designElectronics/app/)**（手机可用） · [文档与截图](https://rorretbolt.github.io/designElectronics/) · [中文介绍](https://rorretbolt.github.io/designElectronics/zh/)

English: [README.md](README.md)

## 下载

| macOS 11+（Apple Silicon 与 Intel） | Windows 10/11（64 位） | Linux x86_64 |
| --- | --- | --- |
| [designElectronics-macOS.zip](https://github.com/rORrEtboLt/designElectronics/releases/latest/download/designElectronics-macOS.zip) | [designElectronics-Windows-x64.zip](https://github.com/rORrEtboLt/designElectronics/releases/latest/download/designElectronics-Windows-x64.zip) | [designElectronics-Linux-x86_64.tar.gz](https://github.com/rORrEtboLt/designElectronics/releases/latest/download/designElectronics-Linux-x86_64.tar.gz) |

目前的构建还没有代码签名：macOS 首次打开请右键点应用 ▸ **打开**；Windows 请选 **更多信息 ▸ 仍要运行**。详见 [发布说明](https://github.com/rORrEtboLt/designElectronics/releases)。

## 构建与运行（桌面）

需要 CMake 3.20+ 和 C++17 编译器。依赖只有 SDL3 与 SDL3_ttf。

```bash
# macOS
brew install cmake sdl3 sdl3_ttf

# Debian/Ubuntu（24.10+）—— 或者自己编译 SDL，见 docs/BUILDING.md
sudo apt install build-essential cmake libsdl3-dev libsdl3-ttf-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# macOS
./build/designElectronics.app/Contents/MacOS/designElectronics

# Linux / Windows
./build/designElectronics
```

### 给 AI 助手用

应用同时是一个 MCP 服务器，Claude Code、Codex、Cursor 等可以读取并修改当前设计：

```bash
# Claude Code
claude mcp add designElectronics -- /path/to/designElectronics --mcp
```

### 自测

```bash
./build/designElectronics --selftest      # 无窗口跑完模型、布线、仿真、导出等测试
```

## 界面语言

内置英文与简体中文，在 **视图 ▸ 语言** 里切换，选择会被记住（存在偏好目录的 `settings.json`）。
`--lang zh-CN` 可以为单次运行指定语言（截图和测试用）。

翻译是编译进二进制的 JSON 语言包（`i18n/*.json`）。想改译文或加新语言：

```bash
# 导出完整模板（含界面文案和零件库的全部 key）
./designElectronics --export-i18n ./i18n

# 改完放回程序旁的 i18n/ 目录（或偏好目录的 i18n/）即可覆盖，无需重新编译
# 提交前自检：缺 key、占位符数量不一致、没被引用的 key 都会报出来
python3 tools/i18n_check.py --strict
```

新增一门语言只需要在 `i18n/` 里放一个 `xx.json`（含 `_meta.code` / `_meta.native`），
语言菜单会自动出现，不需要改代码。

## 使用方式

打开应用就是一个新设计：左边是零件箱，中间是画布，右边是检查器；底部工具栏可切换视图、仿真、导出。

1. 把元件拖到面包板上（引脚会自动吸附到孔）。
2. 从孔或引脚拖拽连线；切到原理图会看到飞线，点一下即可变成导线。
3. 切到 PCB 摆放封装，点 **自动布线** 或手动画走线，跑一次 DRC。
4. 点 **仿真** 让它跑起来；板子上可以写 Arduino 草图。
5. **文件 ▸ 导出** 拿到 Gerber、BOM、网表或 SPICE。

## 功能一览

| 领域 | 内容 |
| --- | --- |
| 三视图 | 面包板、原理图、PCB 联动，全部可撤销 |
| 面包板 | 孔位吸附、已用孔高亮、彩色导线 |
| 原理图 | 飞线转导线、直角整齐走线、网络标签 |
| PCB | 双层 A* 自动布线、DRC、丝印、过孔、安装孔 |
| 仿真 | 改进节点法求解，LED / 万用表 / 电机 / 蜂鸣器实时反馈 |
| 编程 | Arduino 子集解释器逐行执行；桌面版可经 arduino-cli 烧到真板 |
| 元件 | 内置元件库（电阻、LED、电容、三极管、开发板、传感器、显示器件…）与常见 IC 目录，支持自定义 `.dezpart` |
| 导出 | Gerber RS-274X、Excellon、SVG、PNG、BOM、网表、SPICE、MP4/GIF |
| 版本与同步 | 本地版本历史 + GitHub 私有仓库同步 |
| 分享 | 设计打包进链接、`<iframe>` 嵌入、网页版 |
| AI | MCP 服务器 + 内置 AI 标签页 |
| 语言 | 英文 / 简体中文，可扩展 |

## 项目结构

主要源文件都在 `src/`（`ui.*` 是自研的即时模式 UI 工具包，`gfx.*` 负责 2D 与文字渲染）。
翻译在 `i18n/`，元件库数据在 `parts/`，打包元数据在 `platform/`。

## 参与贡献

欢迎提交 issue 和 PR，细节见 [CONTRIBUTING.md](CONTRIBUTING.md)。首次贡献需要签署 [CLA.md](CLA.md)。
翻译类 PR 只需要改 `i18n/*.json`，不必动代码。

## 许可

MIT，见 [LICENSE](LICENSE)。

### 字体

- **Sarasa Gothic SC** © 2015-2025 Renzhi Li（be5invis），SIL Open Font License 1.1
- **JetBrains Maple Mono** © 2025 Space Time，基于 JetBrains Mono 与 Maple Mono，SIL Open Font License 1.1

许可证原文随程序分发，在 `assets/fonts/` 下。
