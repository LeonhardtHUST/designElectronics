// Canvas: rendering of the breadboard / schematic / PCB views and all pointer
// interaction (select, move, wire, bend, pan, pinch-zoom, rubber band).
#include <algorithm>
#include <cmath>

#include "app.h"
#include "i18n.h"

namespace {
const uint32_t kBgBB = rgba(246, 246, 244), kBgSCH = rgba(255, 255, 255), kBgPCB = rgba(70, 76, 84);
const uint32_t kCopper = rgba(233, 185, 73), kTraceTop = rgba(242, 163, 58), kTraceBot = rgba(217, 183, 60);
const uint32_t kSel = rgba(28, 116, 222);

uint32_t darker(uint32_t c, float f) {
    return rgba((int)(((c >> 24) & 0xFF) * f), (int)(((c >> 16) & 0xFF) * f), (int)(((c >> 8) & 0xFF) * f), (int)(c & 0xFF));
}

Rect worldExtent(const Document& doc, int v, bool* any) {
    Rect b;
    *any = false;
    auto add = [&](Rect r) { b = *any ? b.united(r) : r; *any = true; };
    for (auto& p : doc.parts) if (p.inView(v)) add(p.worldBounds(v));
    for (auto& w : doc.wires) if (w.view == v) for (auto& pt : w.pts) add(Rect(pt.x, pt.y, 0, 0));
    for (auto& n : doc.notes) if (n.view == v) add(Rect(n.pos.x, n.pos.y, n.size.x, n.size.y));
    // Markup counts as content: fitting the view must not clip an annotation away.
    for (auto& a : doc.annos) if (a.view == v && !a.hidden) add(annoBounds(a));
    return b;
}
}  // namespace

void App::fitView(int v, Rect vp) {
    bool any;
    Rect b = worldExtent(doc, v, &any);
    if (!any) b = Rect(-500, -500, 3500, 2500);
    b = Rect(b.x - 150, b.y - 150, b.w + 300, b.h + 300);
    Camera& c = cam[v];
    c.zoom = std::clamp(std::min(vp.w / b.w, vp.h / b.h) * 0.92f, 0.01f, 2.0f);
    c.pan = vp.center() - b.center() * c.zoom;
    c.valid = true;
}

void App::zoomAt(float f, Vec2 s) {
    Camera& c = cam[view];
    Vec2 w = (s - c.pan) / c.zoom;
    c.zoom = std::clamp(c.zoom * f, 0.008f, 4.0f);
    c.pan = s - w * c.zoom;
}

// PCB pad: rectangle (square/SMD), oval or round copper with its drill hole.
static void drawPad(Gfx& g, Vec2 ctr, const PinDef& pin, Vec2 size, float z) {
    Rect r(ctr.x - size.x / 2 * z, ctr.y - size.y / 2 * z, size.x * z, size.y * z);
    if (pin.rectPad()) g.fillRect(r, kCopper);
    else if (size.x != size.y) g.fillRoundRect(r, std::min(r.w, r.h) / 2, kCopper);
    else g.fillCircle(ctr, r.w / 2, kCopper);
    if (!pin.smd()) g.fillCircle(ctr, pin.drill / 2 * z, rgba(30, 30, 32));
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

static float pinRadius(const PartInst& p, int pin, int v) {
    if (v == PCB) {
        Vec2 sz = p.def->pins[(size_t)pin].padSize();
        return std::max(12.f, std::min(sz.x, sz.y) / 2);  // fine-pitch SMD pads sit closer than 40 mil
    }
    if (p.def->isBreadboard) return 40;
    return 35;
}

bool App::pinAt(Vec2 s, int& part, int& pin, int exPart, int exPin) {
    Vec2 w = toWorld(s);
    float minPx = (in.touch ? 14.f : 7.f) * ui.s;
    float best = 1e30f;
    part = pin = -1;
    for (int i = (int)doc.parts.size() - 1; i >= 0; i--) {
        const PartInst& p = doc.parts[(size_t)i];
        if (!partShown(p, view)) continue;
        Rect b = p.worldBounds(view);
        float pad = 80 + minPx / cam[view].zoom;
        if (!Rect(b.x - pad, b.y - pad, b.w + 2 * pad, b.h + 2 * pad).contains(w)) continue;
        for (size_t k = 0; k < p.def->pins.size(); k++) {
            if (p.id == exPart && (int)k == exPin) continue;
            float r = std::max(pinRadius(p, (int)k, view), minPx / cam[view].zoom);
            float d = dist(p.pinPos((int)k, view), w);
            // Parts in front win over breadboard holes underneath.
            float score = d + (p.def->isBreadboard ? 1000.f : 0.f);
            if (d <= r && score < best) { best = score; part = p.id; pin = (int)k; }
        }
    }
    return part >= 0;
}

float App::hitRadius(int v, float visualHalfWorld) const {
    // A thin line has to stay grabbable when the view is zoomed out, and a thick
    // trace should be grabbable across its whole width. The radius follows the drawn
    // width but stays close to the pixel minimum, so a wide cable never swallows the
    // part drawn next to it. Copper is real, so on the PCB the width counts.
    float z = std::max(cam[v].zoom, 1e-6f);
    float minPx = (in.touch ? 14.f : 6.f) * ui.s;
    float capPx = minPx * (v == PCB ? 8.f : 1.3f);
    return std::clamp(visualHalfWorld * z, minPx, capPx) / z;
}

float App::wireJoinTol(int v) const {
    // Half a grid step of the view: two wire points closer than this are one node.
    return v == PCB ? 4.f : v == SCH ? 6.f : 10.f;
}

// Collects everything under the pointer instead of returning the first match.
//
// The old order (pins before wire segments) let a part swallow every wire crossing
// it: the pin radius is a comfortable 35 mils, the wire only a few pixels. Each
// candidate now carries the layer it belongs to and how far it is from the pointer
// on screen, so a wire crossing a part wins in the middle of the wire and the part
// wins everywhere else.
//
// Priorities (small wins): 0 selection handle, 1 wire end, 2 wire bend, 3 wire
// segment, 4 pin, 5 part body, 6 annotation, 7 note, 10 ratsnest line.
//
// Boards and breadboards are drawn under the notes and the markup (see the drawing
// code), so where an annotation or a note is hit, the holes and the body of a board
// under it drop to 8 and 9: otherwise a text label placed on a breadboard could never
// be clicked, the breadboard would always win.
void App::collectHits(Vec2 s, std::vector<HitCand>& out) {
    out.clear();
    const Vec2 w = toWorld(s);
    const float z = std::max(cam[view].zoom, 1e-6f);
    auto push = [&out](const Hit& h, int prio, float dPx) {
        HitCand c;
        c.h = h;
        c.prio = prio;
        c.d = dPx;
        out.push_back(c);
    };

    // 0 — handles of the current selection: note corner, annotation resize point
    if (layerShown(view, LY_NOTES)) {
        for (auto& n : doc.notes) {
            if (n.view != view) continue;
            Vec2 corner = n.pos + n.size;
            float tol = hitRadius(view, 0) * 1.5f;
            if (dist(corner, w) <= tol) {
                Hit h;
                h.kind = Hit::NOTE_CORNER;
                h.note = n.id;
                push(h, 0, dist(corner, w) * z);
            }
        }
    }
    if (layerShown(view, LY_ANNO)) {
        for (auto& a : doc.annos) {
            if (a.view != view || a.hidden) continue;
            if (!selAnnos.count(a.id)) continue;  // grab the shape first, then its handles
            // A picture has one extra handle: the rotation grip above its top edge.
            if (a.kind == AnnoKind::Image) {
                Vec2 rot = annoRotateHandleAt(a, kRotateHandlePx * ui.s / z);
                float rtol = hitRadius(view, 0) * 1.2f;
                if (dist(rot, w) <= rtol) {
                    Hit h;
                    h.kind = Hit::ANNO_HANDLE;
                    h.anno = a.id;
                    h.index = kAnnoRotateHandle;
                    push(h, 0, dist(rot, w) * z);
                    continue;
                }
            }
            int idx = -1;
            float tol = hitRadius(view, 0) * 1.2f;
            if (annoHitHandle(a, w, tol, &idx)) {
                Hit h;
                h.kind = Hit::ANNO_HANDLE;
                h.anno = a.id;
                h.index = idx;
                push(h, 0, dist(annoHandleAt(a, idx), w) * z);
            }
        }
    }

    // 1..3 — wires: ends, bend points, segments
    for (auto& wr : doc.wires) {
        if (wr.view != view || !wireShown(wr)) continue;
        float endR = hitRadius(view, view == PCB ? wr.width / 2 : 18.f);
        for (int e = 0; e < 2; e++) {
            Vec2 pt = e == 0 ? wr.pts.front() : wr.pts.back();
            float d = dist(pt, w);
            if (d > endR) continue;
            // An end sitting on a pin is only grabbable when the wire is selected,
            // otherwise pressing there means "use this pin".
            if (wr.ends[e].attached() && !selWires.count(wr.id)) continue;
            Hit h;
            h.kind = Hit::WIRE_END;
            h.wire = wr.id;
            h.index = e;
            push(h, 1, d * z);
        }
        for (size_t k = 1; k + 1 < wr.pts.size(); k++) {
            float d = dist(wr.pts[k], w);
            if (d > endR) continue;
            Hit h;
            h.kind = Hit::WIRE_BEND;
            h.wire = wr.id;
            h.index = (int)k;
            push(h, 2, d * z);
        }
        float segR = hitRadius(view, (view == PCB ? wr.width : view == BB ? 30.f : 12.f) / 2);
        for (size_t k = 0; k + 1 < wr.pts.size(); k++) {
            float d = distPointSeg(w, wr.pts[k], wr.pts[k + 1]);
            if (d > segR) continue;
            Hit h;
            h.kind = Hit::WIRE_SEG;
            h.wire = wr.id;
            h.index = (int)k;
            push(h, 3, d * z);
        }
    }

    // 4 — pins (pinAt is left exactly as it was: snapping must not get pickier)
    int part = -1, pin = -1;
    if (pinAt(s, part, pin, -1, -1)) {
        const PartInst* p = doc.part(part);
        if (p) {
            bool onPin = dist(p->pinPos(pin, view), w) <= std::max(pinRadius(*p, pin, view), hitRadius(view, 0));
            // On touch the enlarged pin radius covers most of a small part, so a drag
            // there would always start a wire: inside the body, the part is meant.
            if (in.touch && !p->def->isBreadboard && !p->def->isBoard && p->worldBounds(view).contains(w) &&
                (selParts.count(p->id) || !onPin)) {
                Hit h;
                h.kind = Hit::PART;
                h.part = p->id;
                push(h, 5, 0.f);
            } else {
                Hit h;
                h.kind = Hit::PIN;
                h.part = part;
                h.pin = pin;
                push(h, 4, dist(p->pinPos(pin, view), w) * z);
            }
        }
    }

    // 5 — part bodies, front-most first so equal distances keep the drawn order
    for (int i = (int)doc.parts.size() - 1; i >= 0; i--) {
        const PartInst& p = doc.parts[(size_t)i];
        if (!partShown(p, view)) continue;
        if (!p.worldBounds(view).contains(w)) continue;
        Hit h;
        h.kind = Hit::PART;
        h.part = p.id;
        push(h, 5, 0.f);
    }

    // 6, 7 — annotations and notes (markup under the parts, and under their clicks
    // too, but above the boards and breadboards they are drawn on)
    if (layerShown(view, LY_NOTES)) {
        for (int i = (int)doc.notes.size() - 1; i >= 0; i--) {
            const Note& n = doc.notes[(size_t)i];
            if (n.view != view) continue;
            if (!Rect(n.pos.x, n.pos.y, n.size.x, n.size.y).contains(w)) continue;
            Hit h;
            h.kind = Hit::NOTE;
            h.note = n.id;
            push(h, 7, 0.f);  // under the markup, which is drawn over the notes
        }
    }
    if (layerShown(view, LY_ANNO)) {
        for (int i = (int)doc.annos.size() - 1; i >= 0; i--) {
            const Anno& a = doc.annos[(size_t)i];
            if (a.view != view || a.hidden) continue;
            // Wide enough to grab a shape easily (see annoHitTolPx), but the priority
            // below stays as it was: a wire end, a bend, a segment or a pin under the
            // pointer still wins over the markup around it.
            float tol = std::max(hitRadius(view, 0), annoHitTolPx(a, ui.s) / z);
            if (!annoHitTest(a, w, tol)) continue;
            // The same distance the hit test measured, so the nearest stroke wins. A
            // box (text) has no stroke: it keeps distance 0, i.e. the drawn order.
            bool stroke = a.kind == AnnoKind::Rect || a.kind == AnnoKind::Line || a.kind == AnnoKind::Circle;
            float d = stroke ? annoStrokeDist(a, w) * z : 0.f;
            Hit h;
            h.kind = Hit::ANNO;
            h.anno = a.id;
            push(h, 6, d);
        }
    }

    // Markup is drawn over boards and breadboards: where it is hit, their holes and
    // bodies give way (8, 9). Everywhere else they keep 4 and 5, so wiring a bare
    // breadboard works exactly as before.
    bool markupHit = false;
    for (auto& c : out)
        if (c.h.kind == Hit::ANNO || c.h.kind == Hit::NOTE) markupHit = true;
    if (markupHit) {
        for (auto& c : out) {
            if (c.h.kind != Hit::PIN && c.h.kind != Hit::PART) continue;
            const PartInst* p = doc.part(c.h.part);
            if (!p || !p->def || !(p->def->isBoard || p->def->isBreadboard)) continue;
            c.prio = c.h.kind == Hit::PIN ? 8 : 9;
        }
    }

    // 10 — ratsnest lines
    if (showRatsnest && layerShown(view, LY_RATSNEST)) {
        for (auto& r : conn.ratsnest(doc, view)) {
            float d = distPointSeg(w, r.a, r.b);
            if (d > hitRadius(view, 0)) continue;
            Hit h;
            h.kind = Hit::RATSNEST;
            h.rat = r;
            push(h, 10, d * z);
        }
    }
}

Hit App::hitTest(Vec2 s) {
    std::vector<HitCand> cands;
    collectHits(s, cands);
    const HitCand* best = nullptr;
    for (auto& c : cands) {
        if (!best || c.prio < best->prio || (c.prio == best->prio && c.d < best->d - 0.01f)) best = &c;
    }
    return best ? best->h : Hit();
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

// A wire end is connected when any of these holds: it is being dragged onto a pin
// right now, it is attached and still sits on that pin, or it touches another wire
// of the same view. Structure alone is not enough — ends[] is only written when the
// drag finishes, so a mid-drag frame would always show a red dot on a snapped end.
bool App::wireEndConnected(const Wire& w, int end) const {
    if (end < 0 || end > 1 || w.pts.size() < 2) return false;
    Vec2 p = end == 0 ? w.pts.front() : w.pts.back();
    if ((drag == DRAG_WIRE_NEW || drag == DRAG_WIRE_END) && dragWire == w.id && dragWireEnd == end && wireTargetPart >= 0) return true;
    const WireEnd& e = w.ends[end];
    if (e.attached()) {
        const PartInst* part = doc.part(e.part);
        if (part && part->def && e.pin >= 0 && e.pin < (int)part->def->pins.size() && part->inView(w.view) &&
            dist(p, part->pinPos(e.pin, w.view)) <= 1e-2f)
            return true;
    }
    float tol = wireJoinTol(w.view);
    return wireProximity(w.view, tol).touches(p, w.id, tol);
}

// Built once per frame (or after any change), not per wire: the old full scan made
// drawing a design with hundreds of wires quadratic.
const WireGrid& App::wireProximity(int v, float tol) const {
    if (wireGridRev != doc.revision || wireGridView != v || wireGridDrag != (int)drag || wireGridDragWire != dragWire ||
        wireGridDragEnd != dragWireEnd || wireGridTarget != wireTargetPart) {
        wireGridCache.clear(std::max(200.f, tol * 8.f));
        for (auto& w : doc.wires) {
            if (w.view != v || w.pts.size() < 2) continue;
            wireGridCache.addEnd(w.pts.front(), w.id);
            wireGridCache.addEnd(w.pts.back(), w.id);
            for (size_t k = 0; k + 1 < w.pts.size(); k++) wireGridCache.addSeg(w.pts[k], w.pts[k + 1], w.id);
        }
        wireGridRev = doc.revision;
        wireGridView = v;
        wireGridDrag = (int)drag;
        wireGridDragWire = dragWire;
        wireGridDragEnd = dragWireEnd;
        wireGridTarget = wireTargetPart;
    }
    return wireGridCache;
}

// The one place that adds a bend point, used by the middle button and by the
// AddBend action. One checkpoint per bend.
bool App::insertBendAt(const Hit& h, Vec2 world) {
    Wire* wr = doc.wire(h.wire);
    if (!wr || h.index < 0 || h.index + 1 >= (int)wr->pts.size()) return false;
    Vec2 at = closestOnSeg(world, wr->pts[(size_t)h.index], wr->pts[(size_t)h.index + 1]);
    if (snapToGrid) at = snapv(at, gridSize(view) / 2);
    // A bend on top of an existing point would be invisible and unclickable.
    float tol = wireJoinTol(view);
    for (auto& pt : wr->pts)
        if (dist(pt, at) <= tol) return false;
    checkpoint();
    wr->pts.insert(wr->pts.begin() + h.index + 1, at);
    wr->tidy = false;
    dragWire = wr->id;
    dragBend = h.index + 1;
    selWires = {wr->id};
    selParts.clear(); selNotes.clear(); selAnnos.clear();
    changed();
    return true;
}

// What a pointer does. Auto keeps the built-in behaviour of that button, so the
// settings sheet replaces an Auto with an explicit role instead of adding a second
// code path. A bound AddBend turns the middle button into a plain pan: bends then
// come from the keyboard, and the two must not compete for the same press.
MouseRole App::mouseRoleFor(int slot) const {
    MouseRole r = binds.mouse[slot < 0 || slot >= kMouseSlots ? 0 : slot];
    if (r == MouseRole::Auto && slot == SLOT_MIDDLE && binds.chord[(int)Action::AddBend].bound()) return MouseRole::Pan;
    return r;
}

bool App::middleButtonPress(Vec2 s) {
    pressWorld = toWorld(s);
    Hit h = hitTest(s);
    if (h.kind == Hit::WIRE_SEG) {
        if (!insertBendAt(h, pressWorld)) return true;
        drag = DRAG_WIRE_BEND;
        return false;
    }
    if (h.kind == Hit::WIRE_BEND) {
        Wire* wr = doc.wire(h.wire);
        // A wire always keeps its two ends, so the last bend point stays.
        if (wr && wr->pts.size() > 2 && h.index > 0 && h.index + 1 < (int)wr->pts.size()) {
            checkpoint();
            wr->pts.erase(wr->pts.begin() + h.index);
            wr->tidy = false;
            changed();
        }
        drag = DRAG_NONE;
        return false;
    }
    return true;  // empty canvas (or anything else): pan
}

void App::startDrag() {
    if (readOnly) { drag = DRAG_PAN; return; }  // viewers can only pan
    Vec2 w = toWorld(in.mouse);
    switch (pressHit.kind) {
        case Hit::PIN: {
            const PartInst* p = doc.part(pressHit.part);
            if (!p) { drag = DRAG_NONE; return; }
            beginEdit();
            Wire wr;
            wr.id = doc.newId();
            wr.view = view;
            Vec2 start = p->pinPos(pressHit.pin, view);
            wr.pts = {start, w};
            wr.ends[0] = {pressHit.part, pressHit.pin};
            wr.layer = activeLayer;
            wr.width = doc.rules.traceWidth;
            wr.color = wireColorMem[view];  // the last colour the user picked
            doc.wires.push_back(wr);
            dragWire = wr.id;
            dragWireEnd = 1;
            selParts.clear(); selNotes.clear(); selAnnos.clear();
            selWires = {wr.id};
            drag = DRAG_WIRE_NEW;
            changed();
            break;
        }
        case Hit::WIRE_END: {
            beginEdit();
            dragWire = pressHit.wire;
            dragWireEnd = pressHit.index;
            if (Wire* wr = doc.wire(dragWire)) wr->ends[dragWireEnd] = WireEnd();
            drag = DRAG_WIRE_END;
            changed();
            break;
        }
        case Hit::WIRE_BEND:
            beginEdit();
            dragWire = pressHit.wire;
            dragBend = pressHit.index;
            drag = DRAG_WIRE_BEND;
            break;
        case Hit::WIRE_SEG: {
            // Dragging a segment moves the whole wire: inserting a bend point here made
            // a plain click on a wire change its shape. Bends are added with the middle
            // button (or a double-click) instead.
            Wire* wr = doc.wire(pressHit.wire);
            if (!wr) { drag = DRAG_NONE; return; }
            if (!selWires.count(wr->id)) {
                if (in.shift()) {
                    selWires.insert(wr->id);
                } else {
                    selWires = {wr->id};
                    selParts.clear(); selNotes.clear(); selAnnos.clear();
                }
            }
            beginEdit();
            moveStart.clear();
            moveWireStart.clear();
            moveNoteStart.clear();
            moveAnnoStart.clear();
            for (int id : selWires)
                if (Wire* w2 = doc.wire(id))
                    if (w2->view == view) moveWireStart[id] = w2->pts;
            anchorPart = -1;
            drag = DRAG_MOVE;
            break;
        }
        case Hit::PART:
        case Hit::NOTE: {
            if (pressHit.kind == Hit::PART && !selParts.count(pressHit.part)) {
                if (!in.shift()) { selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear(); }
                selParts.insert(pressHit.part);
            }
            if (pressHit.kind == Hit::NOTE && !selNotes.count(pressHit.note)) {
                if (!in.shift()) { selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear(); }
                selNotes.insert(pressHit.note);
            }
            beginEdit();
            moveStart.clear();
            moveWireStart.clear();
            moveNoteStart.clear();
            std::set<int> moving;
            for (int id : selParts) if (auto* p = doc.part(id)) if (p->inView(view)) moving.insert(id);
            // A breadboard carries the parts plugged into it.
            if (view == BB) {
                for (int id : std::set<int>(moving)) {
                    PartInst* bb = doc.part(id);
                    if (!bb || !bb->def->isBreadboard) continue;
                    for (auto& p : doc.parts) {
                        if (moving.count(p.id) || !p.inView(BB) || p.def->isBreadboard) continue;
                        for (size_t k = 0; k < p.def->pins.size() && !moving.count(p.id); k++)
                            for (size_t hidx = 0; hidx < bb->def->pins.size(); hidx++)
                                if (dist(p.pinPos((int)k, BB), bb->pinPos((int)hidx, BB)) < 15) { moving.insert(p.id); break; }
                    }
                }
            }
            for (auto it = moving.begin(); it != moving.end();) it = doc.part(*it)->locked ? moving.erase(it) : std::next(it);
            if (moving.empty() && pressHit.kind == Hit::PART && doc.part(pressHit.part) && doc.part(pressHit.part)->locked)
                ui.toast(trf("toast.locked", {doc.part(pressHit.part)->label}));
            for (int id : moving) moveStart[id] = doc.part(id)->place[view].pos;
            for (auto& wr : doc.wires) {
                if (wr.view != view) continue;
                bool a = wr.ends[0].attached() && moving.count(wr.ends[0].part), b = wr.ends[1].attached() && moving.count(wr.ends[1].part);
                if (selWires.count(wr.id) || (a && b)) moveWireStart[wr.id] = wr.pts;
            }
            for (int id : selNotes) if (auto* n = doc.note(id)) moveNoteStart[id] = n->pos;
            anchorPart = pressHit.kind == Hit::PART ? pressHit.part : -1;
            drag = DRAG_MOVE;
            break;
        }
        case Hit::NOTE_CORNER:
            beginEdit();
            selNotes = {pressHit.note};
            drag = DRAG_NOTE_RESIZE;
            break;
        case Hit::ANNO:
        case Hit::ANNO_HANDLE: {
            Anno* a = doc.anno(pressHit.anno);
            if (!a) { drag = DRAG_NONE; return; }
            if (!selAnnos.count(a->id)) {
                if (!in.shift()) { selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear(); }
                selAnnos.insert(a->id);
            }
            beginEdit();
            dragAnno = a->id;
            dragAnnoHandle = pressHit.kind == Hit::ANNO_HANDLE ? std::max(0, pressHit.index) : -1;
            // Shift keeps a picture's proportions: freeze the ratio the drag started with.
            dragAnnoAspect = a->kind == AnnoKind::Image && a->b.y > 0 ? a->b.x / a->b.y : 0.f;
            moveAnnoStart.clear();
            for (int id : selAnnos) if (const Anno* s = doc.anno(id)) moveAnnoStart[id] = {s->a, s->b};
            drag = DRAG_ANNO;
            break;
        }
        default:
            if (in.touch) drag = DRAG_PAN;
            else {
                drag = DRAG_RUBBER;
                rubberA = rubberB = pressWorld;
                if (!in.shift()) { selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear(); }
            }
            break;
    }
}

void App::finishPointer() {
    Vec2 w = toWorld(in.mouse);
    switch (drag) {
        case DRAG_PENDING: {
            const Hit& h = pressHit;
            bool add = in.shift();
            if (readOnly) {  // clicks only select (and show) parts
                if (h.kind == Hit::PART || h.kind == Hit::PIN) selParts = {h.part};
                else selParts.clear();
                selWires.clear(); selNotes.clear(); selAnnos.clear();
                break;
            }
            if (h.kind == Hit::PART || h.kind == Hit::PIN) {
                if (add) { if (!selParts.erase(h.part)) selParts.insert(h.part); }
                else { selParts = {h.part}; selWires.clear(); selNotes.clear(); selAnnos.clear(); }
                if (in.doubleClick && compact) drawerInspector = true;
            } else if (h.kind == Hit::WIRE_SEG || h.kind == Hit::WIRE_END || h.kind == Hit::WIRE_BEND) {
                Wire* wr = doc.wire(h.wire);
                if (in.doubleClick && wr && h.kind == Hit::WIRE_BEND) {
                    checkpoint();
                    wr->pts.erase(wr->pts.begin() + h.index);
                    wr->tidy = false;
                    changed();
                } else if (in.doubleClick && wr && h.kind == Hit::WIRE_SEG) {
                    checkpoint();
                    wr->pts.insert(wr->pts.begin() + h.index + 1, snapToGrid ? snapv(w, gridSize(view) / 2) : w);
                    wr->tidy = false;
                    changed();
                }
                if (add) { if (!selWires.erase(h.wire)) selWires.insert(h.wire); }
                else { selWires = {h.wire}; selParts.clear(); selNotes.clear(); selAnnos.clear(); }
            } else if (h.kind == Hit::NOTE || h.kind == Hit::NOTE_CORNER) {
                selNotes = {h.note};
                selParts.clear(); selWires.clear(); selAnnos.clear();
                if (in.doubleClick) drawerInspector = compact;
            } else if (h.kind == Hit::ANNO || h.kind == Hit::ANNO_HANDLE) {
                if (add) { if (!selAnnos.erase(h.anno)) selAnnos.insert(h.anno); }
                else { selAnnos = {h.anno}; selParts.clear(); selWires.clear(); selNotes.clear(); }
                if (in.doubleClick) {
                    const Anno* anno = doc.anno(h.anno);
                    if (anno && anno->kind == AnnoKind::Text && tool == TOOL_SELECT) beginInlineAnnoEdit(h.anno);
                    else drawerInspector = compact;
                }
            } else if (h.kind == Hit::RATSNEST) {
                checkpoint();
                Wire wr;
                wr.id = doc.newId();
                wr.view = view;
                wr.pts = {h.rat.a, h.rat.b};
                wr.ends[0] = {h.rat.pa.part, h.rat.pa.pin};
                wr.ends[1] = {h.rat.pb.part, h.rat.pb.pin};
                wr.layer = activeLayer;
                wr.width = doc.rules.traceWidth;
                doc.wires.push_back(wr);
                if (autoTidy) tidyWire(doc, wr.id);
                selWires = {wr.id};
                selParts.clear(); selNotes.clear(); selAnnos.clear();
                changed();
            } else if (!add) {
                selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
            }
            break;
        }
        case DRAG_WIRE_NEW:
        case DRAG_WIRE_END: {
            Wire* wr = doc.wire(dragWire);
            if (wr) {
                if (wireTargetPart >= 0) wr->ends[dragWireEnd] = {wireTargetPart, wireTargetPin};
                doc.syncWireEnds();
                float l = 0;
                for (size_t i = 0; i + 1 < wr->pts.size(); i++) l += dist(wr->pts[i], wr->pts[i + 1]);
                if (l < 5) { doc.removeWire(wr->id); selWires.clear(); }
                else if (autoTidy && (drag == DRAG_WIRE_NEW || wr->tidy)) tidyWire(doc, wr->id);
            }
            changed();
            endEdit();
            break;
        }
        case DRAG_WIRE_BEND:
            if (Wire* wr = doc.wire(dragWire)) wr->tidy = false;  // laid out by hand now
            changed();
            endEdit();
            break;
        case DRAG_MOVE: {
            std::set<int> moved, rigid;
            for (auto& kv : moveStart) moved.insert(kv.first);
            for (auto& kv : moveWireStart) rigid.insert(kv.first);  // moved whole with both its parts
            retidyAround(moved, rigid);
            changed();
            endEdit();
            break;
        }
        case DRAG_NOTE_RESIZE:
            changed();
            endEdit();
            break;
        case DRAG_ANNO:
            changed();
            endEdit();
            dragAnno = -1;
            dragAnnoHandle = -1;
            moveAnnoStart.clear();
            break;
        case DRAG_RUBBER: {
            Rect r = Rect::fromPoints(rubberA, rubberB);
            for (auto& p : doc.parts) if (partShown(p, view) && !p.locked && r.overlaps(p.worldBounds(view)) && !(p.def->isBreadboard && !Rect(r).contains(p.worldBounds(view).center()) && r.w < p.worldBounds(view).w)) selParts.insert(p.id);
            for (auto& wr : doc.wires) {
                if (wr.view != view || !wireShown(wr)) continue;
                for (auto& pt : wr.pts) if (r.contains(pt)) { selWires.insert(wr.id); break; }
            }
            for (auto& n : doc.notes) if (n.view == view && layerShown(view, LY_NOTES) && r.overlaps(Rect(n.pos.x, n.pos.y, n.size.x, n.size.y))) selNotes.insert(n.id);
            for (auto& a : doc.annos) if (a.view == view && !a.hidden && layerShown(view, LY_ANNO) && r.overlaps(annoBounds(a))) selAnnos.insert(a.id);
            break;
        }
        default:
            break;
    }
    drag = DRAG_NONE;
    dragWire = dragWireEnd = dragBend = -1;
    wireTargetPart = wireTargetPin = -1;
}

// Point text has no fixed frame: its box follows the glyphs. Measuring it before input
// handling keeps the hit box, the selection frame and the in-place editor on the text
// that is actually drawn, whatever changed it last frame (typing, size, font, undo).
void App::fitPointTexts(int v) {
    for (auto& a : doc.annos)
        if (a.view == v && a.kind == AnnoKind::Text && a.pointText) annoFitText(a, g);
}

void App::canvasInput() {
    fitPointTexts(view);
    Vec2 m = in.mouse;
    float s = ui.s;
    bool drawerOpen = compact && (drawerParts || drawerInspector || drawerLayers || drawerAI);
    bool overCanvas = rCanvas.contains(m) && !ui.overBlocking(m) && !(drawerOpen && rRight.contains(m)) &&
                      !(simOn && simNotesRect.contains(m));  // the overlay's notes toggle
    Camera& c = cam[view];
    if (inlineAnnoEdit >= 0 && inlineAnnoRect().contains(m)) {
        hover = Hit();
        return;
    }

    // pinch / two-finger gestures
    if (in.pinching || in.pinchScale != 1.f) {
        if (in.pinching && drag != DRAG_NONE && drag != DRAG_PAN) {
            if (editOpen) endEdit();
            drag = DRAG_NONE;
            canvasOwnsPointer = false;
        }
        if (in.pinching) { drag = DRAG_NONE; canvasOwnsPointer = false; }
        if (in.pinchScale != 1.f && (rCanvas.contains(in.pinchCenter) || in.pinching)) zoomAt(in.pinchScale, in.pinching ? in.pinchCenter : m);
        c.pan += in.pinchPan;
    }
    // wheel / trackpad
    if (overCanvas && !ui.blocking() && (in.wheelX != 0 || in.wheelY != 0)) {
        if (in.cmd() || !in.wheelPrecise) zoomAt(std::pow(1.18f, in.wheelY), m);
        else c.pan += Vec2(-in.wheelX, in.wheelY) * 22 * s;
    }
    // Middle button: by default a bend point on a wire segment, no bend on a bend
    // point, otherwise pan. Settings ▸ Mouse can replace that with a single role.
    MouseRole middle = mouseRoleFor(SLOT_MIDDLE);
    if (in.middlePressed && overCanvas && !ui.blocking() && drag == DRAG_NONE) {
        canvasOwnsPointer = true;
        middleDrag = true;
        lastMouse = m;
        Hit mh = hitTest(m);
        switch (readOnly ? MouseRole::Pan : middle) {
            case MouseRole::AddBend:
                if (mh.kind == Hit::WIRE_SEG) insertBendAt(mh, toWorld(m));
                canvasOwnsPointer = false;
                middleDrag = false;
                break;
            case MouseRole::ContextMenu:
                canvasOwnsPointer = false;
                middleDrag = false;
                ui.openMenu(Rect(m.x, m.y, 0, 0), contextMenu(mh));
                break;
            case MouseRole::RubberBand:
                selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
                drag = DRAG_RUBBER;
                rubberA = rubberB = toWorld(m);
                break;
            case MouseRole::None:
                canvasOwnsPointer = false;
                middleDrag = false;
                break;
            case MouseRole::Pan:
                drag = DRAG_PAN;
                break;
            case MouseRole::Auto:
                if (middleButtonPress(m)) drag = DRAG_PAN;
                else if (drag == DRAG_NONE) {
                    canvasOwnsPointer = false;
                    middleDrag = false;
                }
                break;
        }
    }
    if (middleDrag && !in.middleDown) {
        if (drag == DRAG_WIRE_BEND) finishPointer();  // commit the drag into one undo step
        else drag = DRAG_NONE;
        middleDrag = false;
        canvasOwnsPointer = false;
    }
    // A right-button pan ends when the right button comes up.
    if (drag == DRAG_PAN && !middleDrag && !in.down && !in.rightDown)
        drag = DRAG_NONE, canvasOwnsPointer = false;

    // A drawing tool owns the pointer: it decides before anything can be selected.
    if (annoToolActive() && !ui.blocking() && drag != DRAG_PAN) {
        if (annoToolInput()) return;
    }

    hover = Hit();
    if (drag == DRAG_NONE && overCanvas && !in.touch && !ui.blocking()) hover = hitTest(m);

    // Right button: the context menu by default, or whatever role the user picked.
    if (in.rightPressed && overCanvas && !ui.blocking() && drag == DRAG_NONE && !readOnly) {
        Hit h = hitTest(m);
        switch (mouseRoleFor(SLOT_RIGHT)) {
            case MouseRole::Pan:
                canvasOwnsPointer = true;
                lastMouse = m;
                drag = DRAG_PAN;
                in.rightPressed = false;  // consumed: no menu
                break;
            case MouseRole::RubberBand:
                selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
                drag = DRAG_RUBBER;
                rubberA = rubberB = toWorld(m);
                in.rightPressed = false;
                break;
            case MouseRole::AddBend:
                if (h.kind == Hit::WIRE_SEG) insertBendAt(h, toWorld(m));
                in.rightPressed = false;
                break;
            case MouseRole::None:
                in.rightPressed = false;
                break;
            case MouseRole::Auto:
            case MouseRole::ContextMenu:
                break;  // falls through to the menu below
        }
    }
    if (in.rightPressed && overCanvas && !ui.blocking() && drag == DRAG_NONE && !readOnly) {
        Hit h = hitTest(m);
        if ((h.kind == Hit::PART || h.kind == Hit::PIN) && !selParts.count(h.part)) { selParts = {h.part}; selWires.clear(); selNotes.clear(); selAnnos.clear(); }
        if ((h.kind == Hit::WIRE_SEG || h.kind == Hit::WIRE_BEND || h.kind == Hit::WIRE_END) && !selWires.count(h.wire)) { selWires = {h.wire}; selParts.clear(); selNotes.clear(); selAnnos.clear(); }
        if (h.kind == Hit::NOTE) { selNotes = {h.note}; selParts.clear(); selWires.clear(); selAnnos.clear(); }
        if (h.kind == Hit::ANNO || h.kind == Hit::ANNO_HANDLE) { selAnnos = {h.anno}; selParts.clear(); selWires.clear(); selNotes.clear(); }
        ui.openMenu(Rect(m.x, m.y, 0, 0), contextMenu(h));
    }

    // press
    if (in.pressed && overCanvas && !ui.editingText() && !ui.blocking() && !in.pinching) {
        canvasOwnsPointer = true;
        pressWorld = toWorld(m);
        pressHit = hitTest(m);
        // Usually handled on this press; finishPointer retains the same-frame release path for tests.
        if (!readOnly && !spaceDown && tool == TOOL_SELECT && in.doubleClick && (pressHit.kind == Hit::ANNO || pressHit.kind == Hit::ANNO_HANDLE)) {
            const Anno* anno = doc.anno(pressHit.anno);
            if (anno && anno->kind == AnnoKind::Text) {
                selAnnos = {anno->id};
                selParts.clear(); selWires.clear(); selNotes.clear();
                beginInlineAnnoEdit(anno->id);
                drag = DRAG_NONE;
                canvasOwnsPointer = false;
                in.pressed = false;
                return;
            }
        }
        lastMouse = m;
        longPressFired = false;
        if (!spaceDown) {
            drag = DRAG_PENDING;
        } else {
            switch (mouseRoleFor(SLOT_SPACE_LEFT)) {
                case MouseRole::AddBend:
                    if (pressHit.kind == Hit::WIRE_SEG) insertBendAt(pressHit, pressWorld);
                    drag = DRAG_NONE;
                    break;
                case MouseRole::RubberBand:
                    drag = DRAG_RUBBER;
                    rubberA = rubberB = toWorld(m);
                    break;
                case MouseRole::ContextMenu:
                    ui.openMenu(Rect(m.x, m.y, 0, 0), contextMenu(pressHit));
                    drag = DRAG_NONE;
                    break;
                case MouseRole::None:
                    drag = DRAG_NONE;
                    break;
                case MouseRole::Auto:
                case MouseRole::Pan:
                default:
                    drag = DRAG_PAN;  // what space + drag has always done
                    break;
            }
        }
        if (simOn && (pressHit.kind == Hit::PART || pressHit.kind == Hit::PIN)) {
            PartInst* p = doc.part(pressHit.part);
            if (p && p->def->sim == "pushbutton") { simPressed.insert(p->id); drag = DRAG_NONE; }
            else if (p && p->def->sim == "switch") {
                setPartProp(p->id, "position", p->prop("position") == "left" ? "right" : "left");
                drag = DRAG_NONE;
            }
        }
    }

    if (canvasOwnsPointer && (in.down || in.middleDown || in.rightDown)) {
        Vec2 w = toWorld(m);
        float moved = dist(m, in.pressPos);
        if (drag == DRAG_PENDING) {
            if (in.touch && moved < 12 * s && SDL_GetTicks() - in.pressTime > 550 && !longPressFired && !readOnly) {
                longPressFired = true;
                Hit h = pressHit;
                if ((h.kind == Hit::PART || h.kind == Hit::PIN) && !selParts.count(h.part)) { selParts = {h.part}; selWires.clear(); selNotes.clear(); selAnnos.clear(); }
                if (h.kind == Hit::WIRE_SEG && !selWires.count(h.wire)) { selWires = {h.wire}; selParts.clear(); selNotes.clear(); selAnnos.clear(); }
                ui.openMenu(Rect(m.x, m.y, 0, 0), contextMenu(h));
                drag = DRAG_NONE;
            } else if (moved > (in.touch ? 10.f : 4.f) * s) {
                startDrag();
            }
        }
        switch (drag) {
            case DRAG_PAN:
                c.pan += m - lastMouse;
                break;
            case DRAG_MOVE: {
                Vec2 d = w - pressWorld;
                if (snapToGrid) {
                    float gs = gridSize(view);
                    auto it = moveStart.find(anchorPart);
                    PartInst* a = doc.part(anchorPart);
                    if (a && it != moveStart.end() && !a->def->pins.empty()) {
                        Xform x = a->place[view];
                        x.pos = it->second;
                        Vec2 pin0 = x.apply(a->def->pins[0].pos[view]);
                        d = snapv(pin0 + d, gs) - pin0;
                    } else if (a && it != moveStart.end()) {
                        d = snapv(it->second + d, gs) - it->second;
                    } else {
                        d = snapv(d, gs);
                    }
                }
                for (auto& kv : moveStart) if (auto* p = doc.part(kv.first)) p->place[view].pos = kv.second + d;
                for (auto& kv : moveWireStart)
                    if (auto* wr = doc.wire(kv.first))
                        for (size_t i = 0; i < wr->pts.size() && i < kv.second.size(); i++) wr->pts[i] = kv.second[i] + d;
                for (auto& kv : moveNoteStart) if (auto* n = doc.note(kv.first)) n->pos = kv.second + d;
                doc.syncWireEnds();
                changed();
                break;
            }
            case DRAG_WIRE_NEW:
            case DRAG_WIRE_END: {
                Wire* wr = doc.wire(dragWire);
                if (!wr) break;
                const WireEnd& other = wr->ends[1 - dragWireEnd];
                Vec2 target = snapToGrid ? snapv(w, view == BB ? 100.f : gridSize(view) / 2) : w;
                if (pinAt(m, wireTargetPart, wireTargetPin, other.part, other.pin)) {
                    target = doc.part(wireTargetPart)->pinPos(wireTargetPin, view);
                }
                if (dragWireEnd == 0) wr->pts.front() = target; else wr->pts.back() = target;
                changed();
                break;
            }
            case DRAG_WIRE_BEND: {
                Wire* wr = doc.wire(dragWire);
                if (wr && dragBend >= 0 && dragBend < (int)wr->pts.size())
                    wr->pts[(size_t)dragBend] = snapToGrid ? snapv(w, gridSize(view) / 2) : w;
                changed();
                break;
            }
            case DRAG_RUBBER:
                rubberB = w;
                break;
            case DRAG_NOTE_RESIZE:
                if (Note* n = doc.note(*selNotes.begin())) {
                    n->size = Vec2(std::max(400.f, w.x - n->pos.x), std::max(250.f, w.y - n->pos.y));
                    changed();
                }
                break;
            case DRAG_ANNO: {
                Anno* a = doc.anno(dragAnno);
                if (!a) break;
                if (dragAnnoHandle == kAnnoRotateHandle && a->kind == AnnoKind::Image) {
                    // Turning a picture: no grid snapping, Shift steps it by 15°.
                    annoMoveHandle(*a, kAnnoRotateHandle, w, 0.f, in.shift());
                } else if (dragAnnoHandle >= 0) {
                    Vec2 b = snapToGrid ? snapv(w, gridSize(view) / 2) : w;
                    // Shift on a picture's handle keeps the width/height ratio it had when
                    // the drag began (corner handles and edge handles alike).
                    float keep = (in.shift() && a->kind == AnnoKind::Image) ? dragAnnoAspect : 0.f;
                    annoMoveHandle(*a, dragAnnoHandle, b, keep, false);
                } else {
                    Vec2 d = w - pressWorld;
                    if (snapToGrid) d = snapv(d, gridSize(view) / 2);
                    for (auto& kv : moveAnnoStart) {
                        Anno* m = doc.anno(kv.first);
                        if (!m) continue;
                        m->a = kv.second.first;
                        m->b = kv.second.second;
                        annoTranslate(*m, d);
                    }
                }
                changed();
                break;
            }
            default:
                break;
        }
        // auto-scroll near the canvas edge while dragging things
        if (drag == DRAG_MOVE || drag == DRAG_WIRE_NEW || drag == DRAG_WIRE_END || drag == DRAG_RUBBER) {
            float edge = 24 * s, sp = 10 * s;
            Vec2 auto_;
            if (m.x < rCanvas.x + edge) auto_.x = sp; else if (m.x > rCanvas.r() - edge) auto_.x = -sp;
            if (m.y < rCanvas.y + edge) auto_.y = sp; else if (m.y > rCanvas.b() - edge) auto_.y = -sp;
            if (auto_ != Vec2()) { c.pan += auto_; pressWorld -= Vec2(0, 0); }
        }
        lastMouse = m;
    }
    if (canvasOwnsPointer && in.released) {
        if (!longPressFired) finishPointer();
        else drag = DRAG_NONE;
        canvasOwnsPointer = false;
        simPressed.clear();
    }
    if (!in.down && !in.middleDown && canvasOwnsPointer && !in.released) {
        // released outside of our event stream (e.g. window lost focus)
        if (drag != DRAG_NONE && drag != DRAG_PENDING) finishPointer();
        drag = DRAG_NONE;
        canvasOwnsPointer = false;
        simPressed.clear();
    }
}

std::vector<MenuItem> App::contextMenu(const Hit& h) {
    std::vector<MenuItem> m;
    auto item = [](const std::string& l, std::function<void()> a, const std::string& sc = "") { MenuItem i; i.label = l; i.action = a; i.shortcut = sc; return i; };
    if (h.kind == Hit::PART || h.kind == Hit::PIN) {
        m.push_back(item(tr("menu.part.rotate_cw"), [this]() { rotateSelection(1); }, "R"));
        m.push_back(item(tr("menu.part.rotate_ccw"), [this]() { rotateSelection(-1); }, "⇧R"));
        m.push_back(item(tr("menu.part.flip"), [this]() { flipSelection(); }, "F"));
        m.push_back(MenuItem::sep());
        m.push_back(item(tr("menu.edit.duplicate"), [this]() { duplicate(); }, "Ctrl+D"));
        m.push_back(item(tr("menu.edit.copy"), [this]() { copySelection(false); }, "Ctrl+C"));
        m.push_back(item(tr("menu.part.front"), [this]() { bringToFront(true); }));
        m.push_back(item(tr("menu.part.back"), [this]() { bringToFront(false); }));
        m.push_back(item(tr("menu.part.export_def"), [this]() { exportFile("part"); }));
        if (const PartInst* hp = doc.part(h.part))
            if (hp->def->sim == "board" || hp->def->sim == "mcu") {
                int pid = hp->id;
                m.insert(m.begin(), MenuItem::sep());
                m.insert(m.begin(), item(tr(hp->code.empty() ? "ctx.write_program" : "ctx.edit_program"), [this, pid]() { openCode(pid); }));
            }
        if (compact) m.push_back(item(tr("ctx.inspector"), [this]() { drawerInspector = true; drawerParts = false; }));
        m.push_back(MenuItem::sep());
        m.push_back(item(tr("menu.edit.delete"), [this]() { deleteSelection(); }, "Del"));
    } else if (h.kind == Hit::WIRE_SEG || h.kind == Hit::WIRE_BEND || h.kind == Hit::WIRE_END) {
        int wid = h.wire;
        if (view == BB) {
            MenuItem colors;
            colors.label = tr("ctx.wire_color");
            const char* names[] = {"color.blue", "color.red", "color.black", "color.yellow", "color.green",
                                   "color.orange", "color.white", "color.grey", "color.purple"};
            const uint32_t cv[] = {rgba(65, 140, 225), rgba(220, 50, 50), rgba(40, 40, 40), rgba(240, 200, 40), rgba(40, 170, 80),
                                   rgba(245, 140, 30), rgba(240, 240, 240), rgba(150, 150, 150), rgba(140, 70, 190)};
            for (int ci = 0; ci < 9; ci++) {
                uint32_t col = cv[ci];
                colors.children.push_back(item(tr(names[ci]), [this, col]() { setWireColor(view, selWires, col); }));
            }
            m.push_back(colors);
            // Eyedropper: keep this wire's colour for the next wires, without repainting it.
            if (const Wire* wt = doc.wire(wid)) {
                uint32_t c = wt->color;
                m.push_back(item(tr("ctx.wire_color_use"), [this, c]() {
                    wireColorMem[view] = c;
                    saveWirePrefs(view);
                    changed();  // repaint nothing, but the tool button colour may change
                }));
            }
        }
        if (view == PCB) {
            m.push_back(item(tr("ctx.move_layer"), [this]() {
                checkpoint();
                for (int id : selWires) if (Wire* w = doc.wire(id)) w->layer = 1 - w->layer;
                changed();
            }));
        }
        m.push_back(item(tr("ctx.tidy_wire"), [this]() { tidyCommand(); }));
        m.push_back(item(tr("insp.btn.straighten"), [this, wid]() {
            checkpoint();
            if (Wire* w = doc.wire(wid)) { w->pts = {w->pts.front(), w->pts.back()}; w->tidy = false; }
            changed();
        }));
        m.push_back(MenuItem::sep());
        m.push_back(item(tr("insp.btn.delete_wire"), [this]() { deleteSelection(); }, "Del"));
    } else if (h.kind == Hit::NOTE) {
        m.push_back(item(tr("insp.btn.delete_note"), [this]() { deleteSelection(); }, "Del"));
    } else if (h.kind == Hit::ANNO || h.kind == Hit::ANNO_HANDLE) {
        MenuItem colors;
        colors.label = tr("anno.stroke_color");
        const char* names[] = {"anno.color.grey", "anno.color.ink", "anno.color.blue",
                               "anno.color.red", "anno.color.green", "anno.color.orange"};
        for (size_t i = 0; i < annoPalette().size(); i++) {
            uint32_t col = annoPalette()[i];
            colors.children.push_back(item(tr(names[i]), [this, col]() {
                checkpoint();
                for (int id : selAnnos) if (Anno* a = doc.anno(id)) a->stroke = col;
                changed();
            }));
        }
        m.push_back(colors);
        m.push_back(item(tr("anno.dashed"), [this]() {
            checkpoint();
            for (int id : selAnnos) if (Anno* a = doc.anno(id)) a->dashed = !a->dashed;
            changed();
        }));
        // A picture's "dashed line" is its frame: offer the frame itself here too.
        if (!selAnnos.empty()) {
            const Anno* first = doc.anno(*selAnnos.begin());
            if (first && first->kind == AnnoKind::Image) {
                bool framed = !first->framed;
                m.push_back(item(tr("anno.framed"), [this, framed]() {
                    checkpoint();
                    for (int id : selAnnos) if (Anno* a = doc.anno(id)) a->framed = framed;
                    changed();
                }));
            }
        }
        m.push_back(MenuItem::sep());
        m.push_back(item(tr("insp.btn.delete_anno"), [this]() { deleteSelection(); }, "Del"));
    } else {
        m.push_back(item(tr("menu.edit.paste"), [this]() { paste(); }, "Ctrl+V"));
        m.push_back(item(tr("menu.edit.add_note"), [this]() { addNote(); }));
        m.push_back(item(tr("menu.edit.select_all"), [this]() { selectAll(); }, "Ctrl+A"));
        m.push_back(item(tr("menu.view.fit"), [this]() { fitView(view, rCanvas); }, "0"));
        m.push_back(item(tr("ctx.tidy_all"), [this]() { selWires.clear(); tidyCommand(); }));
        if (view == PCB) m.push_back(item(tr("tb.autoroute"), [this]() { runAutoroute(); }));
    }
    return m;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void App::drawPrims(const PartInst& p, int v, const Camera& c, bool forIcon) {
    const Xform& xf = p.place[v];
    auto S = [&](Vec2 w) { return c.pan + xf.apply(w) * c.zoom; };
    float z = c.zoom;
    float bright = 0;
    uint32_t ledCol = 0;
    const std::vector<float>* segs = nullptr;
    uint32_t glowOverride = 0;
    const ard::OledFrame* oled = nullptr;
    if (simOn && !forIcon) {
        auto ol = sim.oled.find(p.id);
        if (ol != sim.oled.end()) oled = &ol->second;
        auto it = sim.brightness.find(p.id);
        if (it != sim.brightness.end()) bright = std::min(1.f, it->second);
        auto sg = sim.segments.find(p.id);
        if (sg != sim.segments.end()) segs = &sg->second;
        auto gl = sim.glow.find(p.id);
        if (gl != sim.glow.end()) glowOverride = gl->second;
    }
    for (auto& pr : p.def->gfx[v]) {
        float w = pr.w > 0 ? std::max(pr.w * z, 0.7f) : 0;
        uint32_t fill = pr.fill, stroke = pr.stroke;
        if (pr.tag == 1 && bright > 0) {
            ledCol = glowOverride ? glowOverride : fill;
            fill = glowOverride ? withAlpha(mixColor(glowOverride, rgba(255, 255, 255), 0.3f * bright), 255)
                                : withAlpha(mixColor(fill, rgba(255, 255, 240), bright * 0.65f), 255);
        }
        if (pr.tag >= 10 && segs && (size_t)(pr.tag - 10) < segs->size()) {
            float b = std::min(1.f, (*segs)[(size_t)(pr.tag - 10)]);
            if (b > 0.02f) fill = withAlpha(mixColor(pr.stroke, rgba(255, 255, 255), 0.25f * b), 255);
            stroke = 0;
        } else if (pr.tag >= 10) stroke = 0;
        if (pr.tag == 3 && oled && pr.kind == Prim::RECT) {
            drawOledFrame(*oled, pr, xf, c);
            continue;
        }
        switch (pr.kind) {
            case Prim::LINE:
                g.line(S(pr.a), S(pr.b), w, stroke);
                break;
            case Prim::RECT: {
                Rect r = Rect::fromPoints(S(pr.a), S(pr.b));
                if (fill & 0xFF) { if (pr.r > 0) g.fillRoundRect(r, pr.r * z, fill); else g.fillRect(r, fill); }
                if ((stroke & 0xFF) && w > 0) g.strokeRoundRect(r, pr.r * z, w, stroke);
                break;
            }
            case Prim::CIRCLE:
                if (fill & 0xFF) g.fillCircle(S(pr.a), pr.r * z, fill);
                if ((stroke & 0xFF) && w > 0) g.strokeCircle(S(pr.a), pr.r * z, w, stroke);
                break;
            case Prim::ARC: {
                float a0 = pr.a0, a1 = pr.a1;
                if (xf.flip) { float t = 180 - a1; a1 = 180 - a0; a0 = t; }
                a0 += 90.f * xf.rot;
                a1 += 90.f * xf.rot;
                g.arc(S(pr.a), pr.r * z, a0, a1, w, stroke);
                break;
            }
            case Prim::POLY: {
                std::vector<Vec2> pts;
                pts.reserve(pr.pts.size());
                for (auto& q : pr.pts) pts.push_back(S(q));
                if (fill & 0xFF) g.fillPoly(pts, fill);
                if ((stroke & 0xFF) && w > 0) g.polyline(pts, w, stroke, (fill & 0xFF) != 0);
                break;
            }
            case Prim::TEXT: {
                float px = pr.size * z;
                if (px < 4.5f) break;
                std::string text = pr.text;
                if (pr.tag == 4 && oled) break;  // screen placeholder label, replaced by the drawing
                if (pr.tag == 2) {
                    auto it = sim.meterText.find(p.id);
                    text = simOn && it != sim.meterText.end() ? it->second : (simOn ? "--" : "OFF");
                }
                int align = pr.align;
                if (xf.flip) align = 2 - align;
                float ang = xf.rot == 1 ? 90.f : xf.rot == 3 ? -90.f : 0.f;
                // Everything printed on a component (breadboard bodies, PCB silkscreen) is monospaced,
         // like marking ink; only the schematic symbols keep the interface face.
         FontFace face = (pr.tag == 2 || v != SCH) ? FONT_MONO : FONT_UI;
                g.text(S(pr.a), text, px, fill, align, face, ang);
                break;
            }
        }
    }
    if (bright > 0.02f && ledCol) {
        Rect lens;
        bool any = false;
        for (auto& pr : p.def->gfx[v]) if (pr.tag == 1) {
            Rect r = pr.kind == Prim::CIRCLE ? Rect(pr.a.x - pr.r, pr.a.y - pr.r, 2 * pr.r, 2 * pr.r) : Rect::fromPoints(pr.a, pr.b);
            lens = any ? lens.united(r) : r;
            any = true;
        }
        Vec2 ctr = c.pan + xf.apply(lens.center()) * z;
        g.radialGlow(ctr, (180 + 260 * bright) * z, withAlpha(ledCol, (int)(170 * bright)));
    }
}

// What a sketch drew through the Adafruit SSD1306 library, on the OLED part's screen rectangle.
void App::drawOledFrame(const ard::OledFrame& f, const Prim& screen, const Xform& xf, const Camera& c) {
    auto S = [&](Vec2 w) { return c.pan + xf.apply(w) * c.zoom; };
    Rect sr = Rect::fromPoints(screen.a, screen.b);
    g.fillRoundRect(Rect::fromPoints(S(screen.a), S(screen.b)), screen.r * c.zoom, rgba(6, 8, 14));
    // active area keeps the panel's aspect ratio, centred with a small bezel
    float cell = std::min((sr.w - 40) / f.w, (sr.h - 40) / f.h);
    if (cell <= 0 || f.px.size() < (size_t)f.w * f.h) return;
    Vec2 o(sr.center().x - cell * f.w / 2, sr.center().y - cell * f.h / 2);
    uint32_t on = rgba(120, 205, 255), off = rgba(6, 8, 14);
    if (f.invert) {
        g.fillRect(Rect::fromPoints(S(o), S(o + Vec2(cell * f.w, cell * f.h))), on);
        std::swap(on, off);
    }
    for (int y = 0; y < f.h; y++)
        for (int x = 0; x < f.w;) {
            if (!f.px[(size_t)y * f.w + x]) { x++; continue; }
            int x1 = x;
            while (x1 < f.w && f.px[(size_t)y * f.w + x1]) x1++;
            g.fillRect(Rect::fromPoints(S(o + Vec2(x * cell, y * cell)), S(o + Vec2(x1 * cell, (y + 1) * cell))), on);
            x = x1;
        }
    // text in the app's monospace font, sized to the library's 6×8 character cells
    float ang = xf.rot == 1 ? 90.f : xf.rot == 3 ? -90.f : 0.f;
    for (auto& t : f.text) {
        float px = 8 * t.size * cell * c.zoom;
        if (px < 3.5f || t.x >= f.w || t.y >= f.h) continue;
        uint32_t col = t.color == 0 ? off : on;
        Vec2 at = o + Vec2(t.x * cell, (t.y + 4 * t.size) * cell);
        g.text(S(at), t.text, px * 0.95f, col, xf.flip ? 2 : 0, FONT_MONO, ang);
    }
}

void App::drawPartIcon(const PartDefPtr& def, Rect r) {
    int v = def->inView(BB) && !def->gfx[BB].empty() ? BB : def->inView(SCH) && !def->gfx[SCH].empty() ? SCH : PCB;
    Rect b = def->bounds[v];
    Camera c;
    c.zoom = std::min(r.w / std::max(b.w, 1.f), r.h / std::max(b.h, 1.f));
    c.zoom = std::min(c.zoom, 0.35f);
    c.pan = r.center() - b.center() * c.zoom;
    PartInst tmp;
    tmp.def = def;
    tmp.id = -1;
    drawPrims(tmp, v, c, true);
    if (v == PCB) {
        for (auto& pin : def->pins) drawPad(g, c.pan + pin.pos[PCB] * c.zoom, pin, pin.padSize(), c.zoom);
        for (auto& h : def->holes) g.fillCircle(c.pan + h.first * c.zoom, h.second / 2 * c.zoom, rgba(60, 60, 60));
    }
}

// A picture annotation: the backdrop behind its transparent pixels, the picture
// itself (or a placeholder when it cannot be decoded), then the optional frame and the
// selection handles. Everything follows Anno::rotation: clockwise about the centre.
void App::drawImageAnno(const Anno& a, int v, const Camera& c, float zoom, float uiScale, bool selected, bool hovered) {
    const float s = uiScale > 0 ? uiScale : 1.f;
    const float z = std::max(zoom, 1e-6f);
    auto S = [&](Vec2 w) { return c.pan + w * zoom; };
    const Rect box = annoImageRect(a);
    const Vec2 sz(box.w, box.h);
    const Vec2 ctr = S(annoImageCenter(a));
    Vec2 corner[4];
    for (int i = 0; i < 4; i++) corner[i] = S(annoImageCorner(a, i));

    // 1 — the backdrop: white paper behind a transparent logo, dark for a screenshot…
    if (a.fill & 0xFF) g.fillPoly({corner[0], corner[1], corner[2], corner[3]}, a.fill);

    // 2 — the picture. The cache rasterizes an SVG at the size it is drawn at (so a
    // zoomed symbol stays sharp) and decodes a bitmap once per size bucket.
    const ImageBlob* blob = doc.image(a.image);
    SDL_Texture* tex = nullptr;
    if (blob) tex = imageCache.get(a.image, *blob, (int)std::ceil(std::max(sz.x, sz.y) * zoom));
    const float wpx = std::max(1.f, sz.x * zoom), hpx = std::max(1.f, sz.y * zoom);
    if (tex) {
        g.image(tex, Rect(ctr.x - wpx / 2, ctr.y - hpx / 2, wpx, hpx), a.rotation);
    } else if (wpx > 3 && hpx > 3) {
        // No renderer, a broken file or a picture this build cannot decode: a dashed
        // box with a cross, so the annotation is still visible and selectable.
        g.fillPoly({corner[0], corner[1], corner[2], corner[3]}, rgba(236, 238, 241));
        const float ph = std::max(1.f, s), da = std::max(6 * s, 1.f), ga = std::max(4 * s, 1.f);
        const uint32_t grid = rgba(150, 156, 166);
        for (int i = 0; i < 4; i++) g.dashedLine(corner[i], corner[(i + 1) % 4], ph, da, ga, grid);
        g.dashedLine(corner[0], corner[2], ph, da, ga, grid);
        g.dashedLine(corner[1], corner[3], ph, da, ga, grid);
    }

    // 3 — the optional frame: dashed or solid, width from the same steps the shapes use.
    // On the PCB markup is always dashed (a solid line there would read as copper).
    if (a.framed) {
        const bool dashed = a.dashed || v == PCB;
        const float width = annoStrokePx(a, uiScale);
        const float dash = std::max(7 * s, width * 3.5f), gap = std::max(5 * s, width * 2.5f);
        for (int i = 0; i < 4; i++) {
            const Vec2 q0 = corner[i], q1 = corner[(i + 1) % 4];
            if (dashed) g.dashedLine(q0, q1, width, dash, gap, a.stroke);
            else g.line(q0, q1, width, a.stroke);
        }
    }

    // 4 — the selection frame is the picture's own box (grown a little), rotated with it.
    auto grownCorners = [&](float growPx, Vec2 out[4]) {
        Anno grown = a;
        const float gw = growPx / z;
        grown.b = sz + Vec2(2 * gw, 2 * gw);
        grown.a = annoImageCenter(a) - grown.b / 2;
        for (int i = 0; i < 4; i++) out[i] = S(annoImageCorner(grown, i));
    };
    if (hovered && !selected) {
        Vec2 q[4];
        grownCorners(2 * s, q);
        for (int i = 0; i < 4; i++) g.line(q[i], q[(i + 1) % 4], std::max(1.f, s), withAlpha(rgba(28, 116, 222), 90));
    }
    if (selected) {
        Vec2 q[4];
        grownCorners(3 * s, q);
        for (int i = 0; i < 4; i++) g.line(q[i], q[(i + 1) % 4], std::max(1.f, s), withAlpha(rgba(28, 116, 222), 170));
        const float hh = 9 * s;
        for (int i = 0; i < annoHandleCount(a); i++) {
            Vec2 h = S(annoHandleAt(a, i));
            Rect hr(h.x - hh / 2, h.y - hh / 2, hh, hh);
            g.fillRect(hr, rgba(255, 255, 255));
            g.strokeRect(hr, std::max(1.f, s), rgba(28, 116, 222));
        }
        // The rotation handle sits on a short stem above the top edge.
        Vec2 topMid = S(annoImageCorner(a, 4));
        Vec2 rot = S(annoRotateHandleAt(a, kRotateHandlePx * s / z));
        g.line(topMid, rot, std::max(1.f, s), withAlpha(rgba(28, 116, 222), 120));
        const float rr = hh / 2;
        g.fillCircle(rot, rr, rgba(255, 255, 255));
        g.strokeCircle(rot, rr, std::max(1.f, s), rgba(28, 116, 222));
        g.arc(rot, rr * 0.5f, -60, 200, std::max(1.f, s), rgba(28, 116, 222));  // a turning arrow
    }
}

void App::drawCanvas(int v, const Camera& c, Rect vp, bool exporting) {
    float s = ui.s, z = c.zoom;
    auto S = [&](Vec2 w) { return c.pan + w * z; };
    g.setClip(&vp);
    uint32_t bg = v == BB ? kBgBB : v == SCH ? kBgSCH : kBgPCB;
    g.fillRect(vp, bg);
    Rect wv((vp.x - c.pan.x) / z, (vp.y - c.pan.y) / z, vp.w / z, vp.h / z);  // visible world rect

    // grid
    if (showGrid && !exporting) {
        float gs = gridSize(v);
        while (gs * z < 9 * s) gs *= (gs == 50.f ? 2.f : 5.f);
        uint32_t gc = v == PCB ? rgba(255, 255, 255, 22) : v == SCH ? rgba(0, 40, 120, 22) : rgba(0, 0, 0, 18);
        float x0 = std::floor(wv.x / gs) * gs, y0 = std::floor(wv.y / gs) * gs;
        if (v == BB) {
            for (float x = x0; x <= wv.r(); x += gs)
                for (float y = y0; y <= wv.b(); y += gs) g.fillRect({S({x, y}).x - 1, S({x, y}).y - 1, 2, 2}, rgba(0, 0, 0, 40));
        } else {
            for (float x = x0; x <= wv.r(); x += gs) g.fillRect({S({x, 0}).x, vp.y, 1, vp.h}, gc);
            for (float y = y0; y <= wv.b(); y += gs) g.fillRect({vp.x, S({0, y}).y, vp.w, 1}, gc);
        }
    }

    auto visible = [&](const Rect& r) { return r.overlaps(Rect(wv.x - 200, wv.y - 200, wv.w + 400, wv.h + 400)); };
    bool selVisible = !exporting;

    auto drawWire = [&](const Wire& w) {
        if (!exporting && !wireShown(w)) return;
        std::vector<Vec2> pts;
        for (auto& p : w.pts) pts.push_back(S(p));
        bool sel = selVisible && selWires.count(w.id);
        bool hov = selVisible && (hover.wire == w.id);
        if (v == BB) {
            if (sel) g.polyline(pts, (30 + 26) * z + 4 * s, withAlpha(kSel, 110));
            else if (hov) g.polyline(pts, (30 + 16) * z + 2 * s, withAlpha(kSel, 60));
            g.polyline(pts, std::max(1.5f, 30 * z), darker(w.color, 0.62f));
            g.polyline(pts, std::max(1.0f, 21 * z), w.color);
            if (21 * z > 4) {
                std::vector<Vec2> hl;
                for (auto& p : pts) hl.push_back(p - Vec2(0, 5 * z));
                g.polyline(hl, 5 * z, withAlpha(mixColor(w.color, rgba(255, 255, 255), 0.45f), 120));
            }
        } else if (v == SCH) {
            if (sel || hov) g.polyline(pts, 10 * z + (sel ? 6 : 3) * s, withAlpha(kSel, sel ? 120 : 60));
            g.polyline(pts, std::max(1.4f * s, 10 * z), rgba(48, 48, 48));
        } else {
            bool inactive = w.layer != activeLayer;
            uint32_t col = w.layer == TOP ? kTraceTop : kTraceBot;
            if (inactive && !exporting) col = withAlpha(col, 120);
            if (sel || hov) g.polyline(pts, w.width * z + (sel ? 6 : 3) * s, withAlpha(rgba(255, 255, 255), sel ? 140 : 70));
            g.polyline(pts, std::max(1.f, w.width * z), col);
        }
        if (!exporting && (sel || v != PCB)) {
            for (int e = 0; e < 2; e++) {
                Vec2 p = e == 0 ? pts.front() : pts.back();
                bool connected = wireEndConnected(w, e);
                float r = (v == BB ? 13 : 9) * z;
                if (r < 2.5f * s) r = 2.5f * s;
                if (v == BB || sel || !connected) g.fillCircle(p, r, connected ? rgba(60, 180, 70) : rgba(225, 50, 50));
            }
            if (sel) for (size_t k = 1; k + 1 < pts.size(); k++) { g.fillCircle(pts[k], 5 * s, rgba(255, 255, 255)); g.strokeCircle(pts[k], 5 * s, 1.5f * s, kSel); }
        }
    };

    // ratsnest: under everything else, so a wire that is still to be routed never
    // covers a part or the markup drawn around it
    if (showRatsnest && !exporting && layerShown(v, LY_RATSNEST)) {
        ensureConn();
        uint32_t rc = v == BB ? rgba(60, 60, 200, 200) : v == SCH ? rgba(90, 90, 220, 220) : rgba(255, 255, 255, 220);
        for (auto& r : conn.ratsnest(doc, v)) {
            bool hov = hover.kind == Hit::RATSNEST && hover.rat.pa == r.pa && hover.rat.pb == r.pb;
            g.dashedLine(S(r.a), S(r.b), (hov ? 3.f : 1.6f) * s, 6 * s, 4 * s, hov ? kSel : rc);
        }
    }

    // Board and backboard bodies are drawn first, then the notes and the markup, and
    // only then every other part: markup is meant to frame a circuit, so it has to sit
    // on top of the board it frames, while pins and silkscreen stay above the markup.
    auto isSubstrate = [](const PartInst& p) { return p.def && (p.def->isBoard || p.def->isBreadboard); };
    for (auto& p : doc.parts) {
        if (!p.inView(v) || !isSubstrate(p) || !visible(p.worldBounds(v))) continue;
        if (!exporting && !partShown(p, v)) continue;
        drawPrims(p, v, c);
    }

    // notes
    for (auto& n : doc.notes) {
        if (n.view != v || (!exporting && !layerShown(v, LY_NOTES))) continue;
        Rect r(S(n.pos).x, S(n.pos).y, n.size.x * z, n.size.y * z);
        g.fillRect(Rect(r.x + 3 * s, r.y + 4 * s, r.w, r.h), rgba(0, 0, 0, 30));
        g.fillRect(r, rgba(255, 246, 176));
        g.strokeRect(r, std::max(1.f, s), rgba(214, 196, 90));
        float px = n.textSize * z;
        if (px >= 5) {
            auto lines = wrapText(g, n.text, px, r.w - 80 * z);
            for (size_t i = 0; i < lines.size(); i++) {
                float y = r.y + 40 * z + px * 1.3f * ((float)i + 0.5f);
                if (y > r.b() - px * 0.5f) break;
                g.text({r.x + 40 * z, y}, lines[i], px, rgba(60, 50, 20), 0);
            }
        }
        if (selVisible && selNotes.count(n.id)) {
            g.strokeRect(r.inset(-3 * s), 2 * s, kSel);
            g.fillRect({r.r() - 5 * s, r.b() - 5 * s, 10 * s, 10 * s}, kSel);
        }
    }

    // annotations (markup): under the parts, so a box drawn around a part never covers
    // one of its pins, and above the notes and the ratsnest lines
    for (auto& a : doc.annos) {
        if (a.view != v) continue;
        if (!exporting && (a.hidden || !layerShown(v, LY_ANNO))) continue;
        // Also here, so exports and video frames (which draw without input) hug the text.
        if (a.kind == AnnoKind::Text && a.pointText) annoFitText(a, g);
        bool sel = selVisible && selAnnos.count(a.id) > 0;
        bool hov = selVisible && hover.anno == a.id;
        // Pictures need the texture cache, so they are drawn by the canvas itself; the
        // shape and text kinds stay in drawAnno().
        if (a.kind == AnnoKind::Image) {
            drawImageAnno(a, v, c, z, s, sel, hov);
            continue;
        }
        drawAnno(g, a, c.pan, z, s, sel, hov);
    }

    if (!exporting) drawAnnoPreview(c, z, s);

    if (v == PCB) {
        auto shownPcb = [&](const PartInst& p, int layer) { return exporting || (!p.hiddenIn(PCB) && layerShown(PCB, layer)); };
        // the board itself went down under the markup, above
        for (auto& w : doc.wires) if (w.view == PCB && w.layer != activeLayer) drawWire(w);
        // pads
        for (auto& p : doc.parts) {
            if (!p.inView(PCB) || p.def->isBoard || !visible(p.worldBounds(PCB)) || !shownPcb(p, LY_PADS)) continue;
            for (size_t i = 0; i < p.def->pins.size(); i++) {
                const PinDef& pin = p.def->pins[i];
                drawPad(g, S(p.pinPos((int)i, PCB)), pin, pin.padSize(p.place[PCB]), z);
            }
            for (auto& h : p.def->holes) g.fillCircle(S(p.place[PCB].apply(h.first)), h.second / 2 * z, rgba(30, 30, 32));
        }
        for (auto& w : doc.wires) if (w.view == PCB && w.layer == activeLayer) drawWire(w);
        for (auto& p : doc.parts) {
            if (!p.inView(PCB) || p.def->isBoard || !visible(p.worldBounds(PCB))) continue;
            if (shownPcb(p, LY_SILK)) drawPrims(p, PCB, c);
            if (showPartLabels && shownPcb(p, LY_LABELS) && p.type != "via" && p.type != "hole") {
                Rect b = p.worldBounds(PCB);
                g.text(S({b.x, b.y - 45}), p.silkLabel(), 50 * z, rgba(255, 255, 255, 230), 0, FONT_MONO);
            }
        }
    } else {
        for (auto& p : doc.parts) {
            if (!p.inView(v) || !visible(p.worldBounds(v))) continue;
            if (!exporting && (p.hiddenIn(v) || !layerShown(v, LY_PARTS))) continue;
            if (!isSubstrate(p)) {
                drawPrims(p, v, c);
            }
            if (!isSubstrate(p) && v == SCH && showPartLabels && (exporting || layerShown(SCH, LY_LABELS)) && p.def->netProp.empty()) {
                Rect b = p.worldBounds(SCH);
                std::string lbl = p.label;
                if (!p.def->valueProp.empty() && p.def->valueProp != "pins") lbl += "  " + p.prop(p.def->valueProp);
                g.text(S({b.x, b.y - 45}), lbl, 55 * z, rgba(40, 40, 120), 0);
            }
            // breadboard holes in use light up green
            if (v == BB && p.def->isBreadboard && !exporting) {
                ensureConn();
                for (size_t i = 0; i < p.def->pins.size(); i++)
                    if (conn.pinConnectedInView(BB, p.id, (int)i)) {
                        Vec2 ctr = S(p.pinPos((int)i, BB));
                        float r = 22 * z;
                        g.fillRect({ctr.x - r, ctr.y - r, 2 * r, 2 * r}, rgba(60, 190, 70, 220));
                    }
            }
            // unconnected schematic pin ends
            if (v == SCH && !exporting && z > 0.04f) {
                ensureConn();
                for (size_t i = 0; i < p.def->pins.size(); i++)
                    if (!conn.pinConnectedInView(SCH, p.id, (int)i)) g.fillCircle(S(p.pinPos((int)i, SCH)), std::max(1.5f * s, 12 * z), rgba(220, 60, 60, 200));
            }
        }
        for (auto& w : doc.wires) if (w.view == v) drawWire(w);
        // schematic junction dots where 3+ things meet
        if (v == SCH) {
            std::map<std::pair<int, int>, int> cnt;
            for (auto& w : doc.wires) {
                if (w.view != SCH) continue;
                for (auto& pt : {w.pts.front(), w.pts.back()}) cnt[{(int)std::lround(pt.x), (int)std::lround(pt.y)}]++;
            }
            for (auto& p : doc.parts)
                if (p.inView(SCH)) for (size_t i = 0; i < p.def->pins.size(); i++) {
                    Vec2 pp = p.pinPos((int)i, SCH);
                    auto it = cnt.find({(int)std::lround(pp.x), (int)std::lround(pp.y)});
                    if (it != cnt.end()) it->second++;
                }
            for (auto& kv : cnt) if (kv.second >= 3) g.fillCircle(S(Vec2((float)kv.first.first, (float)kv.first.second)), std::max(2.5f * s, 22 * z), rgba(48, 48, 48));
        }
    }

    // simulation effects
    if (simOn && v != PCB) {
        float t = (float)SDL_GetTicks() / 1000.f;
        for (auto& p : doc.parts) {
            if (!partShown(p, v)) continue;
            Rect b = p.worldBounds(v);
            if (sim.damaged.count(p.id)) {
                for (int k = 0; k < 5; k++) {
                    float ph = std::fmod(t * 0.6f + k * 0.2f, 1.f);
                    Vec2 pos = S({b.center().x + std::sin(k * 2.1f + t) * 60, b.y - ph * 500});
                    g.fillCircle(pos, (60 + ph * 140) * z, rgba(90, 90, 90, (int)(150 * (1 - ph))));
                }
            }
            auto act = sim.activity.find(p.id);
            if (act != sim.activity.end() && act->second > 0.05f) {
                Vec2 ctr = S(b.center());
                if (p.def->sim == "motor") {
                    float ang = t * 12 * act->second;
                    float r = std::min(b.w, b.h) * 0.25f * z;
                    g.line(ctr - Vec2(std::cos(ang), std::sin(ang)) * r, ctr + Vec2(std::cos(ang), std::sin(ang)) * r, 6 * s, rgba(28, 116, 222, 200));
                } else {
                    for (int k = 1; k <= 3; k++) {
                        float ph = std::fmod(t * 2 + k * 0.33f, 1.f);
                        g.arc(ctr + Vec2(b.w * 0.3f * z, 0), (80 + ph * 200) * z, -40, 40, 4 * s, rgba(28, 116, 222, (int)(200 * (1 - ph))));
                    }
                }
            }
        }
    }

    if (!exporting) {
        // selection & hover outlines
        for (auto& p : doc.parts) {
            if (!partShown(p, v)) continue;
            bool sel = selParts.count(p.id) > 0;
            bool hov = hover.part == p.id && (hover.kind == Hit::PART || hover.kind == Hit::PIN);
            if (!sel && !hov) continue;
            Rect b = p.worldBounds(v);
            Rect r(S({b.x, b.y}).x - 4 * s, S({b.x, b.y}).y - 4 * s, b.w * z + 8 * s, b.h * z + 8 * s);
            if (sel) {
                g.fillRoundRect(r, 4 * s, withAlpha(kSel, 22));
                g.strokeRoundRect(r, 4 * s, 2 * s, kSel);
                if (p.locked) {  // small lock badge on locked parts
                    Rect lb(r.r() - 18 * s, r.y - 9 * s, 18 * s, 18 * s);
                    g.fillRoundRect(lb, 4 * s, kSel);
                    ui.drawIcon("lock", lb.inset(2 * s), rgba(255, 255, 255));
                }
            } else if (!p.def->isBreadboard && !p.def->isBoard) {
                g.strokeRoundRect(r, 4 * s, 1.5f * s, withAlpha(kSel, 110));
            }
        }
        // DRC markers
        if (v == PCB && drcRev == doc.revision) {
            for (auto& d : drcIssues) {
                if (d.radius <= 0) continue;
                g.strokeCircle(S(d.pos), std::max(10 * s, d.radius * z), 3 * s, rgba(255, 60, 60));
                g.fillCircle(S(d.pos), std::max(10 * s, d.radius * z), rgba(255, 60, 60, 50));
            }
        }
        // hovered pin / wire target
        auto ring = [&](int partId, int pin) {
            const PartInst* p = doc.part(partId);
            if (!p || pin < 0 || pin >= (int)p->def->pins.size()) return;
            Vec2 ctr = S(p->pinPos(pin, v));
            float r = std::max(7 * s, pinRadius(*p, pin, v) * z);
            g.fillCircle(ctr, r, withAlpha(kSel, 70));
            g.strokeCircle(ctr, r, 2 * s, kSel);
        };
        if (hover.kind == Hit::PIN) ring(hover.part, hover.pin);
        if ((drag == DRAG_WIRE_NEW || drag == DRAG_WIRE_END) && wireTargetPart >= 0) ring(wireTargetPart, wireTargetPin);
        if (drag == DRAG_RUBBER) {
            Rect r = Rect::fromPoints(S(rubberA), S(rubberB));
            g.fillRect(r, withAlpha(kSel, 30));
            g.strokeRect(r, std::max(1.f, s), kSel);
        }
        // pin tooltip
        if (hover.kind == Hit::PIN && !in.touch) {
            const PartInst* p = doc.part(hover.part);
            if (p) {
                std::string tip = p->pinAddress(hover.pin);
                if (simOn) {
                    int net = conn.netOf(p->id, hover.pin);
                    if (net >= 0 && net < (int)sim.netVoltage.size()) tip += "  " + num(sim.netVoltage[(size_t)net], 2) + " V";
                }
                float px = 13 * s;
                float tw = g.textWidth(tip, px, FONT_MONO) + 16 * s;
                Rect r(in.mouse.x + 14 * s, in.mouse.y + 18 * s, tw, 26 * s);
                if (r.r() > vp.r()) r.x = in.mouse.x - tw - 8 * s;
                g.fillRoundRect(r, 5 * s, rgba(40, 40, 46, 235));
                g.text({r.x + 8 * s, r.center().y}, tip, px, rgba(255, 255, 255), 0, FONT_MONO);
            }
        }
    }
    g.setClip(nullptr);
}

bool App::renderViewPng(int v, const std::string& path, std::string* msg) {
    bool any;
    Rect b = worldExtent(doc, v, &any);
    if (!any) { if (msg) *msg = "This view is empty."; return false; }
    b = Rect(b.x - 150, b.y - 150, b.w + 300, b.h + 300);
    float zoom = std::min({3000 / b.w, 3000 / b.h, 1.0f});
    int w = std::max(16, (int)(b.w * zoom)), h = std::max(16, (int)(b.h * zoom));
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!tex) { if (msg) *msg = SDL_GetError(); return false; }
    g.flush();
    SDL_SetRenderTarget(ren, tex);
    Camera c;
    c.zoom = zoom;
    c.pan = Vec2(-b.x * zoom, -b.y * zoom);
    c.valid = true;
    float savedScale = ui.s;
    drawCanvas(v, c, Rect(0, 0, (float)w, (float)h), true);
    g.flush();
    SDL_Surface* surf = SDL_RenderReadPixels(ren, nullptr);
    SDL_SetRenderTarget(ren, nullptr);
    SDL_DestroyTexture(tex);
    ui.s = savedScale;
    if (!surf) { if (msg) *msg = SDL_GetError(); return false; }
    bool ok = SDL_SavePNG(surf, path.c_str());
    if (!ok && msg) *msg = SDL_GetError();
    SDL_DestroySurface(surf);
    return ok;
}
