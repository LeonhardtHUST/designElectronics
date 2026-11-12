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

}  // namespace

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
    }
    return "rect";
}

bool annoKindFromKey(const std::string& key, AnnoKind* out) {
    AnnoKind k;
    if (key == "rect") k = AnnoKind::Rect;
    else if (key == "line") k = AnnoKind::Line;
    else if (key == "circle") k = AnnoKind::Circle;
    else if (key == "text") k = AnnoKind::Text;
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
    }
    return tr("anno.kind.rect");
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
    a.b = pointOf(j["b"], kind == AnnoKind::Text ? Vec2(1200, 160) : a.a + Vec2(600, 0));
    a.stroke = parseColor(j["stroke"].str(), a.stroke);
    a.fill = parseColor(j["fill"].str(), 0);
    a.dashed = j["dashed"].boolean(true);
    a.widthStep = std::clamp(j["widthStep"].i(0), 0, 2);
    a.text = j["text"].str();
    a.textSize = j.has("textSize") ? std::clamp(j["textSize"].f(kNoteTextSize), 24.f, 400.f) : kNoteTextSize;
    a.hidden = j["hidden"].boolean();
    if (a.kind == AnnoKind::Text && a.b.x <= 0) a.b.x = 1200;
    if (a.kind == AnnoKind::Text && a.b.y <= 0) a.b.y = lineHeight(a) * textLineCount(a.text);
    *out = a;
    return true;
}

void annoSetText(Anno& a, const std::string& text) {
    a.text = text;
    if (a.kind != AnnoKind::Text) return;
    a.b.y = std::max(1, textLineCount(text)) * lineHeight(a);
    if (a.b.x <= 0) a.b.x = 1200;
}

void annoBumpTextSize(Anno& a, float delta) {
    a.textSize = std::clamp(a.textSize + delta, 32.f, 400.f);
    if (a.kind == AnnoKind::Text) a.b.y = std::max(1, textLineCount(a.text)) * lineHeight(a);
}

int annoHandleCount(const Anno& a) { return a.kind == AnnoKind::Rect ? 4 : 2; }

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
    return index == 1 ? a.b : a.a;
}

void annoMoveHandle(Anno& a, int index, Vec2 p) {
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
        // Text handles resize the box, they never flip it.
        a.b = Vec2(std::max(400.f, p.x - a.a.x), std::max(120.f, p.y - a.a.y));
        return;
    }
    if (index == 0) a.a = p;  // line start, circle centre
    else a.b = p;             // line end, circle radius point
}

bool annoHitTest(const Anno& a, Vec2 w, float tol) {
    switch (a.kind) {
        case AnnoKind::Line:
            return distPointSeg(w, a.a, a.b) <= tol;
        case AnnoKind::Circle: {
            float r = dist(a.a, a.b);
            if (std::fabs(dist(w, a.a) - r) <= tol) return true;
            return (a.fill & 0xFF) != 0 && dist(w, a.a) <= r;
        }
        case AnnoKind::Text:
            return Rect(a.a.x, a.a.y, std::max(1.f, a.b.x), std::max(1.f, a.b.y)).inset(-tol).contains(w);
        case AnnoKind::Rect:
        default: {
            Rect r = Rect::fromPoints(a.a, a.b);
            if (distPointSeg(w, {r.x, r.y}, {r.r(), r.y}) <= tol) return true;
            if (distPointSeg(w, {r.r(), r.y}, {r.r(), r.b()}) <= tol) return true;
            if (distPointSeg(w, {r.r(), r.b()}, {r.x, r.b()}) <= tol) return true;
            if (distPointSeg(w, {r.x, r.b()}, {r.x, r.y}) <= tol) return true;
            return (a.fill & 0xFF) != 0 && r.contains(w);
        }
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
    const float width = kStrokePx * s;  // constant on screen: markup never thickens with zoom
    // Copper has no markup, so an annotation drawn on the PCB is always dashed:
    // a solid line there would read as a trace.
    const bool dashed = a.dashed || a.view == PCB;
    const uint32_t col = a.stroke;
    auto seg = [&](Vec2 p0, Vec2 p1) {
        if (dist(p0, p1) < 0.5f) return;
        if (dashed) g.dashedLine(p0, p1, width, 7 * s, 5 * s, col);
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
                int n = std::clamp((int)(r / (6 * s)), 8, 120);
                for (int i = 0; i < n; i++) {
                    float a0 = (float)i / n * 2 * kPi, a1 = a0 + (2 * kPi / n) * 0.6f;
                    g.line(c + Vec2(std::cos(a0), std::sin(a0)) * r, c + Vec2(std::cos(a1), std::sin(a1)) * r, width, col);
                }
            } else {
                g.strokeCircle(c, r, width, col);
            }
            break;
        }
        case AnnoKind::Text: {
            float px = a.textSize * zoom;
            if (px < 4.5f || a.text.empty()) break;
            float boxW = std::max(1.f, a.b.x) * zoom;
            auto lines = wrapText(g, a.text, px, std::max(px * 2, boxW - 8 * s));
            float lh = px * 1.35f;
            Vec2 o = S(a.a);
            float bottom = o.y + std::max(1.f, a.b.y) * zoom;
            for (size_t i = 0; i < lines.size(); i++) {
                float y = o.y + lh * ((float)i + 0.5f) + 2 * s;
                if (y - lh * 0.5f > bottom) break;  // the box clips the text, as on screen
                g.text({o.x + 4 * s, y}, lines[i], px, col, 0);
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
