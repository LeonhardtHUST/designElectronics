// designElectronics — drawing annotations (see anno.h).
#include "anno.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "i18n.h"
#include "ui.h"

namespace {

constexpr float kStrokePx = 2.0f;  // stroke width on screen, times the UI scale
const float kPi = 3.14159265358979323846f;
// Multiplier per width step: thin (the old width), medium, thick, extra thick.
const float kWidthMul[4] = {1.f, 1.8f, 2.8f, 4.f};

Json pointJson(Vec2 v) {
    Json a = Json::array();
    a.push(Json((double)v.x));
    a.push(Json((double)v.y));
    return a;
}

Vec2 pointOf(const Json& j, Vec2 def) {
    if (!j.isArray() || j.size() < 2) return def;
    return {j[(size_t)0].f(def.x), j[(size_t)1].f(def.y)};
}

float lineHeight(const Anno& a) { return std::max(6.f, a.textSize * 1.35f); }

int textLineCount(const std::string& text) {
    int n = 1;
    for (char c : text) n += c == '\n' ? 1 : 0;
    return n;
}

// Splits at '\n' only: a point text is never wrapped, a text box wraps each of these.
std::vector<std::string> textLines(const std::string& text) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        out.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) return out;
        start = nl + 1;
    }
}

// ---- picture geometry: a rotated box ---------------------------------------
// The y axis points down, so a positive angle turns clockwise on screen, which is
// what Anno::rotation documents.
Vec2 rotAbout(Vec2 v, float deg) {
    if (deg == 0) return v;
    const float r = deg * kPi / 180.f, c = std::cos(r), s = std::sin(r);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

float normalizeDeg(float d) {
    d = std::fmod(d, 360.f);
    if (d <= -180.f) d += 360.f;
    if (d > 180.f) d -= 360.f;
    return d;
}

Vec2 imageSize(const Anno& a) { return {std::max(1.f, a.b.x), std::max(1.f, a.b.y)}; }
Vec2 imageCentre(const Anno& a) { return a.a + imageSize(a) / 2; }

// A point in the picture's own frame: origin at its top-left corner, axes before the
// rotation. Distances are preserved (a rotation is an isometry), so a hit test can be
// done there with plain rect maths.
Vec2 imageLocalPoint(const Anno& a, Vec2 w) {
    return rotAbout(w - imageCentre(a), -a.rotation) + imageSize(a) / 2;
}

Vec2 imageWorldPoint(const Anno& a, Vec2 local) {
    return imageCentre(a) + rotAbout(local - imageSize(a) / 2, a.rotation);
}

// Handle positions in the picture's own frame, in the same order as annoHandleAt():
// four corners, then the middle of the top, right, bottom and left edges.
Vec2 imageLocalHandle(const Anno& a, int index) {
    const Vec2 sz = imageSize(a);
    switch (index) {
        case 0: return {0, 0};
        case 1: return {sz.x, 0};
        case 2: return {sz.x, sz.y};
        case 3: return {0, sz.y};
        case 4: return {sz.x / 2, 0};
        case 5: return {sz.x, sz.y / 2};
        case 6: return {sz.x / 2, sz.y};
        case 7: return {0, sz.y / 2};
        default: return {sz.x / 2, sz.y / 2};
    }
}

}  // namespace

Rect annoImageRect(const Anno& a) { return Rect(a.a.x, a.a.y, imageSize(a).x, imageSize(a).y); }

Vec2 annoImageCenter(const Anno& a) { return imageCentre(a); }

Vec2 annoImageCorner(const Anno& a, int index) {
    // Resizing handles are 0..7 (corners and edge midpoints); 8 is the rotation handle,
    // which the caller places with annoRotateHandleAt().
    if (index < 0 || index > 7) return imageCentre(a);
    return imageWorldPoint(a, imageLocalHandle(a, index));
}

Vec2 annoRotateHandleAt(const Anno& a, float gap) {
    const Vec2 sz = imageSize(a);
    // Above the middle of the top edge, `gap` world units further out on the same line
    // through the centre — so it follows the rotation like every other handle.
    const Vec2 local = Vec2(sz.x / 2, -gap);
    return imageWorldPoint(a, local);
}

const std::vector<uint32_t>& annoPalette() {
    static const std::vector<uint32_t> p = {
        rgba(120, 124, 130),  // neutral grey (default)
        rgba(60, 60, 64),     // ink
        rgba(28, 116, 222),   // note blue
        rgba(208, 52, 52),    // warning red
        rgba(40, 150, 80),    // ok green
        rgba(235, 140, 30),   // highlight orange
    };
    return p;
}

const char* annoKindKey(AnnoKind k) {
    switch (k) {
        case AnnoKind::Rect: return "rect";
        case AnnoKind::Line: return "line";
        case AnnoKind::Circle: return "circle";
        case AnnoKind::Text: return "text";
        case AnnoKind::Image: return "image";
    }
    return "rect";
}

bool annoKindFromKey(const std::string& key, AnnoKind* out) {
    AnnoKind k;
    if (key == "rect") k = AnnoKind::Rect;
    else if (key == "line") k = AnnoKind::Line;
    else if (key == "circle") k = AnnoKind::Circle;
    else if (key == "text") k = AnnoKind::Text;
    else if (key == "image") k = AnnoKind::Image;
    else return false;
    if (out) *out = k;
    return true;
}

std::string annoKindLabel(AnnoKind k) {
    switch (k) {
        case AnnoKind::Rect: return tr("anno.kind.rect");
        case AnnoKind::Line: return tr("anno.kind.line");
        case AnnoKind::Circle: return tr("anno.kind.circle");
        case AnnoKind::Text: return tr("anno.kind.text");
        case AnnoKind::Image: return tr("anno.kind.image");
    }
    return tr("anno.kind.rect");
}

float annoStrokePx(const Anno& a, float uiScale) {
    const float s = uiScale > 0 ? uiScale : 1.f;
    int step = std::clamp(a.widthStep, 0, 3);
    return kStrokePx * kWidthMul[step] * s;
}

float annoHitTolPx(const Anno& a, float uiScale) {
    const float s = uiScale > 0 ? uiScale : 1.f;
    // A heavy outline is grabbable across its own half width plus a small margin;
    // a thin one still gets 10 px, so catching a shape is never a pixel hunt.
    return std::max(10.f * s, annoStrokePx(a, uiScale) * 0.5f + 6.f * s);
}

float annoStrokeDist(const Anno& a, Vec2 w) {
    switch (a.kind) {
        case AnnoKind::Line:
            return distPointSeg(w, a.a, a.b);
        case AnnoKind::Circle:
            return std::fabs(dist(w, a.a) - dist(a.a, a.b));
        case AnnoKind::Rect: {
            Rect r = Rect::fromPoints(a.a, a.b);
            float d = distPointSeg(w, {r.x, r.y}, {r.r(), r.y});
            d = std::min(d, distPointSeg(w, {r.r(), r.y}, {r.r(), r.b()}));
            d = std::min(d, distPointSeg(w, {r.r(), r.b()}, {r.x, r.b()}));
            d = std::min(d, distPointSeg(w, {r.x, r.b()}, {r.x, r.y}));
            return d;
        }
        default:
            return 1e30f;  // text and images are boxes, not strokes
    }
}

Rect annoBounds(const Anno& a) {
    switch (a.kind) {
        case AnnoKind::Line:
            return Rect::fromPoints(a.a, a.b).inset(-kStrokePx * 8);
        case AnnoKind::Circle: {
            float r = dist(a.a, a.b);
            return Rect(a.a.x - r, a.a.y - r, 2 * r, 2 * r);
        }
        case AnnoKind::Text:
            return Rect(a.a.x, a.a.y, std::max(1.f, a.b.x), std::max(1.f, a.b.y));
        case AnnoKind::Image: {
            // The bounding box of the rotated picture: a turned picture takes more room
            // than its unrotated box, and selection, fitting and rubber-band need that.
            Rect r = annoImageRect(a);
            if (a.rotation == 0) return r;
            Vec2 p0 = annoImageCorner(a, 0), p1 = annoImageCorner(a, 1), p2 = annoImageCorner(a, 2), p3 = annoImageCorner(a, 3);
            float x0 = std::min(std::min(p0.x, p1.x), std::min(p2.x, p3.x));
            float y0 = std::min(std::min(p0.y, p1.y), std::min(p2.y, p3.y));
            float x1 = std::max(std::max(p0.x, p1.x), std::max(p2.x, p3.x));
            float y1 = std::max(std::max(p0.y, p1.y), std::max(p2.y, p3.y));
            return Rect(x0, y0, x1 - x0, y1 - y0);
        }
        case AnnoKind::Rect:
        default:
            return Rect::fromPoints(a.a, a.b);
    }
}

Json annoToJson(const Anno& a) {
    Json o = Json::object();
    o.set("id", a.id);
    o.set("view", viewKey(a.view));
    o.set("kind", annoKindKey(a.kind));
    o.set("a", pointJson(a.a));
    o.set("b", pointJson(a.b));
    o.set("stroke", colorToHex(a.stroke));
    if (a.fill & 0xFF) o.set("fill", colorToHex(a.fill));
    o.set("dashed", a.dashed);
    if (a.widthStep) o.set("widthStep", a.widthStep);
    if (!a.text.empty()) o.set("text", a.text);
    if (std::fabs(a.textSize - kNoteTextSize) > 0.01f) o.set("textSize", (double)a.textSize);
    if (a.kind == AnnoKind::Text && a.font == FONT_MONO) o.set("font", "mono");
    // Only point text is written: an entry without "point" is a text box, which is what
    // every older file holds.
    if (a.kind == AnnoKind::Text && a.pointText) o.set("point", true);
    // A picture is a content key plus a rotation and an optional frame; the bytes live
    // once in the document's picture store (see docs/FILE_FORMAT.md).
    if (a.kind == AnnoKind::Image) {
        if (!a.image.empty()) o.set("image", a.image);
        if (std::fabs(a.rotation) > 0.001f) o.set("rotation", (double)a.rotation);
        if (a.framed) o.set("framed", true);
    }
    if (a.hidden) o.set("hidden", true);
    return o;
}

bool annoFromJson(const Json& j, Anno* out) {
    if (!j.isObject() || !out) return false;
    AnnoKind kind;
    if (!annoKindFromKey(j["kind"].str("rect"), &kind)) {
        // A newer build may have added kinds: skip that one, keep the rest.
        return false;
    }
    Anno a;
    a.id = j["id"].i();
    if (a.id <= 0) return false;
    a.kind = kind;
    for (int v = 0; v < NUM_VIEWS; v++)
        if (j["view"].str("breadboard") == viewKey(v)) a.view = v;
    a.a = pointOf(j["a"], Vec2());
    a.b = pointOf(j["b"], kind == AnnoKind::Text || kind == AnnoKind::Image ? Vec2(1200, 160) : a.a + Vec2(600, 0));
    a.stroke = parseColor(j["stroke"].str(), a.stroke);
    a.fill = parseColor(j["fill"].str(), 0);
    // A picture has no dashed outline unless the user framed it, so an entry with no
    // "dashed" field reads as a plain picture rather than as a dashed shape.
    a.dashed = j["dashed"].boolean(kind != AnnoKind::Image);
    a.widthStep = std::clamp(j["widthStep"].i(0), 0, 3);
    a.text = j["text"].str();
    a.textSize = j.has("textSize") ? std::clamp(j["textSize"].f(kNoteTextSize), 24.f, 400.f) : kNoteTextSize;
    a.hidden = j["hidden"].boolean();
    a.font = j["font"].str() == "mono" ? FONT_MONO : FONT_UI;  // anything else reads as the regular face
    // Files written before point text existed have no "point": they load as text boxes,
    // which is exactly what they were.
    a.pointText = a.kind == AnnoKind::Text && j["point"].boolean(false);
    a.image = kind == AnnoKind::Image ? j["image"].str() : std::string();
    a.rotation = kind == AnnoKind::Image ? normalizeDeg(j["rotation"].f(0.f)) : 0.f;
    a.framed = kind == AnnoKind::Image && j["framed"].boolean(false);
    if (a.kind == AnnoKind::Text && a.b.x <= 0) a.b.x = 1200;
    if (a.kind == AnnoKind::Text && a.b.y <= 0) a.b.y = lineHeight(a) * textLineCount(a.text);
    // A picture's b is its size: a missing or nonsense one gets a usable default rather
    // than a degenerate box.
    if (a.kind == AnnoKind::Image && (a.b.x <= 0 || a.b.y <= 0)) a.b = Vec2(1200, 900);
    *out = a;
    return true;
}

void annoSetText(Anno& a, const std::string& text) {
    a.text = text;
    // A point text is measured around its glyphs by annoFitText(); a text box keeps the
    // size the user gave it. Only a box with no height yet (a brand new one) gets a
    // usable default, so the tools can create text with annoSetText alone.
    if (a.kind != AnnoKind::Text || a.pointText) return;
    if (a.b.x <= 0) a.b.x = 1200;
    if (a.b.y <= 0) a.b.y = std::max(1, textLineCount(text)) * lineHeight(a);
}

void annoBumpTextSize(Anno& a, float delta) {
    // The size only: a text box keeps its frame (drawAnno clips the glyphs to it) and a
    // point text has its box re-measured by annoFitText().
    a.textSize = std::clamp(a.textSize + delta, 32.f, 400.f);
}

void annoFitText(Anno& a, Gfx& g) {
    if (a.kind != AnnoKind::Text || !a.pointText) return;
    float widest = 0;
    int lines = 0;
    for (const std::string& line : textLines(a.text)) {
        widest = std::max(widest, g.textWidth(line, a.textSize, a.font));
        lines++;
    }
    const float pad = kPointTextPad * a.textSize;
    a.b = Vec2(std::max(1.f, widest + 2 * pad), std::max(1.f, (float)std::max(1, lines) * lineHeight(a) + 2 * pad));
}

int annoHandleCount(const Anno& a) {
    if (a.kind == AnnoKind::Rect) return 4;
    if (a.kind == AnnoKind::Image) return 8;  // four corners plus four edge midpoints
    return 2;
}

Vec2 annoHandleAt(const Anno& a, int index) {
    if (a.kind == AnnoKind::Rect) {
        // Corner order matches the drag directions: a, top-right, b, bottom-left.
        Rect r = Rect::fromPoints(a.a, a.b);
        switch (index) {
            case 1: return {r.r(), r.y};
            case 2: return {r.r(), r.b()};
            case 3: return {r.x, r.b()};
            default: return {r.x, r.y};
        }
    }
    if (a.kind == AnnoKind::Image) return annoImageCorner(a, index);
    return index == 1 ? (a.kind == AnnoKind::Text ? a.a + a.b : a.b) : a.a;  // text: b is the box size
}

void annoTranslate(Anno& a, Vec2 d) {
    a.a += d;
    // Text and pictures keep their size: their b is the box size, not a second point.
    if (a.kind != AnnoKind::Text && a.kind != AnnoKind::Image) a.b += d;
}

// The picture's handle drag: work in the picture's own frame, then put the result back
// rotated. Corner 0..3 keeps the opposite corner of the diagonal fixed in the world,
// 4..7 move a single edge, 8 turns the picture about its centre.
void annoMoveImageHandle(Anno& a, int index, Vec2 p, float keepRatio, bool snapAngle15) {
    if (index == kAnnoRotateHandle) {
        Vec2 d = p - imageCentre(a);
        if (!(std::fabs(d.x) > 1e-4f || std::fabs(d.y) > 1e-4f)) return;
        // At rotation 0 the handle points straight up (-90° in screen angles).
        float deg = (float)(std::atan2(d.y, d.x) * 180.0 / 3.14159265358979) + 90.f;
        if (snapAngle15) deg = std::round(deg / 15.f) * 15.f;
        a.rotation = normalizeDeg(deg);
        return;
    }
    const Vec2 old = imageSize(a);
    // The dragged point in the picture's own frame — and where the point opposite it sits.
    Vec2 q = imageLocalPoint(a, p);
    Vec2 fixed;
    int dirX = 0, dirY = 0;  // from the fixed side towards the dragged side
    switch (index) {
        case 0: fixed = {old.x, old.y}; dirX = -1; dirY = -1; break;  // top-left: bottom-right stays
        case 1: fixed = {0, old.y}; dirX = +1; dirY = -1; break;      // top-right: bottom-left stays
        case 2: fixed = {0, 0}; dirX = +1; dirY = +1; break;          // bottom-right: top-left stays
        case 3: fixed = {old.x, 0}; dirX = -1; dirY = +1; break;      // bottom-left: top-right stays
        case 4: fixed = {old.x / 2, old.y}; dirY = -1; break;         // top edge: bottom edge stays
        case 5: fixed = {0, old.y / 2}; dirX = +1; break;             // right edge: left edge stays
        case 6: fixed = {old.x / 2, 0}; dirY = +1; break;             // bottom edge: top edge stays
        case 7: fixed = {old.x, old.y / 2}; dirX = -1; break;         // left edge: right edge stays
        default: return;
    }
    float sx = std::fabs(q.x - fixed.x), sy = std::fabs(q.y - fixed.y);
    if (dirX == 0) sx = old.x;  // an edge drag keeps the other dimension
    if (dirY == 0) sy = old.y;
    if (keepRatio > 0) {
        // Shift: hold the width/height ratio the picture had when the drag began. The
        // side the pointer moved further, in ratio terms, decides the scale.
        float w = std::max(sx, sy * keepRatio);
        sx = w;
        sy = w / keepRatio;
        if (sx < kImageMinSide || sy < kImageMinSide) {  // keep the ratio even at the floor
            float k = kImageMinSide / std::min(sx, sy);
            sx *= k;
            sy *= k;
        }
    }
    sx = std::max(kImageMinSide, sx);
    sy = std::max(kImageMinSide, sy);
    Vec2 sizeNew(sx, sy);
    // The fixed point keeps its place in the world: hang the new box off it, along the
    // direction the dragged side lies in, then put the result back into world space.
    Vec2 fixedWorld = imageWorldPoint(a, fixed);
    Vec2 centreNew = fixedWorld + rotAbout(Vec2((float)dirX * sx / 2, (float)dirY * sy / 2), a.rotation);
    a.b = sizeNew;
    a.a = centreNew - sizeNew / 2;
}

void annoMoveHandle(Anno& a, int index, Vec2 p, float keepRatio, bool snapAngle15) {
    if (a.kind == AnnoKind::Image) {
        annoMoveImageHandle(a, index, p, keepRatio, snapAngle15);
        return;
    }
    if (a.kind == AnnoKind::Rect) {
        // The corner opposite the one being dragged stays put.
        Rect r = Rect::fromPoints(a.a, a.b);
        Vec2 tl = {r.x, r.y}, br = {r.r(), r.b()};
        switch (index) {
            case 1: tl.y = p.y; br.x = p.x; break;  // top-right
            case 2: br = p; break;                  // bottom-right
            case 3: tl.x = p.x; br.y = p.y; break;  // bottom-left
            default: tl = p; break;                 // top-left
        }
        a.a = tl;
        a.b = br;
        return;
    }
    if (a.kind == AnnoKind::Circle && index == 0) {
        // The centre handle moves the circle; the rim handle changes the radius.
        Vec2 d = p - a.a;
        a.a = p;
        a.b += d;
        return;
    }
    if (a.kind == AnnoKind::Text && index == 1) {
        if (a.pointText) {
            // A point text has no frame of its own: the corner handle scales the glyphs
            // proportionally and annoFitText() re-measures the box around them. No
            // wrapping is introduced, the line breaks stay exactly as typed.
            Vec2 old = Vec2(std::max(1.f, a.b.x), std::max(1.f, a.b.y));
            Vec2 want(std::max(1.f, p.x - a.a.x), std::max(1.f, p.y - a.a.y));
            float k = std::max(0.05f, std::max(want.x / old.x, want.y / old.y));
            float size = std::clamp(a.textSize * k, 32.f, 400.f);
            k = size / a.textSize;
            a.textSize = size;
            a.b = old * k;  // close enough until the next fit measures the real glyphs
            return;
        }
        // A text box's handles resize the frame, they never flip it.
        a.b = Vec2(std::max(kTextBoxMinW, p.x - a.a.x), std::max(kTextBoxMinH, p.y - a.a.y));
        return;
    }
    if (index == 0) a.a = p;  // line start, circle centre
    else a.b = p;             // line end, circle radius point
}

bool annoHitTest(const Anno& a, Vec2 w, float tol) {
    switch (a.kind) {
        case AnnoKind::Text:
            return Rect(a.a.x, a.a.y, std::max(1.f, a.b.x), std::max(1.f, a.b.y)).inset(-tol).contains(w);
        case AnnoKind::Image: {
            // A picture is a solid box: work in its own frame (rotation keeps distances),
            // so the whole picture is grabbable and a turned one is hit correctly.
            Vec2 local = imageLocalPoint(a, w);
            return Rect(0, 0, imageSize(a).x, imageSize(a).y).inset(-tol).contains(local);
        }
        case AnnoKind::Circle:
            if (annoStrokeDist(a, w) <= tol) return true;
            return (a.fill & 0xFF) != 0 && dist(w, a.a) <= dist(a.a, a.b);
        case AnnoKind::Rect:
            if (annoStrokeDist(a, w) <= tol) return true;
            return (a.fill & 0xFF) != 0 && Rect::fromPoints(a.a, a.b).contains(w);
        case AnnoKind::Line:
        default:
            return annoStrokeDist(a, w) <= tol;
    }
}

bool annoHitHandle(const Anno& a, Vec2 w, float tol, int* index) {
    int n = annoHandleCount(a);
    int best = -1;
    float bestD = 1e30f;
    for (int i = 0; i < n; i++) {
        float d = dist(annoHandleAt(a, i), w);
        if (d <= std::max(tol, 1.f) && d < bestD) {
            bestD = d;
            best = i;
        }
    }
    if (index) *index = best;
    return best >= 0;
}

void drawAnno(Gfx& g, const Anno& a, Vec2 pan, float zoom, float uiScale, bool selected, bool hovered) {
    auto S = [&](Vec2 w) { return pan + w * zoom; };
    const float s = uiScale > 0 ? uiScale : 1.f;
    const float width = annoStrokePx(a, uiScale);  // constant on screen: markup never thickens with zoom
    // Dashes grow a little with the stroke, so a heavy outline reads as one dashed
    // line instead of a row of blocks. At the thin step this is the old 7/5 px.
    const float dash = std::max(7 * s, width * 3.5f), gap = std::max(5 * s, width * 2.5f);
    // Copper has no markup, so an annotation drawn on the PCB is always dashed:
    // a solid line there would read as a trace.
    const bool dashed = a.dashed || a.view == PCB;
    const uint32_t col = a.stroke;
    auto seg = [&](Vec2 p0, Vec2 p1) {
        if (dist(p0, p1) < 0.5f) return;
        if (dashed) g.dashedLine(p0, p1, width, dash, gap, col);
        else g.line(p0, p1, width, col);
    };

    if (a.kind == AnnoKind::Text && (a.fill & 0xFF)) {
        Rect box = Rect::fromPoints(S(a.a), S(a.a + Vec2(std::max(1.f, a.b.x), std::max(1.f, a.b.y))));
        g.fillRoundRect(box, 4 * s, a.fill);
    }
    switch (a.kind) {
        case AnnoKind::Rect: {
            Rect r = Rect::fromPoints(S(a.a), S(a.b));
            if (dashed) {
                seg({r.x, r.y}, {r.r(), r.y});
                seg({r.r(), r.y}, {r.r(), r.b()});
                seg({r.r(), r.b()}, {r.x, r.b()});
                seg({r.x, r.b()}, {r.x, r.y});
            } else if (r.w > 0.2f && r.h > 0.2f) {
                g.strokeRect(r, width, col);
            }
            break;
        }
        case AnnoKind::Line:
            seg(S(a.a), S(a.b));
            break;
        case AnnoKind::Circle: {
            Vec2 c = S(a.a);
            float r = dist(a.a, a.b) * zoom;
            if (r < 0.7f) break;
            if (dashed) {
                int n = std::clamp((int)(r / std::max(6 * s, (dash + gap) * 0.5f)), 8, 120);
                for (int i = 0; i < n; i++) {
                    float a0 = (float)i / n * 2 * kPi, a1 = a0 + (2 * kPi / n) * 0.6f;
                    g.line(c + Vec2(std::cos(a0), std::sin(a0)) * r, c + Vec2(std::cos(a1), std::sin(a1)) * r, width, col);
                }
            } else {
                g.strokeCircle(c, r, width, col);
            }
            break;
        }
        case AnnoKind::Image:
            // Pictures are drawn by the canvas (they need the texture cache and the
            // renderer), so nothing shape-like is left to do here.
            break;
        case AnnoKind::Text: {
            float px = a.textSize * zoom;
            if (px < 4.5f || a.text.empty()) break;
            std::vector<std::string> lines;
            float xoff, yoff;
            if (a.pointText) {
                // Point text is never wrapped: what was typed is what is shown, one line
                // per '\n', and annoFitText() measured the box around exactly that.
                lines = textLines(a.text);
                xoff = yoff = kPointTextPad * px;
            } else {
                float boxW = std::max(1.f, a.b.x) * zoom;
                lines = wrapText(g, a.text, px, std::max(px * 2, boxW - 8 * s), a.font);
                xoff = 4 * s;
                yoff = 2 * s;
            }
            float lh = px * 1.35f;
            Vec2 o = S(a.a);
            float bottom = o.y + std::max(1.f, a.b.y) * zoom;
            for (size_t i = 0; i < lines.size(); i++) {
                float y = o.y + yoff + lh * ((float)i + 0.5f);
                if (y - lh * 0.5f > bottom) break;  // the box clips the text, as on screen
                g.text({o.x + xoff, y}, lines[i], px, col, 0, a.font);
            }
            break;
        }
    }

    if (hovered && !selected) {
        Rect b = annoBounds(a);
        if (b.w > 0 && b.h > 0) g.strokeRect(Rect::fromPoints(S({b.x, b.y}), S({b.r(), b.b()})).inset(-2 * s), std::max(1.f, s), withAlpha(rgba(28, 116, 222), 90));
    }
    if (selected) {
        Rect b = annoBounds(a);
        if (b.w > 0 && b.h > 0) g.strokeRect(Rect::fromPoints(S({b.x, b.y}), S({b.r(), b.b()})).inset(-3 * s), std::max(1.f, s), withAlpha(rgba(28, 116, 222), 170));
        const float hh = 9 * s;
        int n = annoHandleCount(a);
        for (int i = 0; i < n; i++) {
            Vec2 c = S(annoHandleAt(a, i));
            Rect h(c.x - hh / 2, c.y - hh / 2, hh, hh);
            g.fillRect(h, rgba(255, 255, 255));
            g.strokeRect(h, std::max(1.f, s), rgba(28, 116, 222));
        }
    }
}
