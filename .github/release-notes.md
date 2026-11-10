Free, open-source electronics design for desktop and phone: breadboard, schematic and PCB views, live simulation, Gerber export, version history and an MCP server for AI assistants.

📖 **Documentation:** https://rorretbolt.github.io/designElectronics/ · 🌐 **Try it in your browser:** https://rorretbolt.github.io/designElectronics/#app

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

Start with **File ▸ Examples** (555 blinker, ESP32 blink, Raspberry Pi LED…) or the [quick start](https://rorretbolt.github.io/designElectronics/#quickstart).

GitHub sync is available in the macOS and Linux builds; Windows builds have local version history only for now.
