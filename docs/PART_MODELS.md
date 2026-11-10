# Part models (`.dezpart`)

A **part model** is everything designElectronics needs to draw, wire and
simulate one component: its name and description, its pins and where they sit in
the breadboard, schematic and PCB views, which pins are joined internally, the
drawing primitives for each view, the silkscreen text and the simulation model.

Models are edited as JSON, and the same document is what the MCP tools read and
write. You rarely type one by hand: export a built-in part, change what you
need, import it back — or write a six-line datasheet block and let the app
generate the package, the pads, the symbol and the silkscreen.

## Where models live

| | |
| --- | --- |
| User library | `<app data>/parts/*.dezpart` — Windows `%APPDATA%\designElectronics\designElectronics\parts`, macOS `~/Library/Application Support/designElectronics/designElectronics/parts`, Linux `~/.local/share/designElectronics/designElectronics/parts` |
| In a project | Every `.dez` embeds the models it uses, so a shared file opens anywhere, even if the receiver has none of them |
| Override | `DE_PARTS_DIR=/some/dir` points the library somewhere else (tests, portable installs); `DE_NO_USER_PARTS=1` ignores it |

Installed models show up in the parts bin under **Custom**, and in `list_parts`
over MCP. Deleting a model never touches designs that already use it — they keep
their embedded copy.

## File format

```json
{
  "format": "designElectronics-part",
  "version": 2,
  "meta": {
    "id": "74hc03",
    "name": "74HC03 Quad NAND",
    "category": "Logic",
    "datasheet": "https://www.ti.com/lit/ds/symlink/sn74hc03.pdf",
    "author": "Leon",
    "created": "2026-11-10",
    "source": "json",
    "tags": ["nand", "open-drain", "quad"],
    "notes": "Open-drain: add a pull-up.",
    "replaces": false
  },
  "part": {
    "type": "74hc03",
    "title": "74HC03 Quad NAND",
    "category": "Logic",
    "description": "Four 2-input NAND gates with open-drain outputs.",
    "datasheet": "https://www.ti.com/lit/ds/symlink/sn74hc03.pdf",
    "tags": "nand open-drain quad",
    "silkscreen": "74HC03",
    "labelPrefix": "U",
    "views": ["breadboard", "schematic", "pcb"],
    "sim": "logic",
    "simParams": "ODNAND 1,2>3;ODNAND 4,5>6;ODNAND 9,10>8;ODNAND 12,13>11",
    "properties": { "package": "DIP-14" },
    "propertyDefs": [
      { "key": "part", "label": "Part #", "options": ["74HC03"] },
      { "key": "package", "label": "Package", "options": ["DIP-14", "SOIC-14"] }
    ],
    "pins": [
      { "id": "1", "name": "1A", "breadboard": [-500, -400], "schematic": [-700, -300], "pcb": [-300, -300], "pad": 70, "drill": 38 }
    ],
    "buses": [],
    "holes": [],
    "graphics": {
      "breadboard": [ { "k": "rect", "a": [-400, -500], "b": [400, 500], "fill": "#2b2b30" } ],
      "schematic": [ { "k": "rect", "a": [-600, -400], "b": [600, 400], "fill": "#fff8d8", "stroke": "#202020", "w": 10 } ],
      "pcb": [ { "k": "rect", "a": [-350, -450], "b": [350, 450], "fill": "#1a1a1e" } ]
    }
  },
  "block": "[74HC03]\ntitle = Quad NAND\npackage = DIP-14\npins = 1A 1B 1Y …"
}
```

### `meta`

| key | meaning |
| --- | --- |
| `id` | Type id, the name other tools use. Lower case letters, digits and `_`. Defaults to a slug of the name. |
| `name` | Display name. Defaults to `part.title`. |
| `category` | Parts-bin category (see `list_parts` for the list). Defaults to `part.category`, then `Custom`. |
| `datasheet` | URL, shown in the model info dialog and `get_part_info`. |
| `author`, `created`, `notes` | Free text, kept with the model. |
| `tags` | Extra search words, as an array or a space/comma separated string. |
| `source` | `json` or `block`; describes how the model was authored. |
| `replaces` | `true` allows the model to shadow a **built-in** type of the same id. Without it an import that collides with a built-in is refused. |

### `part`

The definition the editor renders. All coordinates are mils (1/1000 inch).

| key | meaning |
| --- | --- |
| `type` | Type id; must equal `meta.id` when both are present. |
| `title` | Shown in the bin and the Inspector. |
| `description` | One or two sentences. |
| `silkscreen` | Text printed on the body. The package generator fills it in; keep it for a hand-drawn IC. |
| `labelPrefix` | Reference designator prefix (`U`, `R`, `Q`…). |
| `views` | Any of `breadboard`, `schematic`, `pcb`. |
| `sim`, `simParams` | Simulator model; see [FILE_FORMAT.md](FILE_FORMAT.md#simulation-models-sim--simparams). Leave `sim` out for a pinout-only part. |
| `properties`, `propertyDefs` | Defaults and the editable fields the Inspector shows. |
| `pins` | One object per pin: `id`, `name`, a `[x, y]` position per view, and the PCB pad (`pad` diameter in mils, `drill` 0 for SMD, `padH` for oval pads, `square`). |
| `buses` | Arrays of pin indices that are joined inside the part, e.g. two ground pins. |
| `holes` | Non-plated PCB holes: `{ "at": [x, y], "diameter": 130 }`. |
| `graphics` | Drawing primitives per view (below). |

### Drawing primitives

Every entry has `k` and, optionally, `w` (stroke width), `stroke`, `fill`, `tag`.

| `k` | fields |
| --- | --- |
| `line` | `a`, `b` |
| `rect` | `a`, `b` (corners), `r` (corner radius) |
| `circle` | `c`, `r` |
| `arc` | `c`, `r`, `a0`, `a1` (degrees, clockwise) |
| `text` | `at`, `text`, `size`, `align` (0 left, 1 centre, 2 right) |
| `poly` | `pts` (filled when it has a `fill`, a polyline when it only has a stroke) |

`tag` 1 marks an LED lens that glows in simulation, 2 a meter display.

## The short way: a datasheet block

A whole model can be written as one block in the format the built-in library
uses ([parts/README.md](../parts/README.md)). The app expands it into a full
definition: package outline, pads, pin positions, schematic box, silkscreen and
the `part`/`package` properties.

```
[74HC03]
title     = Quad 2-Input NAND (Open Collector)
category  = Logic
desc      = Four 2-input NAND gates with open-drain outputs.
datasheet = https://www.ti.com/lit/ds/symlink/sn74hc03.pdf
package   = DIP-14, SOIC-14, TSSOP-14
pins      = 1A 1B 1Y 2A 2B 2Y GND 3Y 3A 3B 4Y 4A 4B VCC
sim       = logic
simParams = ODNAND 1,2>3;ODNAND 4,5>6;ODNAND 9,10>8;ODNAND 12,13>11
```

Over MCP:

```json
{ "name": "import_part_model", "arguments": { "block": "[74HC03]\ntitle = Quad NAND\npackage = DIP-14\npins = …" } }
```

Or through the file: `{ "format": "designElectronics-part", "version": 2, "block": "[74HC03]\n…" }`.

A block cannot describe custom artwork. When you need that, import the block
first and then export it — the exported document has the generated geometry as
plain primitives you can edit.

## Checks

`import_part_model` (and the Import Part dialog) refuse a model when:

* the type id is missing or not lower-case `[a-z0-9_]`
* there are no pins, or two pins share an id
* a pin position is not a finite number, or a pad has a non-positive size
* a bus names a pin index that doesn't exist
* two pads overlap
* it is not in any view
* the type collides with a built-in part and `meta.replaces` is not `true`

Warnings (the model still installs): no simulator model, no breadboard drawing,
no schematic symbol.

## Over MCP

| Tool | Use |
| --- | --- |
| `list_part_models` | `{query, scope: all\|builtin\|custom}` — what is available, with its file. |
| `get_part_model` | `{type}` — the full document, same shape as above. |
| `import_part_model` | `{path}` or `{json}` or `{block}`, plus `overwrite` — validate, write to the library, register. |
| `export_part_model` | `{type, path?}` — write a `.dezpart`; without `path`, the JSON comes back inline. |
| `delete_part_model` | `{type}` — remove an installed model. |

Typical loop for a new chip:

1. `export_part_model {"type": "74hc00"}` — start from a part that already looks right.
2. Change the id, title, datasheet, pin names and `simParams`.
3. `import_part_model {"json": …, "overwrite": true}`.
4. `add_part {"type": "74hc99"}`, wire it, `simulate`.
