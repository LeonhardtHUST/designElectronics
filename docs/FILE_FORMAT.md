# The `.dez` file format (version 3)

A designElectronics project is one UTF-8 **JSON** file with the extension
`.dez`. It is self-contained: besides the design itself it embeds the full
definition of every part type it uses, so it opens in any designElectronics
install, offline, even one that has never seen those parts.

Units: all coordinates and sizes are **mils** (1/1000 inch, 0.0254 mm). The
y axis points down. Breadboard holes are 100 mil apart.

## Top level

```jsonc
{
  "format": "designElectronics",   // required, identifies the file
  "version": 3,                    // format version; readers ignore unknown fields
  "app": "designElectronics 0.1.0",
  "uid": "97b8da3c1e0f4a21",       // optional: stable design id; local version history follows it
  "title": "LED with battery",
  "author": "Ada",                 // optional
  "notes": "Project description",  // optional
  "saved": "2026-10-16T05:36:48Z",
  "github": {                      // optional: where the design is committed on GitHub
    "repo": "octocat/electronics-designs",
    "path": "led-with-battery-97b8da/led-with-battery.dez",
    "branch": ""                   // empty = the repository's default branch
  },
  "rules": { "traceWidth": 24, "clearance": 12, "viaPad": 40, "viaDrill": 20 },
  "nextId": 14,
  "nextGroup": 2,                  // optional: next hierarchy group id (version 3 and newer)
  "parts":   [ ... ],
  "wires":   [ ... ],
  "notes_":  [ ... ],              // sticky notes on the canvas
  "annos":   [ ... ],              // drawing annotations (version 2 and newer)
  "images":  { ... },              // optional: pictures an image annotation points at
  "library": [ ... ]               // embedded part definitions
}
```

## Parts

```jsonc
{
  "id": 2,                         // unique among parts, wires and notes
  "type": "resistor",              // part type; looked up in the library
  "label": "R1",
  "props": { "resistance": "470Ω", "power": "1/4W", "tolerance": "±5%" },
  "views": {                       // placement in each view the part appears in
    "breadboard": { "at": [500, 1000] },
    "schematic":  { "at": [300, 0], "rot": 90 },
    "pcb":        { "at": [500, 400], "flip": true, "hidden": true }
  },
  "locked": true,                  // optional: can't be moved or deleted in the editor
  "group": 2,                      // optional: hierarchy group (version 3 and newer)
  "code": "void setup() {…}\nvoid loop() {…}"  // optional: the board's program (Arduino sketch)
}
```

`rot` is clockwise in degrees (0, 90, 180, 270), applied after `flip`, which
mirrors horizontally. `hidden` hides the part in that view only; it is purely
visual (the part still connects, simulates and exports).

## Hierarchy

The order of `parts` is the stacking order: earlier entries are further back,
later ones are further forward. A part with a `group` id shares its level with
every other part carrying the same id. Group members must be **neighbours** in
the array; readers pull scattered members together, and a group left with fewer
than two members is dissolved. Groups never nest: a part has at most one group
id, so a group cannot contain another group. `nextGroup` is the next free id.

## Wires and traces

```jsonc
{
  "id": 6,
  "view": "breadboard",            // breadboard | schematic | pcb
  "points": [[500, 1400], [500, 1600]],   // ≥ 2 points; inner points are bends
  "from": { "part": 1, "pin": "a6" },     // optional: end attached to a pin
  "to":   { "part": 1, "pin": "bp5" },
  "color": "#dc3232",              // breadboard only
  "width": 24, "layer": "top",     // pcb only (layer: top | bottom)
  "autorouted": true               // pcb only, set by the autorouter
}
```

Ends without `from`/`to` are connected by position: an end within 15 mil of a
pin, or within 3 mil of another wire end in the same view, connects to it.

## Connectivity rules

* Two pins of different parts within 15 mil of each other in a view are
  connected (that's how parts plug into breadboards and headers).
* Pins listed together in a part's `buses` are always connected (breadboard
  strips, the two GND pins of an Arduino, …).
* Parts with a `netProp` (ground, power and net labels) connect to every other
  such part with the same value.
* A connection in **any** view counts in **all** views. Views where it isn't
  wired show ratsnest lines.

## Notes

```jsonc
{ "id": 13, "view": "breadboard", "at": [0, -1100], "size": [2200, 600], "text": "Hello" }
```

## Annotations

Markup drawn over the design: rectangles, lines, circles, text and pictures.
Annotations are **never** electrical — they are not connected, simulated, routed,
checked by DRC or written to a netlist, Gerber or BOM — so a circle drawn around
a pin creates no net.

```jsonc
{
  "id": 21,
  "view": "schematic",             // breadboard | schematic | pcb
  "kind": "rect",                  // rect | line | circle | text | image
  // rect/line: start and end point. circle: centre and a point on the circle.
  // text and image: top-left corner and the box size (b is a size, not a point).
  "a": [200, 1200],
  "b": [1600, 2400],
  "stroke": "#787c82",             // default: neutral grey
  "fill": "#fff6b0",               // optional: text/backdrop background
  "dashed": true,
  "widthStep": 0,                  // 0 thin, 1 medium, 2 thick, 3 extra thick
  "text": "check the polarity",    // kind == "text"
  "textSize": 108,                 // world units, as for notes
  "image": "3b1f…d9c4",            // kind == "image": key into "images"
  "rotation": 20,                  // kind == "image": degrees, clockwise about the centre
  "framed": true,                  // kind == "image": draw a border (stroke/widthStep/dashed)
  "hidden": false                  // hidden by the Layers panel
}
```

An unknown `kind` is skipped on load (with a log line) and the rest of the file
still opens. Version 1 files have no `annos` field at all: they load with an
empty list.

## Pictures (`images`)

An image annotation keeps only a **content key**; the bytes live once in the
top-level `images` object, so the same picture placed ten times is stored once:

```jsonc
"images": {
  "3b1f…d9c4": {                    // SHA-1 of the bytes, lowercase hex
    "mime": "image/png",            // image/png | image/jpeg | image/gif | image/bmp |
                                    // image/webp | image/vnd.adobe.photoshop |
                                    // image/x-tga | image/svg+xml
    "w": 640, "h": 480,             // intrinsic pixels; SVG: its own width/viewBox size
    "data": "iVBORw0KGgo…"          // the encoded bytes, base64, exactly as imported
  }
}
```

Only entries an annotation still refers to are written, and an `images` object
with no users is left out entirely: deleting an annotation drops its picture from
the next save. The key is a hash, so the content is immutable — loading a file
keeps the pictures already in memory and adds the ones it brings.

Pictures are **not** part of undo snapshots (they would multiply the memory of the
150-step undo stack) and are **not** part of share links (`#d=…`): the bytes would
push a URL far past what browsers and chat apps accept. A design shared by link
therefore shows its markup but not its pictures; save it as a `.dez` file to carry
them along.

A picture gets into a design from the toolbar's Picture button, Edit ▸ Add
Annotation ▸ Picture…, `Ctrl+V` with a picture on the system clipboard, or by
dropping a file on the window. The same bytes always produce the same key, so
copying an annotation through the internal clipboard carries one copy of the
picture, and pasting it again adds nothing to the file.

Supported formats and limits (sniffed from the bytes, never from the file name):

| Format | Read as | Notes |
| --- | --- | --- |
| PNG | `image/png` | keeps the original bytes unless it is scaled |
| JPEG | `image/jpeg` | |
| GIF | `image/gif` | first frame only |
| BMP | `image/bmp` | |
| WebP | `image/webp` | lossy, lossless and alpha (libwebp decoder) |
| PSD | `image/vnd.adobe.photoshop` | flattened composite (stb_image) |
| TGA | `image/x-tga` | no magic number: recognised by name only |
| SVG | `image/svg+xml` | kept as text and rasterized on demand |

* at most **20 MB** of encoded bytes per picture;
* at most **64 megapixels** decoded on import (enough for a 48–50 MP phone photo,
  which is then scaled down; a picture whose *encoded* size is small but whose
  bitmap would be huge is refused instead of allocated). Pictures stored in a file
  are at most 4096 px on a side, so drawing never decodes more than that;
* at most **4096 px** on the longest side. A bigger bitmap is scaled down and
  re-encoded in place — PNG while it has transparency, JPEG (quality 90)
  otherwise — and `note` on import says so. If it is still over 20 MB after that,
  the import is refused;
* SVG markup may be at most **5 MB**, and a rasterized SVG never exceeds 4096 px.
* Not supported: AVIF, HEIC/HEIF, JPEG XL, animated GIF beyond the first frame and
  compressed SVG (`.svgz`) — unzip it first.

## Part definitions (`library`, `.dezpart`)

The `library` array holds one definition per part type used. A `.dezpart` file
holds a single definition; the model format (metadata, datasheet, tags,
silkscreen, the user library and the MCP tools that manage it) is documented in
[PART_MODELS.md](PART_MODELS.md):

```jsonc
{ "format": "designElectronics-part", "version": 2,
  "meta": { "id": "my_sensor", "name": "My Sensor", "datasheet": "https://…" },
  "part": { ...definition... } }
```

Older files with only `"part"` (version 1), and bare definitions, still load.

A definition:

```jsonc
{
  "type": "my_sensor",             // unique id; use a prefix to avoid clashes
  "title": "My Sensor",
  "category": "Custom",
  "description": "Shown in the Inspector",
  "labelPrefix": "U",
  "valueProp": "model",            // property shown next to the label
  "netProp": "",                   // see connectivity rules
  "sim": "",                       // simulator model (resistor, led, battery, …) or empty
  "views": ["breadboard", "schematic", "pcb"],
  "properties": { "model": "XYZ-1" },            // defaults
  "propertyDefs": [ { "key": "model", "label": "Model", "options": ["XYZ-1", "XYZ-2"] } ],
  "pins": [
    { "id": "vcc", "name": "VCC",
      "breadboard": [0, 0], "schematic": [0, 100], "pcb": [0, 0],
      "pad": 70, "drill": 38, "square": true },
    { "id": "out", "name": "OUT",               // surface-mount pad: drill 0
      "breadboard": [100, 0], "schematic": [500, 100], "pcb": [100, 0],
      "pad": 24, "padH": 60, "drill": 0 }
  ],
  "buses": [["gnd1", "gnd2"]],
  "holes": [ { "at": [100, 100], "diameter": 125 } ],   // non-plated PCB holes
  "graphics": {
    "breadboard": [ primitives… ],
    "schematic":  [ primitives… ],
    "pcb":        [ primitives… ]   // silkscreen; copper pads come from pins
  }
}
```

### Pads

Positions and sizes are in mils (1/1000 inch). `pad` is the pad width and
`padH` its height (omit it, or 0, for a square/round pad). A pad with a `drill`
is plated through-hole: copper on both layers, round, or oval when `padH`
differs; `square: true` makes it rectangular. A pad with `"drill": 0` is
surface-mount. It is always rectangular, and its copper, solder mask and paste
go on the component side only: the top layer, or the bottom layer on a
single-sided board. Gerber export adds a paste file whenever the board has SMD
pads.

### Graphic primitives

| `k` | fields |
| --- | --- |
| `line` | `a`, `b`, `w`, `stroke` |
| `rect` | `a` (min corner), `b` (max corner), optional `r` (corner radius), `fill`, `stroke`, `w` |
| `circle` | `c`, `r`, `fill`, `stroke`, `w` |
| `arc` | `c`, `r`, `a0`, `a1` (degrees, 0 = +x, clockwise), `w`, `stroke` |
| `poly` | `pts` — filled polygon if `fill` is set, otherwise a polyline |
| `text` | `at` (vertical centre), `text`, `size`, `align` (0 left, 1 centre, 2 right), `fill` |

Colours are `#rrggbb` or `#rrggbbaa`. Optional `tag`: `1` marks an LED lens
(glows in simulation), `2` marks a meter display (shows the simulated reading),
`10`–`17` mark display segments a–g and dp (`fill` = unlit colour, `stroke` = lit colour).

## Simulation models (`sim` + `simParams`)

Any part, including your own, can be simulated by naming a model in `sim` and
mapping its pins in `simParams`: a `;`-separated list of `KEY=value` settings,
bare flags and `ITEM args` lines. A pin is given by its 1-based number, its name or its id; a name also matches the part before a `/` (so `GPIO2` finds the pin named `GPIO2/SDA`).

| `sim` | `simParams` example | Behaviour |
| --- | --- | --- |
| `logic` | `VCC=14;GND=7;NAND 1,2>3;NOT 5>6;ROUT=50` | Digital gates: `AND OR NAND NOR XOR XNOR NOT BUF` (inputs `>` output), Schmitt `SNOT SNAND`, open-drain `ODNAND a,b>y` / `ODINV a>y` / `ODBUF a>y` (pull low or let go — add a pull-up), tri-state `TRI a,oe>y`, `DFF d,clk,pre,clr>q,qn` (active-low), `DFFH` (active-high set/reset), `CNT clk,clr>qa..qd`, `CNTUD up,down,~pl,reset,j1..j4>q1..q4,carry,borrow` (CD40192 BCD up/down counter), `SR595 ser,srclk,rclk,srclr,oe>qa..qh,qh'`, `J4017 clk,inh,rst>q0..q9,co`, `DEC138 a,b,c,g1,g2a,g2b>y0..y7`, `BCD7 a,b,c,d,lt,bl,le>a..g`, `MUX s0,s1,s2,e,z>y0..y7`, `SW ctrl>a,b`. Inputs above ½ VCC read HIGH; outputs drive to the rails through `ROUT` Ω. |
| `opamp` | `VCC=8;GND=4;HI=1.5;LO=0.02;OA 3,2>1` | High-gain amplifier (`OA in+,in->out`) limited to `VCC−HI … GND+LO`. |
| `comparator` | `VCC=8;GND=4;CMP 3,2>1` | Open-collector output pulls LOW when in+ < in−. |
| `555` | `U GND=1,TRIG=2,OUT=3,RESET=4,CTRL=5,THR=6,DIS=7,VCC=8` | 555 timer (repeat `U` for a 556). |
| `board` | `GND=GND;RAIL 3V3=3.3;RAIL 5V=5;IO=3.3;ROUT=40` | USB-powered dev board; drives the pins listed in its `outputs` property: `PIN=HIGH`, `LOW`, `BLINK:ms`, `PWM:%`. |
| `mcu` | `VCC=7,20;GND=8,22;ROUT=25` | Bare microcontroller chip powered from the circuit, same `outputs` property. |
| `reg` | `IN=3;OUT=2;GND=1;V=3.3;DROP=1.1` | Linear regulator (`ADJ=` + `VREF=` for LM317-style, `VPROP=` to read the voltage from a property, `BOOST` for step-up). |
| `sensor` | `VCC=1;GND=3;LIN 2:temperature:0:0.01` | Outputs from properties: `LIN pin:prop:a:b` (V = a + b·prop), `RATIO pin:prop` (% of VCC), `DIG pin:prop[:V]`, `OC pin:prop` (open collector). |
| `uln` | `GND=8;IN 1>16` | Darlington sink outputs. |
| `l293` | `VS=8;GND=4;CH 1:2>3` | H-bridge half: `CH enable:input>output`. |
| `relay` | `COIL 1,2;COM=3;NO=4;NC=5;R=70;I=0.04` | Coil resistance and pull-in current. |
| `opto` | `LED 1>2;SW 4>3` | LED + phototransistor switch. |
| `seg7` | `[CA;]COM=3,8;SEG 7,6,4,2,1,9,10,5` | 7-segment display, segments a–g, dp. |
| `rgbled` | `[CA;]COM=2;R=1;G=3;B=4` | RGB LED. |
| `leds` | `LED 15>16:3.0:20` | Generic LEDs `anode>cathode:Vf:R`. |
| `npn` / `pnp` | `BETA=150;VF=0.65` | Bipolar transistor (pins `B`, `C`, `E`). |
| `nmos` | `VTH=2` | MOSFET switch (pins `G`, `D`, `S`). |
| `amp386`, `tl431` | see catalog | Audio amp, shunt reference. |

Built-in passive models (`resistor`, `capacitor`, `inductor`, `potentiometer`,
`photocell`, `led`, `diode`, `battery`, `switch`, `pushbutton`, `motor`,
`buzzer`, `multimeter`) use fixed pin ids — see the built-in parts.

The simulator runs in real time with 1 ms steps: capacitors charge, 555s and
RC oscillators oscillate, and counters and shift registers clock. Parts with no
model are listed as "Not simulated (pinout only)".

## Compatibility

* Readers must ignore unknown fields. Writers keep the field order shown above.
* When a file's `version` is newer than the reader supports, the reader loads
  what it understands and warns.
* Built-in part types are regenerated from their properties on load. The
  embedded definition is used only when the reader doesn't know the type
  (or the type is a custom part).
