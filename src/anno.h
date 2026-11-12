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

// Point text: the margin kept around the glyphs, as a fraction of the text size
// (world units, so it zooms with the text). A text box has no such margin: its
// frame is the size the user gave it.
constexpr float kPointTextPad = 0.1f;
// A text box never shrinks below this (world units), whoever drags its corner.
constexpr float kTextBoxMinW = 400.f;
constexpr float kTextBoxMinH = 120.f;
// A picture never shrinks below this on either side when one of its handles is
// dragged (world units).
constexpr float kImageMinSide = 60.f;
// The rotation handle sits this many screen pixels above the picture's top edge
// (multiplied by the interface scale; divide by the zoom for world units).
constexpr float kRotateHandlePx = 26.f;
// The rotation handle is not a resizing handle: annoHandleCount() does not count it,
// and dragging it turns the annotation instead of sizing it.
constexpr int kAnnoRotateHandle = 8;

enum class AnnoKind : int { Rect = 0, Line = 1, Circle = 2, Text = 3, Image = 4 };

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
    int widthStep = 0;                      // stroke width step, see annoStrokePx
    std::string text;                       // kind == Text
    float textSize = kNoteTextSize;
    FontFace font = FONT_UI;  // kind == Text: regular or monospaced
    // kind == Text: true = point text (bounds follow the text, no wrapping), false = text box
    // (fixed size, word-wrapped). Older files have only text boxes.
    bool pointText = false;
    // kind == Image: a = top-left corner, b = size (like text). `image` is the content key of the
    // picture in Document::images; `fill` is the backdrop behind transparent pixels (alpha 0 = none);
    // `framed` draws a border with `stroke`, `widthStep` and `dashed`.
    std::string image;
    float rotation = 0;  // degrees, clockwise, about the centre (kind == Image)
    bool framed = false;
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

// kind == Image: the picture's box (a = top-left, b = the size), its centre, and the
// four corners / four edge midpoints in world space — all rotated about the centre by
// Anno::rotation (clockwise degrees). Corner order matches annoHandleAt(): top-left,
// top-right, bottom-right, bottom-left.
Rect annoImageRect(const Anno& a);
Vec2 annoImageCenter(const Anno& a);
Vec2 annoImageCorner(const Anno& a, int index);  // 0..3 (corners), 4..7 (edge midpoints)
// Where the rotation handle sits: above the middle of the top edge, `gap` world units
// out, following the rotation. Callers pass kRotateHandlePx × uiScale / zoom.
Vec2 annoRotateHandleAt(const Anno& a, float gap);

// Resize handles. A rectangle has four corner handles, a line its two ends, a
// circle its centre and a point on the rim, text its two opposite corners and a
// picture four corners plus four edge midpoints (the rotation handle, index 8, is
// handled separately — see kAnnoRotateHandle).
int annoHandleCount(const Anno& a);
Vec2 annoHandleAt(const Anno& a, int index);
// Drags handle `index` to `p`, keeping the rest of the shape where it is.
// `keepRatio` (0 = off) holds a picture's width/height ratio while a handle is
// dragged — Shift; `snapAngle15` snaps a rotation drag to 15° steps.
void annoMoveHandle(Anno& a, int index, Vec2 p, float keepRatio = 0.f, bool snapAngle15 = false);
// Moves the whole annotation by `d`. Text keeps its size: its `b` is a size, not a
// point (pictures are the same).
void annoTranslate(Anno& a, Vec2 d);

Json annoToJson(const Anno& a);
// Reads one annotation. Returns false (and leaves `out` untouched) for an entry
// this build cannot understand — an unknown kind or a missing id.
bool annoFromJson(const Json& j, Anno* out);

// Draws one annotation. The stroke keeps a constant width on screen (2 × uiScale
// pixels) at any zoom, so markup stays readable without ever growing into the
// drawing. `selected` adds the square handles, `hovered` a light outline.
void drawAnno(Gfx& g, const Anno& a, Vec2 pan, float zoom, float uiScale, bool selected, bool hovered);

// Stroke width on screen for the annotation's width step, in screen pixels. It is a
// screen width (times the UI scale), exactly like the old fixed 2 px: markup never
// thickens when the drawing is zoomed. Steps are 0 thin (the old width), 1 medium,
// 2 thick, 3 extra thick; out-of-range values read as the nearest step.
float annoStrokePx(const Anno& a, float uiScale);

// Grab tolerance around the stroke, in screen pixels: at least 10 px (so a shape is
// easy to catch even with a thin stroke), and wider as the stroke gets heavier.
// Callers divide by the zoom to get the world tolerance annoHitTest wants.
float annoHitTolPx(const Anno& a, float uiScale);

// Distance from `world` to the shape's stroke, in world units (1e30 for text, which
// is a box rather than a stroke). The hit test and the hit priority both use it, so
// the two can never disagree about which shape is nearest.
float annoStrokeDist(const Anno& a, Vec2 world);

// True when `world` is close enough to the annotation's shape. `tol` is a world
// distance: callers pass a pixel tolerance divided by the zoom.
bool annoHitTest(const Anno& a, Vec2 world, float tol);
// True when `world` is on any resize handle; *index receives which one.
bool annoHitHandle(const Anno& a, Vec2 world, float tol, int* index = nullptr);

// Text editing support: replaces the text. A text box keeps its size; a point text
// is re-measured by annoFitText().
void annoSetText(Anno& a, const std::string& text);
// Text style: bumps the size. A text box keeps its frame (the glyphs get clipped by
// it); a point text follows through annoFitText().
void annoBumpTextSize(Anno& a, float delta);

// Point text only: sets `b` so the box hugs the glyphs — width = the widest line
// (measured with `g` in world units, the way drawAnno() scales it), height = line
// count × line height, plus kPointTextPad around both. Lines are only split at
// '\n': a point text never wraps. Does nothing to a text box, whose size the user
// owns. Cheap enough to call every frame before drawing and hit-testing.
void annoFitText(Anno& a, Gfx& g);
