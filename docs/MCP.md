# Using designElectronics from an AI assistant (MCP)

designElectronics has a built-in [Model Context Protocol](https://modelcontextprotocol.io)
server. An AI assistant (Claude Desktop, Claude Code, Cursor, VS Code Copilot, or
any MCP client) can then work on the design you have open: pick parts, wire them,
plug them into a breadboard, simulate, lay out and route the PCB, check it and
export Gerbers. Whatever it builds appears in the app as it happens.

It runs locally. No server, account or network is involved.

## Attaching to the app you have open

`designElectronics --mcp` looks for a running app before it does anything else.

1. A window with a design open listens on `127.0.0.1`, on a port it picks, with a
   token generated at startup. It publishes both as one small record per process
   under `<prefs>/agent/instances/<pid>.json`:

   | Platform | Folder |
   | --- | --- |
   | Windows | `%APPDATA%\designElectronics\designElectronics\agent\instances` |
   | macOS | `~/Library/Application Support/designElectronics/designElectronics/agent/instances` |
   | Linux | `~/.local/share/designElectronics/designElectronics/agent/instances` |

   The record is written when the design appears and deleted when the app quits.
2. `--mcp` reads those records, prefers the window you used last, checks the
   handshake, and forwards every tool call to it. `get_design` returns the design
   on your screen, and `add_part` shows up there immediately.
3. With no window running — or a window still sitting on the welcome screen, where
   no design exists yet — the server keeps its own design in memory instead.
   `get_design` then says `"standalone": true`, so an assistant can tell the two
   apart. Open the app and the next MCP session attaches; inside one session the
   server still attaches on the first tool call if a window appeared and it has
   not changed its own design yet.
4. `--standalone` never attaches (the server always keeps its own design).
   `--attach` refuses to start unless a window answers.

The in-app AI tab needs none of this: it starts
`designElectronics --mcp --connect <port> --token <token>` itself and talks to the
same bridge.

## Setup

First build the app (see the [README](../README.md)). The server is the app
binary itself:

| Platform | Binary |
| --- | --- |
| macOS | `/path/to/designElectronics.app/Contents/MacOS/designElectronics` |
| Linux / Windows | `/path/to/designElectronics` (`designElectronics.exe`) |

### Claude Code

```sh
claude mcp add designElectronics -- /path/to/designElectronics --mcp
```

### Claude Desktop, Cursor, VS Code and other clients

Add this to the client's MCP configuration (for Claude Desktop:
*Settings ▸ Developer ▸ Edit Config*):

```json
{
  "mcpServers": {
    "designElectronics": {
      "command": "/path/to/designElectronics",
      "args": ["--mcp"]
    }
  }
}
```

Then ask things like:

* "Design an ESP32 circuit that blinks a red LED on GPIO2 on a breadboard, simulate it and save it to ~/blink.dez."
* "Open ~/blink.dez, make a 50×40 mm PCB for it, autoroute it, run DRC and export Gerbers."
* "Build a 555 astable that blinks at about 2 Hz and check the frequency in simulation."

## Tools

| Tool | What it does |
| --- | --- |
| `list_parts` | Search the parts library by words and/or category, built-in and custom. |
| `get_part_info` | Pins, properties (with allowed values), datasheet and simulation model of a part type. |
| `list_part_models` / `get_part_model` | List the models in the library (built-in and installed) and read one in full: metadata, pins in every view, buses, drawings and simulation definition. |
| `import_part_model` | Install a model from a `.dezpart` file, a model document, or a datasheet block (`[74HC03]` + `package = DIP-14` + `pins = …`). Validates it, writes it to the user library and registers it. |
| `export_part_model` | Write a model to a `.dezpart` file (or return the JSON inline). Works for built-in parts too, which is the easy way to start a custom one. |
| `delete_part_model` | Remove an installed model. Built-in parts are never deleted. |
| `new_design` / `open_design` / `load_example` / `save_design` | Start, open, load a built-in example, save as `.dez`. Every save (with an optional `message`) is also kept in the app's Version History. |
| `get_design` | Parts (labels, properties, positions), wires, nets and routing status per view, including which connections are still unrouted. Says whether it is the attached window or a standalone copy. |
| `add_part` | Add a part. Without `x`/`y` (mils) parts fill tidy rows in every view. Returns its pins. |
| `set_part_properties` | Change values, colours, labels, a dev board's `outputs` (e.g. `GPIO2=BLINK:500`) or its program (`properties.code` = an Arduino sketch that runs in the simulator); hide a part in some views or lock it. |
| `move_part` | Move, rotate or flip a part in one view. |
| `place_on_breadboard` | Plug a part in so a given pin sits in a given hole (`a1`…`j30`, rails `tp1`/`tn1`/`bp1`/`bn1`; on the HUST EIC Lesson carrier `1a1`…`4j60` for the field holes and `1u1`…`4u50` / `1l1`…`4l50` for the segmented power rows, the leading digit being the tile); reports the hole each pin landed in and what it now connects to, and warns about pins on a power rail, off the board or shorted together. |
| `remove_part` / `remove_wire` | Delete things. |
| `connect` | Wire two pins (schematic + breadboard by default). Connections count in every view. |
| `simulate` | Run the simulator: LED brightness, display segments, meter readings, motor activity, damaged parts, warnings, net voltages, plus each board's Serial output and program errors. |
| `autoroute` | Route the PCB (needs a `pcb_board` part under the components in the PCB view). |
| `run_drc` | Design rules check. |
| `export` | `gerber` (zip), `svg`, `bom`, `netlist`, `spice`, written to a file or returned inline. |

Parts and pins are referenced by label (`"R1"`) or id, and pins by id or name
(`"anode"`, `"GPIO2"`, `"D13"`). Coordinates are mils (1/1000 inch); breadboard
holes are 100 mil apart.

## Notes

* Attached, every tool call is a normal edit in the window: `Ctrl/⌘+Z` undoes it,
  and `save_design` writes the design you are looking at.
* Standalone (no app running), the server holds one design in memory. Call
  `save_design` to write it, then open the file in the app.
* If a client was started while no app was running and then edits that private
  design, it stays standalone for the rest of the session: attaching afterwards
  would silently swap the design under the assistant. Restart the client to attach.
* The record holds the token, so it is written with owner-only permissions on
  macOS/Linux and lives in your user profile on Windows. Tests can point the
  lookup somewhere else with the `DE_LIVE_DIR` environment variable.
* A dev board runs its Arduino sketch (`properties.code`) in the simulator's
  built-in interpreter. Without a sketch, it drives the pins listed in its
  `outputs` property (`HIGH`, `LOW`, `BLINK:ms`, `PWM:%`).
* Test it without an AI: `python3 tests/mcp_smoke_test.py path/to/designElectronics`
  (standalone) and `python3 tests/mcp_attach_test.py path/to/designElectronics`
  (attaching to a fake app).
