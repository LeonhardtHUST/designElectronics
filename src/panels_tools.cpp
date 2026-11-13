// designElectronics — the Tools collection: small stand-alone utilities.
//
// A tool is a sheet that works without a project (it opens from the start page as well
// as from the top bar's Tools menu). The first one is the resistor colour code
// calculator; the same widget is embedded in the Inspector for resistor parts, so the
// bands, the value and the printed marking always agree (see colorcode.h).
#include <algorithm>
#include <cmath>

#include "app.h"
#include "colorcode.h"
#include "i18n.h"

const std::vector<App::ToolEntry>& App::toolList() {
    static const std::vector<ToolEntry> v = {
        {"colorcode", "palette", &App::openColorCodeTool},
    };
    return v;
}

std::vector<MenuItem> App::toolsMenu() {
    std::vector<MenuItem> m;
    for (auto& t : toolList()) {
        MenuItem it;
 it.label = tr(std::string("tool.") + t.id + ".name");
        auto open = t.open;
        it.action = [this, open]() { (this->*open)(); };
 m.push_back(it);
    }
    return m;
}

namespace {

std::string colorName(int c) { return tr(std::string("cc.color.") + cc::colorKey(c)); }

const char* roleKey(cc::Role r) {
    switch (r) {
        case cc::DIGIT: return "cc.role.digit";
        case cc::MULTIPLIER: return "cc.role.multiplier";
        case cc::TOLERANCE: return "cc.role.tolerance";
 case cc::TEMPCO: return "cc.role.tempco";
    }
    return "cc.role.digit";
}

}  // namespace

// The calculator: a resistor with its bands (click one to select it), the palette for
// that band, and the value / tolerance fields. Editing either side rewrites the other.
bool App::colorCalcWidget(const std::string& id, float x, float& y, float w, cc::Spec& spec, int& sel) {
    float s = ui.s;
    auto& th = ui.th;
    bool changed = false;
    spec.bands = std::max(3, std::min(6, spec.bands));
    if (spec.bands == 3) spec.tolerance = 20;
    bool exact = true;
    cc::Bands bands = cc::encode(spec, &exact);
    sel = std::max(0, std::min(bands.n - 1, sel));

    // ---- how many bands ----
    g.text({x, y + 13 * s}, tr("cc.bands"), 13 * s, th.textDim, 0);
    float cw = 40 * s, gap = 4 * s;
    float x0 = x + w - 4 * cw - 3 * gap;
    for (int k = 3; k <= 6; k++) {
 Rect r(x0 + (float)(k - 3) * (cw + gap), y, cw, 26 * s);
        if (ui.button(id + "n" + std::to_string(k), r, std::to_string(k), BTN_CHIP, spec.bands == k) && k != spec.bands) {
     if (k == 3) spec.tolerance = 20;
     else if (spec.bands == 3) spec.tolerance = 5;
     spec.bands = k;
     changed = true;
        }
    }
    y += 36 * s;

    // ---- the resistor ----
    float cy = y + 32 * s;
    float bodyH = 46 * s, lead = 26 * s;
    g.line({x + 4 * s, cy}, {x + w - 4 * s, cy}, 3.5f * s, rgba(165, 165, 170));
    Rect body(x + lead, cy - bodyH / 2, w - 2 * lead, bodyH);
    g.fillRoundRect(body, 16 * s, rgba(222, 200, 150));
    g.strokeRoundRect(body, 16 * s, std::max(1.f, s), rgba(170, 145, 95));
    float bx[6], bw = 0;
    cc::bandLayout(bands.n, bx, bw);
    for (int i = 0; i < bands.n; i++) {
        Rect br(body.x + bx[i] * body.w, body.y + (i == 0 ? 0.f : 3 * s), bw * body.w, bodyH - (i == 0 ? 0.f : 6 * s));
        if (bands.color[i] != cc::NONE) g.fillRect(br, cc::colorRgba(bands.color[i]));
        else g.strokeRect(br, std::max(1.f, s), rgba(170, 145, 95));  // "no band" = ±20 %
        Rect hit(br.x - 3 * s, body.y - 6 * s, br.w + 6 * s, bodyH + 12 * s);
 if (ui.hotRect(hit) && in.pressed) sel = i;
        ui.tooltip(hit, trf("cc.band_tip", {std::to_string(i + 1), tr(roleKey(bands.role[i])), colorName(bands.color[i]), cc::meaning(bands.role[i], bands.color[i])}));
 if (i == sel) {
     g.strokeRect(Rect(br.x - 2 * s, body.y - 5 * s, br.w + 4 * s, bodyH + 10 * s), 2 * s, th.accent);
     g.fillConvex({{br.center().x - 5 * s, body.b() + 12 * s}, {br.center().x + 5 * s, body.b() + 12 * s}, {br.center().x, body.b() + 6 * s}}, th.accent);
        }
    }
    y += 78 * s;

    // ---- the palette for the selected band ----
    g.text({x, y + 8 * s},
    trf("cc.band_caption", {std::to_string(sel + 1), tr(roleKey(bands.role[sel])), colorName(bands.color[sel]), cc::meaning(bands.role[sel], bands.color[sel])}),
           13 * s, th.text, 0);
    y += 22 * s;
    std::vector<int> colors = cc::allowedColors(bands.role[sel], bands.n);
    float cell = 46 * s, sw = 28 * s;
    int perRow = std::max(1, (int)std::floor(w / cell));
    for (size_t k = 0; k < colors.size(); k++) {
        int c = colors[k];
        float cx = x + (float)((int)k % perRow) * cell, cyy = y + (float)((int)k / perRow) * (cell + 2 * s);
 Rect r(cx + (cell - sw) / 2, cyy, sw, sw);
        bool on = bands.color[sel] == c;
        if (c != cc::NONE) g.fillRoundRect(r, 5 * s, cc::colorRgba(c));
 else {
            g.fillRoundRect(r, 5 * s, rgba(255, 255, 255));
     g.line({r.x + 4 * s, r.b() - 4 * s}, {r.r() - 4 * s, r.y + 4 * s}, std::max(1.f, s), th.textDim);
        }
        g.strokeRoundRect(r, 5 * s, on ? 3 * s : std::max(1.f, s), on ? th.accent : th.border);
        g.text({cx + cell / 2, cyy + sw + 9 * s}, cc::meaning(bands.role[sel], c), 10.5f * s, on ? th.text : th.textDim, 1);
        Rect hit(cx, cyy, cell, cell);
 ui.tooltip(hit, colorName(c));
        if (ui.hotRect(hit) && in.pressed && !on) {
            cc::Bands nb = bands;
            nb.color[sel] = c;
     cc::Spec out = spec;
     if (cc::decode(nb, out)) {
         out.bands = spec.bands;
         if (spec.bands < 6) out.ppm = spec.ppm;
         spec = out;
         changed = true;
            }
        }
    }
    int rows = ((int)colors.size() + perRow - 1) / perRow;
    y += (float)rows * (cell + 2 * s) + 6 * s;

    // ---- value, tolerance, temperature coefficient ----
    float rowH = 34 * s, lw = std::min(120 * s, w * 0.4f);
    auto label = [&](const std::string& t) { g.text({x, y + rowH / 2}, t, 13 * s, th.textDim, 0); };
    label(tr("cc.resistance"));
    std::string text = cc::formatOhms(spec.ohms);
    if (ui.textField(id + "val", Rect(x + lw, y, w - lw, rowH - 4 * s), text)) {
        double v = cc::parseResistance(text, -1);
 if (v >= 0 && std::fabs(v - spec.ohms) > 1e-12) { spec.ohms = v; changed = true; }
    }
    y += rowH + 2 * s;
    if (spec.bands >= 4) {
        label(tr("cc.tolerance"));
 std::vector<std::string> opts;
        for (double t : cc::toleranceChoices()) opts.push_back(cc::formatTolerance(t));
        std::string tv = cc::formatTolerance(spec.tolerance);
        if (ui.comboField(id + "tol", Rect(x + lw, y, w - lw, rowH - 4 * s), tv, opts, false)) {
     spec.tolerance = cc::parseTolerance(tv, 5);
     changed = true;
 }
        y += rowH + 2 * s;
    }
    if (spec.bands == 6) {
        label(tr("cc.tempco"));
        std::vector<std::string> opts;
        for (int t : cc::tempcoChoices()) opts.push_back(cc::formatPpm(t));
        std::string pv = cc::formatPpm(spec.ppm);
        if (ui.comboField(id + "ppm", Rect(x + lw, y, w - lw, rowH - 4 * s), pv, opts, false)) {
     spec.ppm = cc::parsePpm(pv, 100);
     changed = true;
        }
        y += rowH + 2 * s;
    }

    // ---- what the bands say, and what would be printed on an SMD one ----
    cc::Spec shown;
    if (!exact && cc::decode(bands, shown))
 y += ui.wrappedText(Rect(x, y, w, 0), trf("cc.rounded", {std::to_string(bands.n), cc::formatOhms(shown.ohms)}), th.danger, 12 * s) + 4 * s;
    std::string read = cc::formatOhms(spec.ohms);
    if (spec.bands >= 4) read += "  " + cc::formatTolerance(spec.tolerance);
    else read += "  ±20%";
    if (spec.bands == 6) read += "  " + cc::formatPpm(spec.ppm);
    g.text({x, y + 10 * s}, read, 15 * s, th.text, 0, FONT_MONO);
    y += 24 * s;
    g.text({x, y + 8 * s}, trf("cc.marking", {cc::resistorMarking(spec.ohms, spec.tolerance)}), 12 * s, th.textDim, 0, FONT_MONO);
    y += 20 * s;
    return changed;
}

void App::openColorCodeTool() {
    // start from the selected resistor, so the tool can read a part's value back
    if (!startScreen && selParts.size() == 1) {
        PartInst* p = doc.part(*selParts.begin());
 if (p && p->type == "resistor") toolCalc = cc::specFromProps(mergedProps(p->type, p->props));
    }
    ui.openSheet("tool-colorcode", tr("tool.colorcode.name"), 540, 640, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float x = body.x, w = body.w;
        float oy = ui.beginScroll("tool-cc-body", body, 600 * s);
        float y = body.y + oy;
        y += ui.wrappedText(Rect(x, y, w, 0), tr("tool.colorcode.desc"), th.textDim, 12.5f * s) + 10 * s;
        colorCalcWidget("toolcc", x, y, w, toolCalc, toolCalcBand);
 y += 6 * s;
 PartInst* target = nullptr;
        if (!startScreen && selParts.size() == 1) {
     PartInst* p = doc.part(*selParts.begin());
     if (p && p->type == "resistor") target = p;
        }
        if (target) {
     int pid = target->id;
     if (ui.button("tool-cc-apply", Rect(x, y, w, 38 * s), trf("cc.apply", {target->label}), BTN_PRIMARY)) {
         setPartProps(pid, cc::propsFromSpec(toolCalc));
         ui.toast(trf("cc.applied", {target->label}));
     }
 } else {
     y += ui.wrappedText(Rect(x, y, w, 0), tr("cc.apply_hint"), th.textDim, 12 * s);
        }
        ui.endScroll();
    });
}
