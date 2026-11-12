// designElectronics — the Layers panel: a hierarchy of components (one row per
// level, groups move as one) plus a Photoshop-style list of everything in the
// design. Rows select, double-click zooms, and each part can be hidden in this
// view or locked. Users see components, never the renderer's layer internals.
#include <cmath>
#include <map>
#include <set>
#include <cstdio>
#include <algorithm>
#include <cctype>

#include "app.h"
#include "i18n.h"

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Collapsed-group ids in the object list: wires, notes, annotations.
constexpr int kGroupWires = -1, kGroupNotes = -2, kGroupAnnos = -3;
// Hierarchy groups fold with their own ids, far from the three above.
int groupCollapseKey(int gid) { return -1000 - gid; }

}  // namespace

// Flat icon toggle used in the list rows (no button frame).
static bool iconToggle(UI& ui, const std::string& id, Rect r, const std::string& icon, uint32_t col, const std::string& tip) {
    bool clicked = ui.button(id, r, "", BTN_FLAT);
    if (!icon.empty()) ui.drawIcon(icon, r.inset(r.w * 0.16f), col);
    ui.tooltip(r, tip);
    return clicked;
}

void App::setPartHidden(int partId, int v, bool hide) {
    PartInst* p = doc.part(partId);
    if (!p || p->hiddenIn(v) == hide) return;
    checkpoint();
    if (hide) { p->hidden |= 1u << v; selParts.erase(partId); }
    else p->hidden &= ~(1u << v);
    changed();
}

void App::setPartLocked(int partId, bool lock) {
    PartInst* p = doc.part(partId);
    if (!p || p->locked == lock) return;
    checkpoint();
    p->locked = lock;
    changed();
}

void App::zoomToPart(int partId) {
    const PartInst* p = doc.part(partId);
    if (!p || !p->inView(view)) return;
    Rect b = p->worldBounds(view);
    Camera& c = cam[view];
    // zoom out if the part doesn't fit, never zoom in past 1:1-ish
    float fit = std::min(rCanvas.w / std::max(1.f, b.w * 1.6f), rCanvas.h / std::max(1.f, b.h * 1.6f));
    if (fit < c.zoom) c.zoom = fit;
    c.pan = rCanvas.center() - b.center() * c.zoom;
}

void App::drawLayers(Rect r) {
    float s = ui.s;
    auto& th = ui.th;
    g.fillRect(r, th.panel);
    g.fillRect({r.x, r.y, std::max(1.f, s), r.h}, th.border);
    float pad = 10 * s;
    float y = r.y + pad;
    if (compact) {
        g.text({r.x + pad + 2 * s, y + 11 * s}, tr("tb.layers"), 16 * s, th.text, 0);
        Rect cb(r.r() - 40 * s, y - 4 * s, 32 * s, 32 * s);
        if (ui.iconButton("closelayers", cb, "close", "")) drawerLayers = false;
    } else {
        drawRightTabs(r, y);
    }
    y += 32 * s;

    // ---- hierarchy toolbar -------------------------------------------------
    // The level under the selection: every selected part has to sit on it.
    int rep = -1;
    bool sameLevel = true;
    int ungrouped = 0, grouped = 0;
    for (int id : selParts) {
        const PartInst* p = doc.part(id);
        if (!p || !p->inView(view)) continue;
        int key = doc.levelKey(id);
        if (rep < 0) { rep = id; }
        else if (doc.levelKey(rep) != key) sameLevel = false;
        if (p->group > 0) grouped++;
        else ungrouped++;
    }
    if (!sameLevel) rep = -1;
    bool canGroup = ungrouped >= 2 && grouped == 0;
    bool canUngroup = grouped > 0;
    bool canUp = rep >= 0 && doc.canMoveLevel(rep, +1);
    bool canDown = rep >= 0 && doc.canMoveLevel(rep, -1);

    float gap = 4 * s;
    float bw = (r.w - 2 * pad - 3 * gap) / 4;
    Rect b(r.x + pad, y, bw, 28 * s);
    if (ui.button("lh-up", b, tr("layers.move_up"), BTN_NORMAL, false, canUp) && canUp) {
        checkpoint();
        doc.moveLevel(rep, +1);
        changed();
    }
    b.x += bw + gap;
    if (ui.button("lh-down", b, tr("layers.move_down"), BTN_NORMAL, false, canDown) && canDown) {
        checkpoint();
        doc.moveLevel(rep, -1);
        changed();
    }
    b.x += bw + gap;
    if (ui.button("lh-group", b, tr("layers.group"), BTN_NORMAL, false, canGroup) && canGroup) {
        std::vector<int> ids;
        for (int id : selParts) {
            const PartInst* p = doc.part(id);
            if (p && p->inView(view) && p->group == 0) ids.push_back(id);
        }
        checkpoint();
        if (!doc.groupParts(ids)) ui.toast(tr("layers.need_two"));
        changed();
    }
    b.x += bw + gap;
    if (ui.button("lh-ungroup", b, tr("layers.ungroup"), BTN_NORMAL, false, canUngroup) && canUngroup) {
        std::set<int> gids;
        for (int id : selParts) {
            const PartInst* p = doc.part(id);
            if (p && p->group > 0) gids.insert(p->group);
        }
        checkpoint();
        for (int gid : gids) doc.ungroup(gid);
        changed();
    }
    y += 34 * s;

    y += ui.wrappedText(Rect(r.x + pad, y, r.w - 2 * pad, 0), tr("layers.hint"), th.textDim, 11.5f * s) + 6 * s;
    g.fillRect({r.x + pad, y, r.w - 2 * pad, std::max(1.f, s)}, th.border);
    y += 10 * s;

    // ---- object list -------------------------------------------------------
    ui.textField("layerfilter", Rect(r.x + pad, y, r.w - 2 * pad, 32 * s), layerFilter, tr("layers.filter"), true);
    y += 40 * s;
    Rect list(r.x, y, r.w, r.b() - y - pad);

    ensureConn();
    std::set<int> unrouted;
    for (auto& rl : conn.ratsnest(doc, view)) { unrouted.insert(rl.pa.part); unrouted.insert(rl.pb.part); }

    std::string f = lower(layerFilter);
    auto matches = [&](const PartInst& p) {
        if (f.empty()) return true;
        std::string hay = lower(p.label + " " + p.def->title + " " + partTitle(*p.def) + " " + p.type + " " + (p.def->valueProp.empty() ? "" : p.prop(p.def->valueProp)));
        for (auto& pin : p.def->pins) if (lower(pin.name).find(f) != std::string::npos) return true;
        return hay.find(f) != std::string::npos;
    };

    // Levels, back to front: an ungrouped part on its own, or one run of a group.
    struct Level { int gid; std::vector<int> ids; };
    std::vector<Level> levels;
    for (size_t i = 0; i < doc.parts.size();) {
        int gid = doc.parts[i].group;
        if (gid > 0) {
            Level L{gid, {}};
            while (i < doc.parts.size() && doc.parts[i].group == gid) {
                if (doc.parts[i].inView(view)) L.ids.push_back(doc.parts[i].id);
                i++;
            }
            if (!L.ids.empty()) levels.push_back(L);
        } else {
            if (doc.parts[i].inView(view)) levels.push_back({0, {doc.parts[i].id}});
            i++;
        }
    }

    struct Row { int kind; int id; int depth; };  // 0 part, 1 group, 2 member, 3 wires, 4 notes, 5 annos, 6 anno
    std::vector<Row> rows;
    for (int li = (int)levels.size() - 1; li >= 0; li--) {
        const Level& L = levels[(size_t)li];
        std::vector<int> vis;
        for (int id : L.ids) {
            const PartInst* p = doc.part(id);
            if (p && matches(*p)) vis.push_back(id);
        }
        if (vis.empty()) continue;
        if (L.gid > 0) {
            rows.push_back({1, L.gid, 0});
            if (!layerCollapsed.count(groupCollapseKey(L.gid)))
                for (int id : vis) rows.push_back({2, id, 1});
        } else {
            rows.push_back({0, vis[0], 0});
        }
    }

    int nWires = 0, nNotes = 0, nAnnos = 0;
    for (auto& w : doc.wires) if (w.view == view) nWires++;
    for (auto& n : doc.notes) if (n.view == view) nNotes++;
    for (auto& a : doc.annos) if (a.view == view) nAnnos++;
    if (f.empty() && nWires) rows.push_back({3, kGroupWires, 0});
    if (f.empty() && nNotes) rows.push_back({4, kGroupNotes, 0});
    if (f.empty() && nAnnos) {
        rows.push_back({5, kGroupAnnos, 0});
        if (!layerCollapsed.count(kGroupAnnos))
            for (int i = (int)doc.annos.size() - 1; i >= 0; i--) {
                const Anno& a = doc.annos[(size_t)i];
                if (a.view == view) rows.push_back({6, a.id, 1});
            }
    }

    const float rowH = 40 * s;
    if (rows.empty()) {
        ui.wrappedText(list.inset(pad), f.empty() ? tr("layers.empty") : trf("layers.no_match", {layerFilter}), th.textDim, 13 * s);
        return;
    }
    float oy = ui.beginScroll("layers-list", list, (float)rows.size() * rowH);
    Uint64 now = SDL_GetTicks();
    for (size_t i = 0; i < rows.size(); i++) {
        const Row& row = rows[i];
        Rect rr(list.x + 4 * s, list.y + oy + (float)i * rowH, list.w - 8 * s, rowH - 2 * s);
        if (rr.b() < list.y || rr.y > list.b()) continue;
        float x = rr.x + 8 * s + (float)row.depth * 20 * s;
        float right = rr.r() - 6 * s;

        if (row.kind == 1) {  // one hierarchy group
            const std::vector<int> members = doc.groupMembers(row.id);
            std::vector<int> here;
            for (int id : members) {
                const PartInst* p = doc.part(id);
                if (p && p->inView(view)) here.push_back(id);
            }
            bool anySel = false;
            for (int id : here) if (selParts.count(id)) anySel = true;
            if (anySel) g.fillRoundRect(rr, 6 * s, withAlpha(th.accent, 40));
            if (ui.button("lgrp-" + std::to_string(row.id), rr, "", BTN_FLAT, anySel)) {
                selParts.clear();
                for (int id : here) if (!doc.part(id)->hiddenIn(view)) selParts.insert(id);
                selWires.clear(); selNotes.clear(); selAnnos.clear();
            }
            bool coll = layerCollapsed.count(groupCollapseKey(row.id)) > 0;
            Rect ch(x, rr.center().y - 10 * s, 20 * s, 20 * s);
            if (ui.button("lcolg-" + std::to_string(row.id), ch, "", BTN_FLAT)) {
                if (coll) layerCollapsed.erase(groupCollapseKey(row.id));
                else layerCollapsed.insert(groupCollapseKey(row.id));
            }
            ui.drawIcon(coll ? "chevright" : "chevron", ch.inset(2 * s), th.textDim);

            Rect ic(x + 22 * s, rr.center().y - 10 * s, 20 * s, 20 * s);
            ui.drawIcon("layer", ic, th.textDim);
            std::string name = trf("layers.group_name", {std::to_string(row.id)}) + "  ·  " +
                               trf("layers.group_count", {std::to_string(members.size())});
            float eyeX = right - 26 * s;
            g.text({ic.r() + 8 * s, rr.center().y}, g.ellipsize(name, 13.5f * s, eyeX - ic.r() - 16 * s), 13.5f * s, th.text, 0);

            bool anyShown = false;
            for (int id : here) if (!doc.part(id)->hiddenIn(view)) anyShown = true;
            Rect eye(eyeX, rr.center().y - 13 * s, 26 * s, 26 * s);
            if (iconToggle(ui, "leyeg-" + std::to_string(row.id), eye, anyShown ? "eye" : "eyeoff",
                           anyShown ? th.text : th.textDim,
                           anyShown ? tr("layers.hide_group") : tr("layers.show_group"))) {
                checkpoint();
                for (int id : here) {
                    PartInst* p = doc.part(id);
                    if (anyShown) { p->hidden |= 1u << view; selParts.erase(id); }
                    else p->hidden &= ~(1u << view);
                }
                changed();
            }
            continue;
        }
        if (row.kind == 2) {  // a member of a group (not reorderable on its own)
            const PartInst* p = doc.part(row.id);
            if (!p) continue;
            bool sel = selParts.count(p->id) > 0;
            bool hidden = p->hiddenIn(view);
            if (sel) g.fillRoundRect(rr, 6 * s, withAlpha(th.accent, 40));
            if (ui.button("lmem-" + std::to_string(p->id), rr, "", BTN_FLAT, sel)) {
                if (in.shift()) { if (sel) selParts.erase(p->id); else selParts.insert(p->id); }
                else { selParts = {p->id}; selWires.clear(); selNotes.clear(); selAnnos.clear(); }
                if (hidden) selParts.erase(p->id);
            }
            Rect eye(right - 26 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
            if (iconToggle(ui, "leye-" + std::to_string(p->id), eye, hidden ? "eyeoff" : "eye",
                           hidden ? th.textDim : th.text,
                           hidden ? trf("layers.show_in", {viewLabel(view)}) : trf("layers.hide_in", {viewLabel(view)})))
                setPartHidden(p->id, view, !hidden);
            Rect icon(x, rr.y + 4 * s, rowH - 10 * s, rowH - 10 * s);
            g.fillRoundRect(icon, 5 * s, rgba(255, 255, 255, hidden ? 110 : 255));
            drawPartIcon(p->def, icon.inset(2 * s));
            float tx = icon.r() + 8 * s;
            std::string l1 = p->label;
            std::string l2 = partTitle(*p->def);
            if (!p->def->valueProp.empty() && p->def->valueProp != "pins")
                l2 = propValueLabel(*p->def, p->def->valueProp, p->prop(p->def->valueProp)) + " · " + l2;
            float tw = eye.x - tx - 6 * s;
            g.text({tx, rr.y + 13 * s}, g.ellipsize(l1, 13.5f * s, tw), 13.5f * s, hidden ? th.textDim : th.text, 0);
            g.text({tx, rr.y + 29 * s}, g.ellipsize(l2, 11.5f * s, tw), 11.5f * s, th.textDim, 0);
            continue;
        }
        if (row.kind == 3 || row.kind == 4) {  // wires / notes groups
            bool wiresGroup = row.kind == 3;
            std::string id = wiresGroup ? "lw" : "ln";
            int layer = wiresGroup ? (view == PCB ? LY_COPPER_TOP : LY_WIRES) : LY_NOTES;
            bool on = view == PCB && wiresGroup ? (layerShown(PCB, LY_COPPER_TOP) || layerShown(PCB, LY_COPPER_BOTTOM)) : layerShown(view, layer);
            if (ui.button("lrow-" + id, rr, "", BTN_FLAT)) {
                // select everything in the group
                selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
                if (wiresGroup) { for (auto& w : doc.wires) if (w.view == view && wireShown(w)) selWires.insert(w.id); }
                else for (auto& n : doc.notes) if (n.view == view) selNotes.insert(n.id);
            }
            Rect eye(right - 26 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
            if (iconToggle(ui, "leye-" + id, eye, on ? "eye" : "eyeoff", on ? th.text : th.textDim, tr(on ? "layers.hide" : "layers.show"))) {
                if (view == PCB && wiresGroup) layerOff[PCB][LY_COPPER_TOP] = layerOff[PCB][LY_COPPER_BOTTOM] = on;
                else layerOff[view][layer] = on;
            }
            ui.drawIcon(wiresGroup ? "wire" : "note", Rect(x, rr.center().y - 11 * s, 22 * s, 22 * s), on ? th.textDim : withAlpha(th.textDim, 120));
            std::string t = trf(wiresGroup ? (view == PCB ? "layers.traces_count" : "layers.wires_count") : "layers.notes_count",
                                {std::to_string(wiresGroup ? nWires : nNotes)});
            g.text({x + 32 * s, rr.center().y}, t, 14 * s, on ? th.text : th.textDim, 0);
            continue;
        }
        if (row.kind == 5) {  // annotations group
            bool on = layerShown(view, LY_ANNO);
            if (ui.button("lrow-la", rr, "", BTN_FLAT)) {
                selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
                if (on) for (auto& a : doc.annos) if (a.view == view && !a.hidden) selAnnos.insert(a.id);
            }
            Rect eye(right - 26 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
            if (iconToggle(ui, "leye-la", eye, on ? "eye" : "eyeoff", on ? th.text : th.textDim, tr(on ? "layers.hide" : "layers.show")))
                layerOff[view][LY_ANNO] = on;
            bool coll = layerCollapsed.count(kGroupAnnos) > 0;
            Rect ch(x - 4 * s, rr.center().y - 10 * s, 20 * s, 20 * s);
            if (ui.button("lcol-la", ch, "", BTN_FLAT)) {
                if (coll) layerCollapsed.erase(kGroupAnnos);
                else layerCollapsed.insert(kGroupAnnos);
            }
            ui.drawIcon(coll ? "chevright" : "chevron", ch.inset(2 * s), th.textDim);
            // a little dashed box as the group icon
            Rect sw(x + 18 * s, rr.center().y - 9 * s, 20 * s, 18 * s);
            g.dashedLine({sw.x, sw.y}, {sw.r(), sw.y}, std::max(1.f, s), 4 * s, 3 * s, th.textDim);
            g.dashedLine({sw.r(), sw.y}, {sw.r(), sw.b()}, std::max(1.f, s), 4 * s, 3 * s, th.textDim);
            g.dashedLine({sw.r(), sw.b()}, {sw.x, sw.b()}, std::max(1.f, s), 4 * s, 3 * s, th.textDim);
            g.dashedLine({sw.x, sw.b()}, {sw.x, sw.y}, std::max(1.f, s), 4 * s, 3 * s, th.textDim);
            g.text({x + 46 * s, rr.center().y}, trf("layers.annos_count", {std::to_string(nAnnos)}), 14 * s, on ? th.text : th.textDim, 0);
            continue;
        }
        if (row.kind == 6) {  // one annotation
            Anno* a = doc.anno(row.id);
            if (!a) continue;
            bool sel = selAnnos.count(a->id) > 0;
            if (sel) g.fillRoundRect(rr, 6 * s, withAlpha(th.accent, 40));
            std::string rid = std::to_string(a->id);
            if (ui.button("lrow-an" + rid, rr, "", BTN_FLAT, sel)) {
                if (in.shift()) { if (sel) selAnnos.erase(a->id); else selAnnos.insert(a->id); }
                else { selAnnos = {a->id}; selParts.clear(); selWires.clear(); selNotes.clear(); }
                if (a->hidden) selAnnos.erase(a->id);
            }
            Rect eye(right - 26 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
            if (iconToggle(ui, "leye-an" + rid, eye, a->hidden ? "eyeoff" : "eye", a->hidden ? th.textDim : th.text,
                           a->hidden ? trf("layers.show_in", {viewLabel(view)}) : trf("layers.hide_in", {viewLabel(view)}))) {
                checkpoint();
                a->hidden = !a->hidden;
                if (a->hidden) selAnnos.erase(a->id);
                changed();
            }
            Rect sw(x + 4 * s, rr.center().y - 7 * s, 18 * s, 14 * s);
            g.fillRect(sw, withAlpha(a->stroke, a->hidden ? 90 : 255));
            float tx2 = sw.r() + 8 * s;
            std::string name = annoKindLabel(a->kind);
            if (a->kind == AnnoKind::Text && !a->text.empty()) {
                std::string first = a->text.substr(0, a->text.find('\n'));
                name += "  " + first;
            } else if (a->kind == AnnoKind::Image) {
                // "Picture  PNG 640×480": what it is, so two pictures can be told apart.
                const ImageBlob* blob = doc.image(a->image);
                name += "  " + (blob ? imageFormatLabel(blob->mime) + " " + std::to_string(blob->w) + "×" + std::to_string(blob->h)
                                     : tr("anno.image_missing"));
            }
            g.text({tx2, rr.center().y}, g.ellipsize(name, 14 * s, eye.x - tx2 - 6 * s), 14 * s, a->hidden ? th.textDim : th.text, 0);
            continue;
        }

        const PartInst* p = doc.part(row.id);
        if (!p) continue;
        bool sel = selParts.count(p->id) > 0;
        bool hidden = p->hiddenIn(view);
        if (sel) g.fillRoundRect(rr, 6 * s, withAlpha(th.accent, 40));
        std::string rid = std::to_string(p->id);
        if (ui.button("lrow-" + rid, rr, "", BTN_FLAT, sel)) {
            bool dbl = lastLayerClickPart == p->id && now - lastLayerClick < 400;
            lastLayerClick = now;
            lastLayerClickPart = p->id;
            if (in.shift()) { if (sel) selParts.erase(p->id); else selParts.insert(p->id); }
            else { selParts = {p->id}; selWires.clear(); selNotes.clear(); selAnnos.clear(); }
            if (hidden) selParts.erase(p->id);
            if (dbl) zoomToPart(p->id);
        }
        // eye + lock (drawn after the row so they take the click)
        Rect eye(right - 26 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
        Rect lock(right - 56 * s, rr.center().y - 13 * s, 26 * s, 26 * s);
        if (iconToggle(ui, "leye-" + rid, eye, hidden ? "eyeoff" : "eye", hidden ? th.textDim : th.text,
                       hidden ? trf("layers.show_in", {viewLabel(view)}) : trf("layers.hide_in", {viewLabel(view)})))
            setPartHidden(p->id, view, !hidden);
        bool rowHot = ui.hovered(rr);
        std::string lockIcon = p->locked ? "lock" : rowHot ? "unlock" : "";
        if (iconToggle(ui, "llock-" + rid, lock, lockIcon, p->locked ? th.text : th.textDim, p->locked ? "Unlock" : "Lock (can't be moved or deleted)"))
            setPartLocked(p->id, !p->locked);
        uint32_t alpha = hidden ? 110 : 255;
        Rect icon(x, rr.y + 4 * s, rowH - 10 * s, rowH - 10 * s);
        g.fillRoundRect(icon, 5 * s, rgba(255, 255, 255, alpha));
        drawPartIcon(p->def, icon.inset(2 * s));
        if (hidden) g.fillRoundRect(icon, 5 * s, withAlpha(th.panel, 140));
        float tx = icon.r() + 8 * s;
        // badges (right to left, before the lock button)
        float bx = lock.x - 6 * s;
        auto badge = [&](uint32_t col, const std::string& tip, bool ring = false) {
            Rect bb(bx - 12 * s, rr.center().y - 6 * s, 12 * s, 12 * s);
            if (ring) g.strokeCircle(bb.center(), 5 * s, 1.5f * s, col);
            else g.fillCircle(bb.center(), 5 * s, col);
            ui.tooltip(bb.inset(-4 * s), tip);
            bx -= 16 * s;
        };
        if (simOn && sim.damaged.count(p->id)) badge(th.danger, tr("layers.damaged"));
        auto br = sim.brightness.find(p->id);
        if (simOn && br != sim.brightness.end()) {
            uint32_t lc = p->def->sim == "led" ? ledDisplayColor(p->prop("color")) : th.ok;
            badge(withAlpha(lc, (int)(60 + 195 * std::clamp(br->second, 0.f, 1.f))), trf("layers.brightness", {std::to_string((int)std::lround(br->second * 100))}));
        }
        if (unrouted.count(p->id)) badge(rgba(235, 140, 30), trf("layers.unrouted", {viewLabel(view)}));
        if (p->def->sim.empty() && !p->def->isBreadboard && !p->def->isBoard && p->def->netProp.empty() && p->type != "via" && p->type != "hole" &&
            p->type.rfind("header", 0) != 0)
            badge(th.textDim, tr("layers.not_simulated"), true);
        // label + value, clipped to the space left
        float tw = bx - tx;
        std::string l1 = p->label;
        std::string l2 = partTitle(*p->def);
        if (!p->def->valueProp.empty() && p->def->valueProp != "pins") l2 = propValueLabel(*p->def, p->def->valueProp, p->prop(p->def->valueProp)) + " · " + l2;
        auto clip = [&](std::string t, float px) { return g.ellipsize(t, px, tw); };
        g.text({tx, rr.y + 13 * s}, clip(l1, 14 * s), 14 * s, hidden ? th.textDim : th.text, 0);
        g.text({tx, rr.y + 29 * s}, clip(l2, 11.5f * s), 11.5f * s, th.textDim, 0);
    }
    ui.endScroll();
}
