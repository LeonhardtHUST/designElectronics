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

float resistorRadius(float t) {
    float end = std::min(t, 1 - t);
    if (end < .125f) {
        float ellipse = (end - .125f) / .125f;
        return .5f * std::sqrt(std::max(0.f, 1 - ellipse * ellipse));
    }
    if (end < .22f) return .5f;
    if (end >= .36f) return .32f;
    float blend = (end - .22f) / .14f;
    blend = blend * blend * (3 - 2 * blend);
    return .5f - .18f * blend;
}

void previewBandLayout(int count, float positions[6], float& width) {
    width = count >= 5 ? .045f : .055f;
    if (count >= 5) {
        const float six[] = {.145f, .345f, .455f, .565f, .755f, .855f};
        std::copy(six, six + 6, positions);
        if (count == 5) positions[4] = .83f;
    } else {
        positions[0] = .145f;
        positions[1] = .37f;
        positions[2] = .51f;
        positions[3] = .83f;
    }
}

void drawResistor(Gfx& gfx, Rect body, const cc::Bands& bands) {
    float positions[6], width = 0;
    previewBandLayout(bands.n, positions, width);
    const int slices = 240, rows = 40;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    auto vertex = [&](float x, float y, uint32_t color) {
        SDL_Vertex point{};
        point.position = {x, y};
        point.color = {(float)((color >> 24) & 255) / 255, (float)((color >> 16) & 255) / 255,
                       (float)((color >> 8) & 255) / 255, (float)(color & 255) / 255};
        vertices.push_back(point);
    };
    for (int slice = 0; slice < slices; ++slice) {
        float left = (float)slice / slices, right = (float)(slice + 1) / slices;
        uint32_t base = rgba(221, 199, 156);
        for (int band = 0; band < bands.n; ++band) {
            if ((left + right) / 2 >= positions[band] && (left + right) / 2 <= positions[band] + width && bands.color[band] != cc::NONE)
                base = cc::colorRgba(bands.color[band]);
        }
        float radiusLeft = resistorRadius(left) * body.h;
        float radiusRight = resistorRadius(right) * body.h;
        for (int row = 0; row < rows; ++row) {
            float top = -1 + 2.f * row / rows, bottom = -1 + 2.f * (row + 1) / rows;
            float height = (top + bottom) / 2;
            uint32_t color = mixColor(base, rgba(68, 48, 28), .25f * std::pow(std::fabs(height), 2.f));
            float highlight = .30f * std::exp(-std::pow((height + .43f) / .28f, 2.f));
            color = mixColor(color, rgba(255, 255, 255), highlight);
            int first = (int)vertices.size();
            vertex(body.x + left * body.w, body.center().y + top * radiusLeft, color);
            vertex(body.x + right * body.w, body.center().y + top * radiusRight, color);
            vertex(body.x + right * body.w, body.center().y + bottom * radiusRight, color);
            vertex(body.x + left * body.w, body.center().y + bottom * radiusLeft, color);
            indices.insert(indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
        }
    }
    gfx.flush();
    SDL_RenderGeometry(gfx.renderer(), nullptr, vertices.data(), (int)vertices.size(), indices.data(), (int)indices.size());
    std::vector<Vec2> outline;
    for (int slice = 0; slice <= slices; ++slice) {
        float t = (float)slice / slices;
        outline.push_back({body.x + t * body.w, body.center().y - resistorRadius(t) * body.h});
    }
    for (int slice = slices; slice >= 0; --slice) {
        float t = (float)slice / slices;
        outline.push_back({body.x + t * body.w, body.center().y + resistorRadius(t) * body.h});
    }
    gfx.polyline(outline, .8f, rgba(117, 99, 70, 150), true);
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

    // ---- polished calculator card ----
    {
        const float cardPad = 14 * s;
        Rect card(x, y, w, 146 * s);
        g.fillRoundRect(Rect(card.x + 2 * s, card.y + 4 * s, card.w, card.h), 14 * s, rgba(0, 0, 0, 24));
        g.fillRoundRect(card, 14 * s, th.panel2);
        g.strokeRoundRect(card, 14 * s, std::max(1.f, s), th.border);
        float bw = 38 * s, bg = 6 * s;
        float bandButtons = 4 * bw + 3 * bg;
        g.text({card.x + cardPad, card.y + 28 * s}, tr("cc.bands"), 12 * s, th.textDim, 0);
        float bx0 = card.r() - cardPad - bandButtons;
        for (int k = 3; k <= 6; k++) {
            Rect br(bx0 + (float)(k - 3) * (bw + bg), card.y + 14 * s, bw, 28 * s);
            if (ui.button(id + "n" + std::to_string(k), br, std::to_string(k), BTN_CHIP, spec.bands == k) && k != spec.bands) {
                if (k == 3) spec.tolerance = 20;
                else if (spec.bands == 3) spec.tolerance = 5;
                spec.bands = k;
                changed = true;
            }
        }
        float cy = card.y + 86 * s;
        float bodyW = std::min(252 * s, card.w * .66f);
        float bodyH = bodyW * .29f;
        Rect body(card.center().x - bodyW / 2, cy - bodyH / 2, bodyW, bodyH);
        float leadLength = std::min(58 * s, (card.w - bodyW) / 2 - 14 * s);
        g.line({body.x - leadLength, cy + s}, {body.r() + leadLength, cy + s}, 4 * s, rgba(140, 151, 156));
        g.line({body.x - leadLength, cy - .5f * s}, {body.r() + leadLength, cy - .5f * s}, 1.2f * s, rgba(219, 227, 230));
        drawResistor(g, body, bands);
        float bandX[6], bandW = 0;
        previewBandLayout(bands.n, bandX, bandW);
        for (int band = 0; band < bands.n; ++band) {
            float center = body.x + (bandX[band] + bandW / 2) * body.w;
            Rect hit(center - std::max(10 * s, body.w * bandW), body.y - 4 * s,
                     std::max(20 * s, body.w * bandW * 2), body.h + 16 * s);
            if (ui.hotRect(hit) && in.pressed) sel = band;
            ui.tooltip(hit, trf("cc.band_tip", {std::to_string(band + 1), tr(roleKey(bands.role[band])), colorName(bands.color[band]), cc::meaning(bands.role[band], bands.color[band])}));
            if (band == sel) {
                g.fillRoundRect(Rect(center - 7 * s, body.b() + 7 * s, 14 * s, 2 * s), s, th.accent);
            }
        }
        y += card.h + 14 * s;
    }

    // ---- selected band and circular color palette ----
    g.text({x, y + 12 * s}, trf("cc.band_caption", {std::to_string(sel + 1), tr(roleKey(bands.role[sel])), colorName(bands.color[sel]), cc::meaning(bands.role[sel], bands.color[sel])}), 13 * s, th.text, 0);
    y += 28 * s;
    std::vector<int> colors = cc::allowedColors(bands.role[sel], bands.n);
    int perRow = std::max(1, std::min((int)colors.size(), (int)std::floor(w / (42 * s))));
    float cell = w / perRow, sw = 27 * s;
    for (size_t k = 0; k < colors.size(); k++) {
        int c = colors[k];
        float cx = x + (float)((int)k % perRow) * cell;
        float cyy = y + (float)((int)k / perRow) * 46 * s;
        Vec2 center{cx + cell / 2, cyy + sw / 2};
        bool on = bands.color[sel] == c;
        if (c == cc::NONE) {
            g.fillCircle(center, sw / 2, rgba(246, 247, 249));
            g.line({center.x - 10 * s, center.y + 10 * s}, {center.x + 10 * s, center.y - 10 * s}, 2 * s, th.textDim);
        } else {
            g.fillCircle(center, sw / 2, cc::colorRgba(c));
            g.fillCircle({center.x - 7 * s, center.y - 8 * s}, 5 * s, rgba(255, 255, 255, 55));
        }
        g.strokeCircle(center, sw / 2, on ? 3 * s : std::max(1.f, s), on ? th.accent : th.border);
        g.text({center.x, cyy + sw + 12 * s}, cc::meaning(bands.role[sel], c), 10.5f * s, on ? th.text : th.textDim, 1);
        Rect hit(cx, cyy - 3 * s, cell, 54 * s);
        ui.tooltip(hit, colorName(c));
        if (ui.hotRect(hit) && in.pressed && !on) {
            cc::Bands nb = bands;
            nb.color[sel] = c;
            cc::Spec out = spec;
            if (cc::decode(nb, out)) { out.bands = spec.bands; if (spec.bands < 6) out.ppm = spec.ppm; spec = out; changed = true; }
        }
    }
    int rows = ((int)colors.size() + perRow - 1) / perRow;
    y += (float)rows * 46 * s + 12 * s;

    // ---- value controls ----
    bool wideFields = w >= 380 * s;
    float fieldsHeight = spec.bands >= 4 && !wideFields ? 126 * s : 72 * s;
    Rect valueCard(x, y, w, fieldsHeight);
    g.fillRoundRect(valueCard, 12 * s, th.panel2);
    g.strokeRoundRect(valueCard, 12 * s, std::max(1.f, s), th.border);
    float innerX = x + 14 * s, innerW = w - 28 * s;
    float fieldW = wideFields && spec.bands >= 4 ? (innerW - 14 * s) / 2 : innerW;
    g.text({innerX, y + 20 * s}, tr("cc.resistance"), 11 * s, th.textDim, 0);
    std::string text = cc::formatOhms(spec.ohms);
    if (ui.textField(id + "val", Rect(innerX, y + 28 * s, fieldW, 32 * s), text)) {
        double v = cc::parseResistance(text, -1);
        if (v >= 0 && std::fabs(v - spec.ohms) > 1e-12) { spec.ohms = v; changed = true; }
    }
    if (spec.bands >= 4) {
        float toleranceX = wideFields ? innerX + fieldW + 14 * s : innerX;
        float toleranceY = wideFields ? y : y + 58 * s;
        g.text({toleranceX, toleranceY + 20 * s}, tr("cc.tolerance"), 11 * s, th.textDim, 0);
        std::vector<std::string> opts;
        for (double t : cc::toleranceChoices()) opts.push_back(cc::formatTolerance(t));
        std::string tv = cc::formatTolerance(spec.tolerance);
        if (ui.comboField(id + "tol", Rect(toleranceX, toleranceY + 28 * s, fieldW, 32 * s), tv, opts, false)) { spec.tolerance = cc::parseTolerance(tv, 5); changed = true; }
    }
    y += valueCard.h + 12 * s;
    if (spec.bands == 6) {
        g.text({x, y + 12 * s}, tr("cc.tempco"), 11 * s, th.textDim, 0);
        std::vector<std::string> opts;
        for (int t : cc::tempcoChoices()) opts.push_back(cc::formatPpm(t));
        std::string pv = cc::formatPpm(spec.ppm);
        if (ui.comboField(id + "ppm", Rect(x, y + 20 * s, w, 32 * s), pv, opts, false)) { spec.ppm = cc::parsePpm(pv, 100); changed = true; }
        y += 64 * s;
    }

    // ---- result card ----
    cc::Spec shown;
    if (!exact && cc::decode(bands, shown)) y += ui.wrappedText(Rect(x, y, w, 0), trf("cc.rounded", {std::to_string(bands.n), cc::formatOhms(shown.ohms)}), th.danger, 12 * s) + 6 * s;
    Rect result(x, y, w, 72 * s);
    g.fillRoundRect(result, 12 * s, rgba(28, 116, 222, 20));
    g.strokeRoundRect(result, 12 * s, std::max(1.f, s), withAlpha(th.accent, 80));
    std::string read = cc::formatOhms(spec.ohms);
    if (spec.bands >= 4) read += "  " + cc::formatTolerance(spec.tolerance);
    else read += "  ±20%";
    if (spec.bands == 6) read += "  " + cc::formatPpm(spec.ppm);
    g.text({result.x + 14 * s, result.y + 25 * s}, read, 16 * s, th.text, 0, FONT_MONO);
    g.text({result.x + 14 * s, result.y + 50 * s}, trf("cc.marking", {cc::resistorMarking(spec.ohms, spec.tolerance)}), 11 * s, th.textDim, 0, FONT_MONO);
    y += result.h + 16 * s;
    return changed;

}

void App::openColorCodeTool() {
    // start from the selected resistor, so the tool can read a part's value back
    if (!startScreen && selParts.size() == 1) {
        PartInst* p = doc.part(*selParts.begin());
 if (p && p->type == "resistor") toolCalc = cc::specFromProps(mergedProps(p->type, p->props));
    }
    ui.openSheet("tool-colorcode", tr("tool.colorcode.name"), 560, 560, [this](Rect body) {
        float originalScale = ui.s;
        ui.s = std::min(ui.s, body.h / 540.f);
        float s = ui.s;
        auto& th = ui.th;
        float x = body.x, w = body.w;
        float y = body.y + 10 * s;
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

        ui.s = originalScale;
    });
}
