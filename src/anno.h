// designElectronics — drawing annotations: rectangles, lines, circles and text
// placed over a design in any view.
//
// Annotations are pure markup. They are never connected, never simulated and
// never leave the editor (no netlist, DRC or Gerber treats them as copper), so
// a circle drawn over a pin can't create a net.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "geom.h"
#include "gfx.h"
#include "json.h"
#include "partlib.h"

// Default text size for notes and annotations (world units). The text is drawn
// at size × zoom, so it grows with the drawing like the box around it.
constexpr float kNoteTextSize = 108;

enum class AnnoKind : int { Rect = 0, Line = 1, Circle = 2, Text = 3 };

struct Anno {
    int id = 0;
    int view = BB;
    AnnoKind kind = AnnoKind::Rect;
    // rect/line: start and end point. circle: centre and a point on the circle
    // (the radius handle). text: top-left corner and the box size.
    Vec2 a, b;
    uint32_t stroke = rgba(120, 124, 130);  // neutral grey: markup is not a net colour
    uint32_t fill = 0;                      // text background, alpha 0 = none
    bool dashed = true;
    int widthStep = 0;                      // 0/1/2 — one stroke width for now, saved for later
    std::string text;                       // kind == Text
    float textSize = kNoteTextSize;
    bool hidden = false;                    // hidden by the Layers panel
};

// The colours offered for annotation strokes. Greys and one muted accent, kept
// apart from the net palette (blue/red/black/yellow/green) on purpose.
const std::vector<uint32_t>& annoPalette();

// "rect" / "line" / "circle" / "text" — the value stored in .dez files.
const char* annoKindKey(AnnoKind k);
bool annoKindFromKey(const std::string& key, AnnoKind* out);
std::string annoKindLabel(AnnoKind k);  // translated, for the interface

// World bounding box, used for fitting the view and for rubber-band selection.
Rect annoBounds(const Anno& a);

// Resize handles. A rectangle has four corner handles, a line its two ends, a
// circle its centre and a point on the rim, and text its two opposite corners.
int annoHandleCount(const Anno& a);
Vec2 annoHandleAt(const Anno& a, int index);
// Drags handle `index` to `p`, keeping the rest of the shape where it is.
void annoMoveHandle(Anno& a, int index, Vec2 p);

Json annoToJson(const Anno& a);
// Reads one annotation. Returns false (and leaves `out` untouched) for an entry
// this build cannot understand — an unknown kind or a missing id.
bool annoFromJson(const Json& j, Anno* out);

// Draws one annotation. The stroke keeps a constant width on screen (2 × uiScale
// pixels) at any zoom, so markup stays readable without ever growing into the
// drawing. `selected` adds the square handles, `hovered` a light outline.
void drawAnno(Gfx& g, const Anno& a, Vec2 pan, float zoom, float uiScale, bool selected, bool hovered);

// True when `world` is close enough to the annotation's shape. `tol` is a world
// distance: callers pass a pixel tolerance divided by the zoom.
bool annoHitTest(const Anno& a, Vec2 world, float tol);
// True when `world` is on any resize handle; *index receives which one.
bool annoHitHandle(const Anno& a, Vec2 world, float tol, int* index = nullptr);

// Text editing support: replaces the text and grows the box with the lines.
void annoSetText(Anno& a, const std::string& text);
// Text style: bumps the size (both the drawing and the box follow).
void annoBumpTextSize(Anno& a, float delta);
