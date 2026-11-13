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
    auto command = [](const std::string& label, std::function<void()> action, bool enabled = true) {
        MenuItem entry;
        entry.label = label;
        entry.action = std::move(action);
        entry.enabled = enabled;
        return entry;
    };
    std::vector<MenuItem> m;
    for (auto& t : toolList()) {
        MenuItem it;
 it.label = tr(std::string("tool.") + t.id + ".name");
        auto open = t.open;
        it.action = [this, open]() { (this->*open)(); };
 m.push_back(it);
    }
    m.push_back(MenuItem::sep());
    MenuItem simulation;
    simulation.label = tr("menu.tools.simulation");
    simulation.children = {
        command(tr(simOn ? "menu.routing.stop_sim" : "menu.routing.start_sim"), [this]() { toggleSim(); }, !startScreen && !readOnly),
        command(tr("menu.tools.restart_sim"), [this]() { restartSimulation(); }, !startScreen && !readOnly && simOn),
        command(tr("menu.tools.simulation_details"), [this]() { openSimulationDetails(); }),
    };
    m.push_back(std::move(simulation));
    return m;
}

void App::restartSimulation() {
    if (startScreen || readOnly || !simOn) return;
    bool wasRunning = simOn;
    simulator.reset();
    sim = SimResult();
    simRev = -1;
    lastSimTicks = 0;
    simPressed.clear();
    simPressedLast.clear();
    serialLog.clear();
    serialSend.clear();
    simNotesOpen = false;
    if (wasRunning && view == PCB) setView(BB);
    ui.toast(tr("toast.sim_restarted"));
}

void App::openSimulationDetails() {
    ui.scrollTo("simulation-details-body", 0);
    ui.openSheet("simulation-details", tr("menu.tools.simulation_details"), 640, 560, [this](Rect body) {
        float scale = ui.s;
        float width = std::max(1.f, body.w - 14 * scale);
        std::vector<std::pair<std::string, uint32_t>> rows;
        rows.emplace_back(tr("sim.details.status") + ": " + tr(simOn ? "sim.details.running" : "sim.details.idle"), ui.th.text);
        if (!sim.ran && sim.time <= 0) {
            rows.emplace_back(tr("sim.details.empty"), ui.th.textDim);
        } else {
            rows.emplace_back(trf("sim.details.time", {num(sim.time, 3)}), ui.th.text);
            if (!sim.message.empty() && sim.message != tr("sim.simulating") && sim.message != tr("overlay.simulating"))
                rows.emplace_back(sim.message, ui.th.text);
            rows.emplace_back(trf("sim.details.warnings", {std::to_string(sim.warnings.size())}), sim.warnings.empty() ? ui.th.textDim : ui.th.danger);
            for (const auto& warning : sim.warnings) rows.emplace_back("• " + warning, ui.th.danger);
            rows.emplace_back(tr("sim.details.notes"), ui.th.text);
            if (sim.notes.empty()) rows.emplace_back(tr("sim.details.no_notes"), ui.th.textDim);
            for (const auto& note : sim.notes) rows.emplace_back("• " + note, ui.th.textDim);
        }
        float contentHeight = 8 * scale;
        for (const auto& row : rows)
            contentHeight += ui.wrappedText(Rect(0, 0, width, 0), row.first, row.second, 14 * scale, false) + 10 * scale;
        float offset = ui.beginScroll("simulation-details-body", body, contentHeight);
        float top = body.y + offset;
        for (const auto& row : rows)
            top += ui.wrappedText(Rect(body.x, top, width, 0), row.first, row.second, 14 * scale) + 10 * scale;
        ui.endScroll();
    });
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

std::string colorCalcResult(const cc::Spec& spec) {
    std::string value = cc::formatOhms(spec.ohms) + "  " + (spec.bands >= 4 ? cc::formatTolerance(spec.tolerance) : "±20%");
    if (spec.bands == 6) value += "  " + cc::formatPpm(spec.ppm);
    return value;
}

struct ColorCalcLayout {
    int bandColumns = 4;
    int perRow = 1;
    int rows = 1;
    float captionHeight = 0;
    float cardHeight = 0;
    float fieldsHeight = 0;
    float warningHeight = 0;
    float resultHeight = 0;
    std::vector<std::string> resultLines;
    float height = 0;
    bool wideFields = false;
};

ColorCalcLayout colorCalcLayout(UI& ui, float width, const cc::Spec& spec, int selected) {
    ColorCalcLayout layout;
    float scale = ui.s;
    bool exact = true;
    cc::Bands bands = cc::encode(spec, &exact);
    selected = std::clamp(selected, 0, bands.n - 1);
    std::string caption = trf("cc.band_caption", {std::to_string(selected + 1), tr(roleKey(bands.role[selected])),
                                               colorName(bands.color[selected]), cc::meaning(bands.role[selected], bands.color[selected])});
    layout.bandColumns = width < 210 * scale ? 2 : 4;
    layout.cardHeight = (width < 300 * scale ? 178.f : 146.f) * scale + (layout.bandColumns == 2 ? 32 * scale : 0);
    layout.resultLines = wrapText(*ui.g, colorCalcResult(spec), 20 * scale, std::max(20 * scale, width - 28 * scale), FONT_MONO);
    layout.resultHeight = (86 + std::max(0, (int)layout.resultLines.size() - 1) * 27) * scale;
    layout.captionHeight = ui.wrappedText(Rect(0, 0, width, 0), caption, ui.th.text, 15 * scale, false);
    int colors = (int)cc::allowedColors(bands.role[selected], bands.n).size();
    layout.perRow = std::max(1, std::min(colors, (int)std::floor(width / (46 * scale))));
    layout.rows = (colors + layout.perRow - 1) / layout.perRow;
    layout.wideFields = width >= 380 * scale;
    layout.fieldsHeight = (spec.bands >= 4 && !layout.wideFields ? 140.f : 80.f) * scale;
    cc::Spec shown;
    if (!exact && cc::decode(bands, shown))
        layout.warningHeight = ui.wrappedText(Rect(0, 0, width, 0), trf("cc.rounded", {std::to_string(bands.n), cc::formatOhms(shown.ohms)}), ui.th.danger, 14 * scale, false) + 6 * scale;
    layout.height = layout.cardHeight + 14 * scale + layout.captionHeight + 8 * scale + layout.rows * 54 * scale + 12 * scale +
                    layout.fieldsHeight + 12 * scale + (spec.bands == 6 ? 72 * scale : 0) + layout.warningHeight + layout.resultHeight + 16 * scale;
    return layout;
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
    ColorCalcLayout layout = colorCalcLayout(ui, w, spec, sel);

    // ---- polished calculator card ----
    {
        const float cardPad = 14 * s;
        Rect card(x, y, w, layout.cardHeight);
        g.fillRoundRect(Rect(card.x + 2 * s, card.y + 4 * s, card.w, card.h), 14 * s, rgba(0, 0, 0, 24));
        g.fillRoundRect(card, 14 * s, th.panel2);
        g.strokeRoundRect(card, 14 * s, std::max(1.f, s), th.border);
        float bg = 6 * s;
        int columns = layout.bandColumns;
        float bw = std::min(38 * s, (w - 2 * cardPad - (columns - 1) * bg) / columns);
        float bandButtons = columns * bw + (columns - 1) * bg;
        g.text({card.x + cardPad, card.y + 28 * s}, tr("cc.bands"), 14 * s, th.textDim, 0);
        bool stackedBands = w < 300 * s;
        float bx0 = stackedBands ? card.center().x - bandButtons / 2 : card.r() - cardPad - bandButtons;
        for (int k = 3; k <= 6; k++) {
            Rect br(bx0 + (float)((k - 3) % columns) * (bw + bg), card.y + ((stackedBands ? 46 : 14) + (k - 3) / columns * 32) * s, bw, 28 * s);
            if (ui.button(id + "n" + std::to_string(k), br, std::to_string(k), BTN_CHIP, spec.bands == k) && k != spec.bands) {
                if (k == 3) spec.tolerance = 20;
                else if (spec.bands == 3) spec.tolerance = 5;
                spec.bands = k;
                changed = true;
            }
        }
        float cy = card.y + ((stackedBands ? 118 : 86) + (columns == 2 ? 32 : 0)) * s;
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
    y += ui.wrappedText(Rect(x, y, w, 0), trf("cc.band_caption", {std::to_string(sel + 1), tr(roleKey(bands.role[sel])), colorName(bands.color[sel]), cc::meaning(bands.role[sel], bands.color[sel])}), th.text, 15 * s) + 8 * s;
    std::vector<int> colors = cc::allowedColors(bands.role[sel], bands.n);
    int perRow = layout.perRow;
    float cell = w / perRow, sw = 28 * s;
    for (size_t k = 0; k < colors.size(); k++) {
        int c = colors[k];
        float cx = x + (float)((int)k % perRow) * cell;
        float cyy = y + (float)((int)k / perRow) * 54 * s;
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
        g.text({center.x, cyy + sw + 12 * s}, cc::meaning(bands.role[sel], c), 13 * s, on ? th.text : th.textDim, 1);
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
    y += (float)rows * 54 * s + 12 * s;

    // ---- value controls ----
    bool wideFields = layout.wideFields;
    float fieldsHeight = layout.fieldsHeight;
    Rect valueCard(x, y, w, fieldsHeight);
    g.fillRoundRect(valueCard, 12 * s, th.panel2);
    g.strokeRoundRect(valueCard, 12 * s, std::max(1.f, s), th.border);
    float innerX = x + 14 * s, innerW = w - 28 * s;
    float fieldW = wideFields && spec.bands >= 4 ? (innerW - 14 * s) / 2 : innerW;
    g.text({innerX, y + 20 * s}, tr("cc.resistance"), 14 * s, th.textDim, 0);
    std::string text = cc::formatOhms(spec.ohms);
    if (ui.textField(id + "val", Rect(innerX, y + 32 * s, fieldW, 36 * s), text)) {
        double v = cc::parseResistance(text, -1);
        if (v >= 0 && std::fabs(v - spec.ohms) > 1e-12) { spec.ohms = v; changed = true; }
    }
    if (spec.bands >= 4) {
        float toleranceX = wideFields ? innerX + fieldW + 14 * s : innerX;
        float toleranceY = wideFields ? y : y + 60 * s;
        g.text({toleranceX, toleranceY + 20 * s}, tr("cc.tolerance"), 14 * s, th.textDim, 0);
        std::vector<std::string> opts;
        for (double t : cc::toleranceChoices()) opts.push_back(cc::formatTolerance(t));
        std::string tv = cc::formatTolerance(spec.tolerance);
        if (ui.comboField(id + "tol", Rect(toleranceX, toleranceY + 32 * s, fieldW, 36 * s), tv, opts, false)) { spec.tolerance = cc::parseTolerance(tv, 5); changed = true; }
    }
    y += valueCard.h + 12 * s;
    if (spec.bands == 6) {
        g.text({x, y + 12 * s}, tr("cc.tempco"), 14 * s, th.textDim, 0);
        std::vector<std::string> opts;
        for (int t : cc::tempcoChoices()) opts.push_back(cc::formatPpm(t));
        std::string pv = cc::formatPpm(spec.ppm);
        if (ui.comboField(id + "ppm", Rect(x, y + 26 * s, w, 36 * s), pv, opts, false)) { spec.ppm = cc::parsePpm(pv, 100); changed = true; }
        y += 72 * s;
    }

    // ---- result card ----
    cc::Spec shown;
    if (!exact && cc::decode(bands, shown)) y += ui.wrappedText(Rect(x, y, w, 0), trf("cc.rounded", {std::to_string(bands.n), cc::formatOhms(shown.ohms)}), th.danger, 14 * s) + 6 * s;
    Rect result(x, y, w, layout.resultHeight);
    g.fillRoundRect(result, 12 * s, rgba(28, 116, 222, 20));
    g.strokeRoundRect(result, 12 * s, std::max(1.f, s), withAlpha(th.accent, 80));
    for (size_t line = 0; line < layout.resultLines.size(); ++line)
        g.text({result.x + 14 * s, result.y + (29 + 27 * line) * s}, layout.resultLines[line], 20 * s, th.text, 0, FONT_MONO);
    std::string marking = trf("cc.marking", {cc::resistorMarking(spec.ohms, spec.tolerance)});
    g.text({result.x + 14 * s, result.b() - 24 * s}, g.ellipsize(marking, 14 * s, std::max(0.f, w - 28 * s), FONT_MONO), 14 * s, th.textDim, 0, FONT_MONO);
    ui.tooltip(result, marking);
    y += result.h + 16 * s;
    return changed;

}

void App::openColorCodeTool() {
    // start from the selected resistor, so the tool can read a part's value back
    if (!startScreen && selParts.size() == 1) {
        PartInst* p = doc.part(*selParts.begin());
 if (p && p->type == "resistor") toolCalc = cc::specFromProps(mergedProps(p->type, p->props));
    }
    ui.openSheet("tool-colorcode", tr("tool.colorcode.name"), 640, 700, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        PartInst* target = nullptr;
        if (!startScreen && selParts.size() == 1) {
            PartInst* part = doc.part(*selParts.begin());
            if (part && part->type == "resistor") target = part;
        }
        auto contentHeight = [&](float width) {
            float description = ui.wrappedText(Rect(0, 0, width, 0), tr("tool.colorcode.desc"), th.textDim, 14.5f * s, false);
            float footer = target ? 40 * s : ui.wrappedText(Rect(0, 0, width, 0), tr("cc.apply_hint"), th.textDim, 14 * s, false);
            return 20 * s + description + colorCalcLayout(ui, width, toolCalc, toolCalcBand).height + 6 * s + footer;
        };
        float width = body.w;
        if (contentHeight(width) > body.h) width -= 16 * s;
        float height = contentHeight(width);
        float offset = ui.beginScroll("tool-cc-body", body, height);
        float x = body.x, y = body.y + offset + 10 * s;
        y += ui.wrappedText(Rect(x, y, width, 0), tr("tool.colorcode.desc"), th.textDim, 14.5f * s) + 10 * s;
        colorCalcWidget("toolcc", x, y, width, toolCalc, toolCalcBand);
        y += 6 * s;
        if (target) {
            int partId = target->id;
            if (ui.button("tool-cc-apply", Rect(x, y, width, 40 * s), trf("cc.apply", {target->label}), BTN_PRIMARY)) {
                setPartProps(partId, cc::propsFromSpec(toolCalc));
                ui.toast(trf("cc.applied", {target->label}));
            }
        } else {
            ui.wrappedText(Rect(x, y, width, 0), tr("cc.apply_hint"), th.textDim, 14 * s);
        }
        ui.endScroll();
    });
}
