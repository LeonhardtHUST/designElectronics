免费开源的面包板、原理图与 PCB 设计工具，支持实时仿真、Gerber 导出、版本历史和 MCP。

本仓库为 [LeonhardtHUST/designElectronics](https://github.com/LeonhardtHUST/designElectronics)，fork 自 [rORrEtboLt/designElectronics](https://github.com/rORrEtboLt/designElectronics)，保留原作者 Nesara Hippla 的 MIT 版权及第三方声明。

📖 **中文站点：** https://leonhardthust.github.io/designElectronics/ · 🌐 **网页编辑器：** https://leonhardthust.github.io/designElectronics/app/

## 0.8.6 更新

- **LED 按颜色导出。** 之前 SPICE 网表里所有 LED 共用同一个二极管模型（约 2.07 V），红/绿/蓝的压降差异在 ngspice 里全部丢失。现在按实时仿真器的颜色压降（红 1.8 V、橙 2.0 V、黄 2.1 V、绿 2.2 V、蓝/白 3.0 V）与 15 Ω 串联电阻，为每种颜色生成独立的二极管参数，导出的工作点与屏幕上看到的一致。
- **光敏导出为光敏电阻。** 以前固定写成 1 MΩ，与“Light”滑块无关；现在按 `1 MΩ × 10^(-3·light/100)` 导出，与仿真器同一条公式。
- **对拍表清零 BAD。** `--spice-xcheck` 的十三个电路现在十二个 `OK`、一个 `WARN`（二极管钳位相差 3.17%：固定 0.7 V 与指数结的模型形状差异）、零个 `BAD`。`tools/check_xcheck.py` 因此去掉了“已知 BAD”白名单，任何新的 `BAD` 都会让 CI 失败；`docs/SPICE_XCHECK.md` 已按新结果重新生成。

### 已知限制

二极管仍按固定 0.7 V 建模，与 ngspice 的指数结相差约 3%，对拍中标记为 `WARN` 而非 `BAD`。`--spice-xcheck` 需要本机 libngspice，CI 只覆盖 Linux；下载包本身不带 ngspice 库。

## 0.8.5 更新

- **SPICE 对拍终于给出真实数值。** `--spice-xcheck` 之前把 ngspice 的每个向量都读成时间轴：`ngGet_Vec_Info` 返回的是复用的结构体，指针存到循环之后再取已经失效，于是几个毫不相干的电路都显示同一个 0.5 V。现在每个向量在查询时立即拷贝，报告反映真实节点电压，`docs/SPICE_XCHECK.md` 已按新结果重新生成。
- **7805 的 SPICE 模型重新限压。** 导出的 B 源只做了“输入减 2 V”，没有按额定值封顶，9 V 输入会得到 7 V；现在用 `min(max(...))` 限到 5 V，与实时仿真器一致。
- **对拍电路本身修正。** 旧 fixture 在连续 `addPart()` 之间持有失效引用、导线端点写死导致所有引脚并成一个网、还把接地端当成观测点；现在按引脚 id 接线、同步导线端点，并为三极管/MOS/稳压器搭出真实电路。毫伏级节点改用绝对误差判定。
- **CI 增加两道门禁。** Linux 任务安装 `libngspice0` 后运行 `--spice-xcheck`，由新增的 `tools/check_xcheck.py` 检查（不允许 error 行、不允许所有向量值相同、BAD 只允许已知的模型差异）；0.8.4 新增的 `tests/mcp_attach_test.py` 也接入了 macOS/Linux 任务。

### 已知限制

LED 与光敏器件的 SPICE 导出仍是通用模型（LED 不分颜色、光敏用固定电阻），对拍表里这三行仍标记为 `BAD`，报告与检查器都按“已知模型差异”放行，模型标定留待后续版本。`--spice-xcheck` 需要本机有 libngspice，CI 只覆盖 Linux。

## 0.8.4 更新

- **MCP 现在连的是你打开的设计。** `designElectronics --mcp` 启动时先找正在运行的窗口：应用在有设计时把本地桥的端口和一次性令牌发布到用户配置目录，客户端读取记录、握手校验（协议号、应用名、进程号）后把工具调用转发过去，`get_design`、`add_part`、`simulate` 立刻作用在屏幕上那份设计里。上一版按“复制 MCP 配置”接上的 Claude Desktop、Cursor、WorkBuddy 等客户端看到的是一份空白文档，本版修复。
- **多窗口与退出都收得住。** 同时开多个窗口时附加最近使用的那个；窗口关闭时记录随之删除，不会留下指向空端口的旧记录；欢迎页还没有设计时不发布，避免把空文档当成你的工程。
- **独立模式不再冒充你的文档。** 没有窗口可连时服务器仍使用自己的内存设计，但 `get_design` 会返回 `"standalone": true` 并说明原因；私有设计一旦被改动就锁定，不会在中途被悄悄换成另一份设计，附加后掉线则明确报错而不是退回去。
- **两个新开关。** `--mcp --standalone` 永不连接窗口；`--mcp --attach` 没有窗口就拒绝启动，适合脚本和 CI。
- **文档与测试。** `docs/MCP.md` 新增“连接到已打开的应用”一节（各平台记录路径、行为边界、`DE_LIVE_DIR` 测试用变量）；新增 `tests/mcp_attach_test.py`，覆盖附加、独立、陈旧记录与 `--attach` 四种情况；README 与站点同步说明。

### 已知限制

欢迎页阶段（尚未打开或新建设计）不发布记录，此时启动的客户端仍工作在独立模式；同一会话内如果先修改了独立设计，之后打开的窗口不会中途接管。多窗口时按最近使用时间选择，不能手动指定。

## 0.8.3 更新

- **限宽提示框。** 元件长说明自动换行，浮层保持在窗口内，不再横跨屏幕；极小窗口放不下时以省略号收尾，检查器保留完整说明。
- **工具菜单中的仿真。** 启动/停止、重新开始和仿真详情集中到“工具 → 仿真”，底部快捷按钮保留；重启清理仿真临时状态，不修改项目或撤销历史。
- **原理图型号去重。** 符号已有型号时，外部仅显示 `U1`、`U2` 等引用名；电阻、电容数值保留，SVG 导出使用同一规则。
- **原理图点击走线。** 默认 `Alt+W` 进入走线模式，依次单击起点和终点并自动吸附引脚；右键或 `Esc` 退出，未完成的预览不会写入工程，每条导线可一次撤销。
- **空格旋转。** 原理图选中元件后轻按 `Space` 旋转，`Shift+Space` 反向；`Space+拖拽` 仍用于平移，`R` 旋转和用户自定义绑定优先规则保留。
- **关于界面精简。** 来源只保留一句 `Forked from …`，去掉重复声明；完整版权与许可仍可离线阅读。

### 已知限制

本版未修复可选 ngspice 后端已记录的向量数值映射异常，也未完成其数值对拍；不能将该后端输出作为精确电路分析依据。“工具 → 仿真”控制内置实时仿真器，并非 ngspice 图形分析入口。ngspice 共享库不随下载包分发。

## 0.8.2 更新

- **可调界面字号。** 在“视图 → 设置 → 外观”选择 12、14（默认）、16、18、20 档，即时生效并自动保存；窄窗口采用上下排列，保留可访问的重置按钮。
- **计算器可读性。** 调大色环电阻计算器的说明、标签和结果文字，取消为适应弹窗而自动缩字，仅在内容实际溢出时滚动。
- **统一引脚地址。** 等宽字体显示引脚编号，有子板使用 `BB1.T1.j1`，无子板使用 `BB1.j1`；保留内部 ID、连线规则与存档兼容性。
- **精简元件说明。** 整理 352 条中文元件说明的标点与短句衔接，保留参数、引脚、操作示例、安全提示和仿真限制。
- **关于与开源许可。** 明确本 fork 与上游来源，新增离线许可证及致谢阅读页；分发包携带 `CREDITS.md`、`LICENSE` 和 `THIRD_PARTY_NOTICES.md`。
- **中文优先文档与 Pages。** 主 README 和站点首页改为中文，英文说明保留；修正项目链接，并修复网页构建中对桌面 ngspice 入口的引用。

### 已知限制

可选 ngspice 后端仍存在已记录的向量数值映射异常和未完成的数值对拍，本版未修复，不能将其输出作为精确电路分析依据。内置实时仿真器保持原有路径；ngspice 共享库不随本次下载包分发。

下方保留历史版本说明。

## What's new in 0.8.1

- **Redrawn resistor preview.** The colour-code calculator now shows a compact axial resistor with rounded shoulders, a narrower waist, metallic leads and shaded colour bands that follow the body contour.
- **Cleaner calculator layout.** Circular colour swatches, side-by-side resistance and tolerance fields on wider layouts, and a separate result panel replace the old flat block layout. The standalone tool no longer shows an unnecessary scrollbar, and its content scales to the available window height.
- **Simpler start screen.** Removed the project-file/upload footer text from the welcome screen in both English and Chinese.
- **Visual regression capture.** The screenshot command accepts `--sheet colorcode`; four-band and six-band layouts were captured and visually checked in addition to the self-tests.

## What's new in 0.8

- **Optional ngspice precision analysis.** Desktop builds can load libngspice at runtime for `.tran`, `.ac` and `.dc` analysis without changing the existing real-time simulator.
- **SPICE export improvements.** Exported netlists cover passive parts, semiconductor models, regulators, switches, board rails, analysis directives and skipped-model diagnostics.
- **SPICE CLI and regression checks.** Use `--spice-run` for CSV analysis output and `--spice-xcheck` to compare supported circuits with the built-in simulator.
- **Colour-code tools.** Calculate resistor bands, capacitor markings and component labels from the Tools menu.

See [docs/NGSPICE.md](../docs/NGSPICE.md) for runtime library setup and platform limitations.

## What's new in 0.7

- **Pictures on the design.** Paste a screenshot with Ctrl+V, drop an image file on the window, or use the new **Picture** button: PNG, JPEG, GIF (first frame), BMP, WebP, PSD and SVG. Drag a corner or an edge to resize (Shift keeps the proportions), drag the handle above the picture to rotate it (Shift snaps to 15°). The inspector adds a backdrop for transparent pictures, a solid or dashed frame in four widths and the stroke palette, the rotation and "original proportions". SVG stays sharp at every zoom.
- **Pictures travel with the project.** Each picture is stored once, by its content, inside the `.dez` file, in its own format; copy and paste carries it along, and undo never keeps extra copies. Up to 20 MB per picture; anything over 4096 px on its longest side (a 48 MP phone photo included) is scaled down on import, and the app says so. AVIF, HEIC and JPEG XL are not supported yet.
- **Point text and text boxes.** As in Photoshop: click with the text tool for point text whose frame hugs the words and only breaks where you press Enter; drag for a text box of fixed size that wraps. Both open for typing straight away, placeholder selected. Switch between them in the inspector; text from older files loads as a text box, unchanged.
- **Edit text where it is.** Double-click any text annotation to type on the canvas — Enter or a click elsewhere keeps the change (one undo step), Esc throws it away. Text can be set in the regular or the monospaced face.
- **Stroke widths and an easier grab.** Rectangles, lines and circles come in thin, medium, thick and extra thick. A stroke is grabbable 10 px either side, and with a drawing tool still active a press on an existing shape moves or resizes it instead of starting a new one — including a frame drawn around a chip.
- **Text fields behave like text fields.** Select with the mouse (drag, double-click a word) or Shift+arrows, Ctrl+A, cut, copy and paste over a selection — everywhere in the app, Chinese included.
- **Fixes.** Markup placed on a breadboard can be clicked again (the breadboard used to win every click); moving a text box no longer stretches it; a dropdown taller than the window scrolls instead of flashing open and picking whatever was under the pointer; the stack panel's eye icon is a softer outline.

Older versions open 0.7 projects without their pictures; everything else is unchanged.
## What's new in 0.6

- **Draw on the design.** Rectangle, line, circle and text tools in the toolbar, with a neutral palette, solid or dashed strokes and a text box with its own size. Markup sits above the board and below the components, so a box around a circuit never hides a pin, and it is saved in the project without ever becoming copper: no nets, no ratsnest, no Gerber.
- **A sidebar that stacks components, not layers.** The Stack panel (formerly Layers) lists one row per level: move a level up or down, select two or more parts and group them so they share one level, ungroup them again. Groups cannot nest, and a group moves as one block. The panel no longer exposes the renderer's layers — copper, silkscreen and pads are implementation details.
- **Wires behave like objects.** Left-drag a wire to move the whole run; the middle button adds a bend and dragging it bends the wire; the middle button on a bend removes it. Double-click still adds a bend. Thin traces now win the hit test over the component behind them, and an end that is actually connected no longer shows the red "unconnected" dot.
- **Your colour stays put.** New wires reuse the last colour you picked in that view; breadboard, schematic and PCB each remember their own, across restarts. Autorouting, pasting and undo never overwrite the memory.
- **Settings are a file you can read.** Preferences moved to `config.toml`, with comments and unknown keys preserved when the app writes. An old `settings.json` is migrated once and kept as `settings.json.bak`; a damaged file falls back to the backup and says so.
- **Shortcuts and mouse buttons are yours.** View ▸ Settings maps 16 actions and the middle/right/space-drag slots. Conflicts ask before replacing, each row resets on its own, and the whole section has a confirmed "restore defaults". Changes apply immediately and survive a restart.
- **A first screen instead of a surprise.** Launching now asks whether to open a project or start a new one, with an offer to restore unsaved work. A new project is a single HUST EIC lesson breadboard with nothing plugged in.

The project format is version 3 (parts gained an optional `group`); version 1 and 2 files still open, with no markup or groups.

## What's new in 0.5.2

- **Chinese paragraphs wrap properly.** The text wrapper only broke lines at spaces, and Chinese has almost none, so it treated a whole sentence as one word: every line stopped early and left a ragged gap on the right. The About box and the part descriptions showed it worst. Lines now fill to the edge, and a comma or full stop never lands alone at the start of a line. English wraps exactly as before.
- **The Chinese reads like Chinese.** 242 strings were rewritten: no em-dashes standing in for full stops, no semicolons between clauses, no hedging. Shorter sentences, plainer wording.

## What's new in 0.5.1

- **A fraction of the download.** The two bundled fonts carried about 21 000 hanzi each and made up 40 MB of every package. They now ship as subsets — Latin, Greek, punctuation and arrows, box drawing, kana, and the 6 763 everyday hanzi — so the downloads drop from 25/28/24 MB to 9/12/8 MB (Linux/macOS/Windows), a 65% cut. Nothing looks different: the outlines and hinting are untouched, and the subset still holds more than twice the characters the interface can draw.
- **Rare characters still render.** Anything outside the subset — traditional Chinese, an uncommon name, a Japanese kanji — is drawn by the system font: Microsoft YaHei on Windows, PingFang on macOS, Noto CJK on Linux. The web and mobile builds have no system fonts to fall back on; `tools/subset_fonts.py --set gbk` rebuilds the fonts with the wider repertoire.

## What's new in 0.5

- **中文界面.** The whole app — menus, toolbar, inspector, dialogs, simulation warnings, design-rule messages, the Arduino interpreter's errors and the AI panel — now speaks English and Simplified Chinese. Switch under **View ▸ Language**; the choice is remembered, and a first launch follows your system language.
- **A format, not a second language baked in.** Every string is a key in `i18n/*.json`, compiled into the binary so the web and mobile builds carry both languages with nothing extra to ship. Drop `i18n/xx.json` next to the executable and a new language appears in the menu — no rebuild, no code change.
- **The parts library too.** All 352 part types are translated — names, categories, property labels and the long notes for the 74xx/CD4000 logic, the boards and the modules — while a design's own text (labels, notes, values like `470Ω` or `Yellow`) stays exactly as you wrote it. A design made in one language opens identically in the other.
- **Authoring tools.** `designElectronics --export-i18n ./i18n` writes a complete template, including the part-library keys a source scan cannot see; `tools/i18n_check.py --strict`, now part of CI, catches a missing key, a mismatched `{0}` count and keys nobody uses.
- **A Chinese README and landing page** — [README.zh-CN.md](https://github.com/LeonhardtHUST/designElectronics/blob/main/README.zh-CN.md) · [中文介绍](https://leonhardthust.github.io/designElectronics/zh/).

## What's new in 0.4

- **Your own components.** Import a `.dezpart` model — name, datasheet, pins, internal connections, silkscreen, a drawing for each view and the simulation model — and it joins the parts bin under **Custom** for every project, not just the one you imported it in. Export any part, built-in ones included, to start from something that already looks right. [Format reference](https://github.com/LeonhardtHUST/designElectronics/blob/main/docs/PART_MODELS.md).
- **Add a chip from its datasheet.** Paste a short block (`[74HC03]`, `package = DIP-14`, `pins = 1A 1B 1Y …`) and the app builds the package, the pads, the schematic box and the silkscreen for you — no drawing required.
- **Ask an AI to do it.** Five new MCP tools (`list_part_models`, `get_part_model`, `import_part_model`, `export_part_model`, `delete_part_model`) manage the library from an assistant, and an imported part is usable with `add_part` immediately.

## What's new in 0.3

- **HUST EIC Lesson breadboard.** One carrier plate holding four SYB-120 style 700-point tiles, laid out like the boards in the lab. Each tile has two five-row fields split by the central channel — a column's five holes are one net, the two fields are not joined — and a segmented power row along each long edge: ten five-hole blocks wired as three nets (blocks 1-3, 4-7, 8-10). Parts snap into the holes, and the hole ids carry the tile number (`1j1`…`4j60`, edge rows `1u1`…`4u50` and `1l1`…`4l50`).
- **The lab ICs.** **74HC03** quad open-drain NAND (pulls low or lets go — add a pull-up, it does wired-AND), **CD40192** BCD up/down counter with preset, reset and carry/borrow that pulse for the half clock cycle at the terminal count, so `CARRY` clocks the next package directly, and **HG5532** dual low-noise audio op-amp. Each comes with its datasheet link, aliases and a schematic symbol, and the 555 timer now answers to `NE555` in the part search.
- **Chips line up with the holes.** Every hole on the lesson breadboard sits on the 0.1 inch grid, so a DIP dropped on the grid lands all its legs in holes instead of straddling them.

## What's new in 0.2.1

- **AI tab — build circuits by chatting.** Describe a circuit and the AI tool you already have (**Claude Code**; Codex and Gemini CLI experimental; or any custom command) builds it live in the open design: parts, breadboard placement, wiring, Arduino sketches and a simulation check. Every change is undoable, the agent may only use designElectronics' tools, and it runs on your own AI account.

## What's new in 0.2

- **Program your boards.** Write an Arduino sketch for an Uno, ESP32, Pico, STM32 and more. It runs in the simulator with the executing line highlighted and a Serial Monitor, and uploads to the real board through arduino-cli / Arduino IDE 2.
- **Videos and GIFs for social media.** File ▸ Export ▸ Video or GIF records the running circuit as Reels/Shorts/TikTok (9:16), Instagram posts (4:5), square or YouTube (16:9), including a tour through breadboard, schematic and PCB.
- **Embed circuits in blogs.** File ▸ Embed in a Website gives you an `<iframe>`; readers can switch views and run the simulation. The design travels inside the link — nothing is uploaded.
- **Web app.** The whole editor also runs in the browser.
- **Layers panel**, **version history with optional GitHub sync**, an **MCP server for AI assistants**, 20 more boards (Raspberry Pi 5/4/3/Zero, Arduino Mega/Due/Uno R4…), true-to-size parts and many simulator fixes.

## Download

| System | File |
| --- | --- |
| macOS 11+ (Apple Silicon and Intel) | `designElectronics-macOS.zip` |
| Windows 10/11 (64-bit) | `designElectronics-Windows-x64.zip` |
| Linux x86_64 (glibc 2.35+, X11 or Wayland) | `designElectronics-Linux-x86_64.tar.gz` |

## First launch

These builds aren't code-signed yet, so your system will ask before opening them the first time.

- **macOS:** unzip, move `designElectronics.app` to Applications, then **right-click ▸ Open ▸ Open**. If macOS says the app is damaged, run `xattr -dr com.apple.quarantine /Applications/designElectronics.app` once.
- **Windows:** unzip and run `designElectronics.exe`. If SmartScreen appears, click **More info ▸ Run anyway**. Keep the `assets` folder next to the exe.
- **Linux:** `tar xzf designElectronics-Linux-x86_64.tar.gz && ./designElectronics/designElectronics`.

Start with **File ▸ Examples** (555 blinker, ESP32 blink, Raspberry Pi LED…) or the [quick start](https://leonhardthust.github.io/designElectronics/#quickstart).

GitHub sync is available in the macOS and Linux builds; Windows builds have local version history only for now.
