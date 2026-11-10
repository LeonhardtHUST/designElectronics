// Design rule check for the PCB view: copper clearance, parts off-board,
// overlapping footprints and unrouted connections.
#include <cstdio>

#include "tools.h"
#include "i18n.h"

namespace {
struct Copper {
    Vec2 a, b;
    float r;      // half width / pad radius
    int layer;    // 0 top, 1 bottom, 2 both
    int net;
    std::string what;
    float clr = 1e9f;  // a fine-pitch footprint's own pad gap, when tighter than the rule
};

// Smallest gap between two pads of one footprint (mils): the part was made with
// that spacing, so copper next to its pads only has to keep that far away.
float padGap(const PartDef& d) {
    float best = 1e9f;
    for (size_t i = 0; i < d.pins.size(); i++)
        for (size_t j = i + 1; j < d.pins.size(); j++) {
            const PinDef &a = d.pins[i], &b = d.pins[j];
            Vec2 sa = a.padSize(), sb = b.padSize();
            float gx = std::fabs(a.pos[PCB].x - b.pos[PCB].x) - (sa.x + sb.x) / 2;
            float gy = std::fabs(a.pos[PCB].y - b.pos[PCB].y) - (sa.y + sb.y) / 2;
            float gap = std::max(gx, gy);
            if (gap > 0.5f) best = std::min(best, gap);
        }
    return best;
}
}  // namespace

std::vector<DrcIssue> runDrc(const Document& doc, const Connectivity& conn) {
    std::vector<DrcIssue> out;
    float clr = doc.rules.clearance;
    std::vector<Copper> cu;
    Rect board;
    bool hasBoard = false;
    bool single = false;
    for (auto& p : doc.parts)
        if (p.inView(PCB) && p.def->isBoard && p.prop("layers").find("one") != std::string::npos) single = true;
    const int smdLayer = single ? BOTTOM : TOP;  // SMD parts sit on the copper side
    for (auto& p : doc.parts) {
        if (!p.inView(PCB)) continue;
        if (p.def->isBoard) { board = hasBoard ? board.united(p.worldBounds(PCB)) : p.worldBounds(PCB); hasBoard = true; continue; }
        float own = padGap(*p.def);
        for (size_t i = 0; i < p.def->pins.size(); i++) {
            const PinDef& pin = p.def->pins[i];
            Copper c{{}, {}, 0, pin.smd() ? smdLayer : 2, conn.netOf(p.id, (int)i), trf("drc.pin_label", {p.label, pin.name}), own};
            padStadium(p.pinPos((int)i, PCB), pin.padSize(p.place[PCB]), c.a, c.b, c.r);
            cu.push_back(c);
        }
        for (auto& h : p.def->holes) {
            Vec2 c = p.place[PCB].apply(h.first);
            cu.push_back({c, c, h.second / 2, 2, -100 - p.id, trf("drc.hole_label", {p.label})});
        }
    }
    for (auto& w : doc.wires) {
        if (w.view != PCB) continue;
        int net = conn.wireNet(w.id);
        for (size_t i = 0; i + 1 < w.pts.size(); i++)
            cu.push_back({w.pts[i], w.pts[i + 1], w.width / 2, w.layer, net, tr(w.layer == TOP ? "drc.trace_top" : "drc.trace_bottom")});
    }

    for (size_t i = 0; i < cu.size(); i++) {
        for (size_t j = i + 1; j < cu.size(); j++) {
            const Copper &A = cu[i], &B = cu[j];
            if (A.net == B.net && A.net >= 0) continue;
            if (A.layer != 2 && B.layer != 2 && A.layer != B.layer) continue;
            float d = distSegSeg(A.a, A.b, B.a, B.b) - A.r - B.r;
            float need = std::min({clr, A.clr, B.clr});
            if (d < need - 0.5f) {
                Vec2 pa = closestOnSeg(B.a, A.a, A.b), pb = closestOnSeg(pa, B.a, B.b);
                out.push_back({(pa + pb) / 2, std::max(30.f, A.r + 10),
                               trf("drc.too_close", {A.what, B.what, num(std::max(0.f, d), 1), num(need, 0)})});
                if (out.size() > 200) return out;
            }
        }
    }
    if (hasBoard) {
        for (auto& p : doc.parts) {
            if (!p.inView(PCB) || p.def->isBoard) continue;
            Rect r = p.worldBounds(PCB);
            if (r.x < board.x || r.y < board.y || r.r() > board.r() || r.b() > board.b()) {
                out.push_back({r.center(), std::max(r.w, r.h) / 2, trf("drc.off_board", {p.label, partTitle(*p.def)})});
            }
        }
    } else {
        out.push_back({{0, 0}, 0, tr("drc.no_board")});
    }
    // overlapping footprints
    for (size_t i = 0; i < doc.parts.size(); i++) {
        const PartInst& a = doc.parts[i];
        if (!a.inView(PCB) || a.def->isBoard || a.type == "via" || a.def->sim == "board") continue;  // shield outlines contain parts by design
        Rect ra = a.worldBounds(PCB).inset(10);
        for (size_t j = i + 1; j < doc.parts.size(); j++) {
            const PartInst& b = doc.parts[j];
            if (!b.inView(PCB) || b.def->isBoard || b.type == "via" || b.def->sim == "board") continue;
            Rect rb = b.worldBounds(PCB).inset(10);
            if (ra.overlaps(rb)) {
                Rect u(std::max(ra.x, rb.x), std::max(ra.y, rb.y), 0, 0);
                u.w = std::min(ra.r(), rb.r()) - u.x;
                u.h = std::min(ra.b(), rb.b()) - u.y;
                out.push_back({u.center(), std::max(40.f, std::min(u.w, u.h) / 2), trf("drc.overlap", {a.label, b.label})});
            }
        }
    }
    // a trace can only reach a surface-mount pad on the pad's own layer
    for (auto& w : doc.wires) {
        if (w.view != PCB || w.layer == smdLayer) continue;
        for (int e = 0; e < 2; e++) {
            const PartInst* p = w.ends[e].attached() ? doc.part(w.ends[e].part) : nullptr;
            if (!p || !p->def || w.ends[e].pin >= (int)p->def->pins.size() || !p->def->pins[(size_t)w.ends[e].pin].smd()) continue;
            out.push_back({e ? w.pts.back() : w.pts.front(), 40,
                           trf("drc.smd_layer", {tr(w.layer == TOP ? "insp.layer.top" : "insp.layer.bottom"), p->label,
                                                 p->def->pins[(size_t)w.ends[e].pin].name,
                                                 tr(smdLayer == TOP ? "drc.layer_lower_top" : "drc.layer_lower_bottom")})});
        }
    }
    RoutingStatus st;
    conn.ratsnest(doc, PCB, &st);
    if (st.connectionsToRoute > 0) {
        out.push_back({{0, 0}, 0, trn("drc.unrouted", st.connectionsToRoute)});
    }
    return out;
}
