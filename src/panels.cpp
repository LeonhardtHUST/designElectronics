// Panels: top bar + menus, parts bin, inspector, bottom toolbar, canvas overlays.
#include <cmath>
#include <cstdio>

#include "app.h"
#include "i18n.h"
#include "logo.h"
#include "partmodels.h"

namespace {
MenuItem item(const std::string& l, std::function<void()> a, const std::string& sc = "", bool enabled = true, bool checked = false) {
    MenuItem m;
    m.label = l;
    m.action = std::move(a);
    m.shortcut = sc;
    m.enabled = enabled;
    m.checked = checked;
    return m;
}
MenuItem sub(const std::string& l, std::vector<MenuItem> children) {
    MenuItem m;
    m.label = l;
    m.children = std::move(children);
    return m;
}
std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
#if defined(__APPLE__)
const char* kCmd = "⌘";
#else
const char* kCmd = "Ctrl+";
#endif
std::string sc(const char* k) { return std::string(kCmd) + k; }
}  // namespace

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

std::vector<MenuItem> App::fileMenu() {
    std::vector<MenuItem> ex;
    for (size_t i = 0; i < examples().size(); i++) {
        // Example titles double as document titles, so the key is derived from the
        // English text rather than an index that shifts when examples are added.
        std::string key = "ex." + slugify(examples()[i].title) + ".title";
        std::string label = i18n::exists(key.c_str()) ? tr(key) : examples()[i].title;
        ex.push_back(item(label, [this, i]() { loadExample(i); }));
    }
    std::vector<MenuItem> exp = {
        item(tr("export.video"), [this]() { videoView = view; openVideoSheet(); }),
        MenuItem::sep(),
        item(tr("export.gerber"), [this]() { exportFile("gerber"); }),
        item(tr("export.svg"), [this]() { exportFile("svg"); }),
        item(tr("export.png"), [this]() { exportFile("png"); }),
        MenuItem::sep(),
        item(tr("export.bom"), [this]() { exportFile("bom"); }),
        item(tr("export.netlist"), [this]() { exportFile("netlist"); }),
        item(tr("export.spice"), [this]() { exportFile("spice"); }),
    };
    std::vector<MenuItem> m = {
        item(tr("menu.file.new"), [this]() { newDoc(); }, sc("N")),
        item(tr("menu.file.open"), [this]() { openDialog(); }, sc("O")),
        sub(tr("menu.file.examples"), ex),
        MenuItem::sep(),
        item(tr("menu.file.save"), [this]() { save(); }, sc("S")),
        item(tr("menu.file.save_as"), [this]() { saveAs(); }, sc("⇧S")),
        item(tr("menu.file.embed"), [this]() { openEmbedSheet(); }),
        MenuItem::sep(),
        item(tr("menu.file.history"), [this]() { openHistory(); }, sc("⇧H")),
        item(tr("menu.file.commit"), [this]() { openCommitSheet(); }, sc("⇧K")),
        githubConnected() ? item(trf("menu.file.github_account", {gh.login}), [this]() { openGitHubAccount(); })
                          : item(tr("menu.file.github_connect"), [this]() { openGitHubAccount(); }),
        MenuItem::sep(),
        sub(tr("menu.file.export"), exp),
        item(tr("menu.file.import_part"), [this]() { importPartDialog(); }),
    };
#if !defined(SDL_PLATFORM_IOS) && !defined(SDL_PLATFORM_ANDROID)
    m.push_back(MenuItem::sep());
    m.push_back(item(tr("menu.file.quit"), [this]() {
        SDL_Event e;
        e.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&e);
    }, sc("Q")));
#endif
    return m;
}

std::vector<MenuItem> App::editMenu() {
    bool any = !selParts.empty() || !selWires.empty() || !selNotes.empty();
    return {
        item(tr("menu.edit.undo"), [this]() { undo(); }, sc("Z"), !undoStack.empty()),
        item(tr("menu.edit.redo"), [this]() { redo(); }, sc("⇧Z"), !redoStack.empty()),
        MenuItem::sep(),
        item(tr("menu.edit.cut"), [this]() { copySelection(true); }, sc("X"), any),
        item(tr("menu.edit.copy"), [this]() { copySelection(false); }, sc("C"), any),
        item(tr("menu.edit.paste"), [this]() { paste(); }, sc("V")),
        item(tr("menu.edit.duplicate"), [this]() { duplicate(); }, sc("D"), any),
        item(tr("menu.edit.delete"), [this]() { deleteSelection(); }, "Del", any),
        MenuItem::sep(),
        item(tr("menu.edit.select_all"), [this]() { selectAll(); }, sc("A")),
        item(tr("menu.edit.add_note"), [this]() { addNote(); }),
    };
}

std::vector<MenuItem> App::partMenu() {
    bool any = !selParts.empty();
    return {
        item(tr("menu.part.rotate_cw"), [this]() { rotateSelection(1); }, "R", any),
        item(tr("menu.part.rotate_ccw"), [this]() { rotateSelection(-1); }, "⇧R", any),
        item(tr("menu.part.flip"), [this]() { flipSelection(); }, "F", any),
        MenuItem::sep(),
        item(tr("menu.part.front"), [this]() { bringToFront(true); }, "", any),
        item(tr("menu.part.back"), [this]() { bringToFront(false); }, "", any),
        MenuItem::sep(),
        item(tr("menu.part.export_def"), [this]() { exportFile("part"); }, "", selParts.size() == 1),
        item(tr("menu.part.import"), [this]() { importPartDialog(); }),
    };
}

std::vector<MenuItem> App::viewMenu() {
    auto scaleItem = [this](const std::string& l, float v) { return item(l, [this, v]() { userScale = v; }, "", true, std::fabs(userScale - v) < 0.01f); };
    // One entry per compiled-in/loaded pack, labelled in its own language.
    std::vector<MenuItem> langs;
    for (auto& l : i18n::languages()) {
        std::string code = l.code;
        langs.push_back(item(l.nativeName, [this, code]() { setLanguage(code); }, "", true, i18n::lang() == code));
    }
    return {
        item(tr("menu.view.breadboard"), [this]() { setView(BB); }, sc("1"), true, view == BB),
        item(tr("menu.view.schematic"), [this]() { setView(SCH); }, sc("2"), true, view == SCH),
        item(tr("menu.view.pcb"), [this]() { setView(PCB); }, sc("3"), true, view == PCB),
        MenuItem::sep(),
        item(tr("menu.view.zoom_in"), [this]() { zoomAt(1.25f, rCanvas.center()); }, "+"),
        item(tr("menu.view.zoom_out"), [this]() { zoomAt(0.8f, rCanvas.center()); }, "−"),
        item(tr("menu.view.fit"), [this]() { fitView(view, rCanvas); }, "0"),
        MenuItem::sep(),
        item(tr("menu.view.show_grid"), [this]() { showGrid = !showGrid; }, "", true, showGrid),
        item(tr("menu.view.snap"), [this]() { snapToGrid = !snapToGrid; }, "", true, snapToGrid),
        item(tr("menu.view.ratsnest"), [this]() { showRatsnest = !showRatsnest; }, "", true, showRatsnest),
        item(tr("menu.view.labels"), [this]() { showPartLabels = !showPartLabels; }, "", true, showPartLabels),
        MenuItem::sep(),
        sub(tr("menu.view.interface_size"), {scaleItem(tr("menu.view.scale.small"), 0.85f), scaleItem(tr("menu.view.scale.normal"), 1.0f),
                                            scaleItem(tr("menu.view.scale.large"), 1.2f), scaleItem(tr("menu.view.scale.xl"), 1.45f)}),
        sub(tr("menu.view.language"), langs),
    };
}

std::vector<MenuItem> App::routingMenu() {
    std::vector<MenuItem> widths;
    const char* widthKeys[] = {"menu.routing.width.xthin", "menu.routing.width.thin", "menu.routing.width.standard",
                               "menu.routing.width.thick", "menu.routing.width.xthick"};
    const float widthValues[] = {8, 12, 24, 32, 48};
    for (int i = 0; i < 5; i++) {
        float v = widthValues[i];
        widths.push_back(item(tr(widthKeys[i]), [this, v]() { doc.rules.traceWidth = v; changed(); }, "", true, std::fabs(doc.rules.traceWidth - v) < 0.1f));
    }
    return {
        item(tr("menu.routing.tidy"), [this]() { tidyCommand(); }, "T"),
        item(tr("menu.routing.tidy_auto"), [this]() { autoTidy = !autoTidy; }, "", true, autoTidy),
        MenuItem::sep(),
        item(tr("menu.routing.autoroute"), [this]() { setView(PCB); runAutoroute(); }),
        item(tr("menu.routing.clear"), [this]() {
            checkpoint();
            doc.wires.erase(std::remove_if(doc.wires.begin(), doc.wires.end(), [](const Wire& w) { return w.view == PCB && w.autorouted; }), doc.wires.end());
            changed();
        }),
        item(tr("menu.routing.drc"), [this]() { setView(PCB); runDrc(); }),
        MenuItem::sep(),
        sub(tr("menu.routing.active_layer"), {item(tr("menu.routing.layer.top"), [this]() { activeLayer = TOP; }, "", true, activeLayer == TOP),
                                              item(tr("menu.routing.layer.bottom"), [this]() { activeLayer = BOTTOM; }, "", true, activeLayer == BOTTOM)}),
        sub(tr("menu.routing.trace_width"), widths),
        MenuItem::sep(),
        item(tr(simOn ? "menu.routing.stop_sim" : "menu.routing.start_sim"), [this]() { toggleSim(); }),
    };
}

std::vector<MenuItem> App::helpMenu() {
    return {
        item(tr("menu.help.shortcuts"), [this]() { showShortcuts(); }),
        item(tr("menu.help.dez_format"), [this]() {
            ui.dialog(tr("dlg.dez_format.title"), {tr("dlg.dez_format.p1"), "", tr("dlg.dez_format.p2"), "", tr("dlg.dez_format.p3")}, {});
        }),
        item(tr("menu.help.about"), [this]() { showAbout(); }),
    };
}

std::vector<MenuItem> App::allMenus() {
    return {sub(tr("menu.title.file"), fileMenu()), sub(tr("menu.title.edit"), editMenu()), sub(tr("menu.title.part"), partMenu()),
            sub(tr("menu.title.view"), viewMenu()), sub(tr("menu.title.routing"), routingMenu()), sub(tr("menu.title.help"), helpMenu())};
}

// ---------------------------------------------------------------------------
// Top bar
// ---------------------------------------------------------------------------

void App::drawTopBar() {
    float s = ui.s;
    Rect r = rTop;
    g.fillRect(Rect(0, 0, (float)winW, r.b()), ui.th.panel2);
    g.fillRect({0, r.b() - std::max(1.f, s), (float)winW, std::max(1.f, s)}, ui.th.border);
    float x = r.x + 8 * s;
    float h = 36 * s, y = r.y + (r.h - h) / 2;
    if (compact) {
        Rect mb(x, y, h, h);
        if (ui.iconButton("menu", mb, "menu", "", false, true, tr("tip.menu"))) ui.openMenu(mb, allMenus());
        x += h + 6 * s;
    } else {
        // logo mark
        Vec2 c(x + 14 * s, r.center().y);
        drawLogo(g, Rect(c.x - 15 * s, c.y - 15 * s, 30 * s, 30 * s));
        x += 34 * s;
        // Ids stay numeric so a language switch cannot disturb button state.
        const char* nameKeys[] = {"menu.title.file", "menu.title.edit", "menu.title.part",
                                  "menu.title.view", "menu.title.routing.short", "menu.title.help"};
        for (int i = 0; i < 6; i++) {
            const std::string& name = tr(nameKeys[i]);
            float w = g.textWidth(name, 14 * s) + 20 * s;
            Rect b(x, y, w, h);
            if (ui.button("mnu" + std::to_string(i), b, name, BTN_FLAT)) {
                std::vector<MenuItem> m = i == 0 ? fileMenu() : i == 1 ? editMenu() : i == 2 ? partMenu() : i == 3 ? viewMenu() : i == 4 ? routingMenu() : helpMenu();
                ui.openMenu(b, m);
            }
            x += w;
        }
    }
    // view tabs (segmented control)
    const char* fullKeys[] = {"view.breadboard", "view.schematic", "view.pcb"};
    const char* shortKeys[] = {"tab.bread", "tab.schem", "view.pcb"};
    float rightW = compact ? 2 * h + 8 * s : 2 * h + 230 * s;
    float avail = r.r() - rightW - x - 8 * s;
    float tabW = std::min(130 * s, avail / 3);
    bool useShort = tabW < 98 * s;
    float tx = compact ? x + std::max(0.f, (avail - tabW * 3) / 2) : std::max(x + 12 * s, r.center().x - tabW * 1.5f);
    if (!compact && tx + tabW * 3 > r.r() - rightW) tx = x + 12 * s;
    Rect seg(tx - 3 * s, y - 2 * s, tabW * 3 + 6 * s, h + 4 * s);
    g.fillRoundRect(seg, 9 * s, rgba(214, 214, 218));
    for (int v = 0; v < 3; v++) {
        Rect b(tx + tabW * v, y + 1 * s, tabW, h - 2 * s);
        bool selected = view == v;
        if (selected) g.fillRoundRect(b, 7 * s, rgba(255, 255, 255));
        if (ui.button(std::string("tab") + viewKey(v), b, "", BTN_FLAT)) setView(v);
        g.text(b.center(), tr(useShort ? shortKeys[v] : fullKeys[v]), 14 * s, selected ? ui.th.text : ui.th.textDim, 1);
    }
    // right side: title + undo/redo
    float rx = r.r() - 8 * s;
    Rect redoR(rx - h, y, h, h);
    Rect undoR(rx - 2 * h - 4 * s, y, h, h);
    if (ui.iconButton("redo", redoR, "redo", "", false, !redoStack.empty(), tr("tip.redo"))) redo();
    if (ui.iconButton("undo", undoR, "undo", "", false, !undoStack.empty(), tr("tip.undo"))) undo();
    if (!compact) {
        std::string t = doc.title + (dirty() ? " •" : "");
        float maxW = 210 * s;
        t = g.ellipsize(t, 13.5f * s, maxW);
        g.text({undoR.x - 10 * s, r.center().y}, t, 13.5f * s, ui.th.textDim, 2);
    }
}

// ---------------------------------------------------------------------------
// Parts bin
// ---------------------------------------------------------------------------

void App::drawPartsBin(Rect r) {
    float s = ui.s;
    // the registry holds the installed library plus the parts a project embeds;
    // refresh here so an import or a delete shows up immediately
    customEntries = customPartEntries();
    g.fillRect(r, ui.th.panel);
    g.fillRect({r.x, r.y, std::max(1.f, s), r.h}, ui.th.border);
    float pad = 10 * s;
    float y = r.y + pad;
    if (compact) {
        g.text({r.x + pad + 2 * s, y + 11 * s}, tr("tb.parts"), 16 * s, ui.th.text, 0);
        Rect cb(r.r() - 40 * s, y - 4 * s, 32 * s, 32 * s);
        if (ui.iconButton("closeparts", cb, "close", "")) drawerParts = false;
    } else {
        drawRightTabs(r, y);
    }
    y += 30 * s;
    Rect sf(r.x + pad, y, r.w - 2 * pad, 34 * s);
    ui.textField("partsearch", sf, search, tr("bin.search"), true);
    y += 42 * s;

    // category dropdown (the catalog has too many categories for chips).
    // The value stays the English category name (it is what filtering compares
    // against); only the label drawn is translated.
    std::vector<std::string> cats = {"All parts"};
    for (auto& c : partCategories()) cats.push_back(c);
    if (!customEntries.empty()) cats.push_back("Custom");
    std::string cur = category == -1 ? "All parts" : category == -2 ? "Custom" : partCategories()[(size_t)category];
    auto catLabel = [](const std::string& v) -> std::string {
        if (v == "All parts") return tr("bin.all_parts");
        if (v == "Custom") return tr("bin.custom");
        std::string key = "cat." + slugify(v);
        return i18n::exists(key.c_str()) ? tr(key) : v;
    };
    if (ui.comboField("catdd", Rect(r.x + pad, y, r.w - 2 * pad, 34 * s), cur, cats, false, catLabel)) {
        category = -1;
        if (cur == "Custom") category = -2;
        for (size_t i = 0; i < partCategories().size(); i++) if (partCategories()[i] == cur) category = (int)i;
    }
    y += 44 * s;

    // entries
    // built-in entries first, then this design's custom parts (no per-frame copy of the library)
    const std::vector<LibEntry>& lib = partLibrary();
    auto entryAt = [&](size_t i) -> const LibEntry& { return i < lib.size() ? lib[i] : customEntries[i - lib.size()]; };
    size_t total = lib.size() + customEntries.size();
    static std::vector<std::string> hay;  // lower-cased search text per built-in entry
    static std::string hayLang;
    if (hay.size() != lib.size() || hayLang != i18n::lang()) {
        hayLang = i18n::lang();
        hay.clear();
        for (auto& e : lib) {
            // match the English title and its translation, so "LED" and "发光二极管" both work
            std::string t = e.title + "\x1f" + entryTitle(e) + "\x1f" + e.type + "\x1f" + e.category;
            // part numbers are how people search: "NE555" should find the 555 timer
            Props m = mergedProps(e.type, e.props);
            auto part = m.find("part");
            if (part != m.end()) t += "\x1f" + part->second;
            hay.push_back(lower(t));
        }
    }
    std::vector<int> shown;
    std::string q = lower(search);
    for (size_t i = 0; i < total; i++) {
        const LibEntry& e = entryAt(i);
        bool isCustom = i >= lib.size();
        if (category == -2 && !isCustom) continue;
        if (category >= 0 && e.category != partCategories()[(size_t)category]) continue;
        if (!q.empty() && (isCustom ? lower(e.title + "\x1f" + entryTitle(e) + "\x1f" + e.type + "\x1f" + e.category) : hay[i]).find(q) == std::string::npos) continue;
        shown.push_back((int)i);
    }
    Rect area(r.x, y, r.w, r.b() - y);
    float cellW = 88 * s, cellH = 100 * s;
    int cols = std::max(2, (int)((area.w - pad) / cellW));
    cellW = (area.w - pad * 2) / (float)cols;
    int rows = ((int)shown.size() + cols - 1) / cols;
    float contentH = rows * cellH + pad;
    float oy = ui.beginScroll("bin", area, contentH);
    for (size_t k = 0; k < shown.size(); k++) {
        int idx = shown[k];
        const LibEntry& e = entryAt((size_t)idx);
        Rect cell(area.x + pad + (float)(k % cols) * cellW, area.y + oy + (float)(k / cols) * cellH, cellW - 4 * s, cellH - 4 * s);
        if (cell.b() < area.y || cell.y > area.b()) continue;
        std::string id = "bin" + std::to_string(idx);
        bool clicked = ui.button(id, cell, "", BTN_FLAT);
        PartDefPtr def = getPartDef(e.type, e.props);
        Rect ir(cell.x + 10 * s, cell.y + 6 * s, cell.w - 20 * s, cell.h - 44 * s);
        if (def) drawPartIcon(def, ir);
        auto lines = wrapText(g, entryTitle(e), 11.5f * s, cell.w - 6 * s);
        for (size_t li = 0; li < lines.size() && li < 2; li++)
            g.text({cell.center().x, cell.b() - 30 * s + (float)li * 14 * s}, lines[li], 11.5f * s, ui.th.text, 1);
        ui.tooltip(cell, def ? partTitle(*def) + " — " + partDesc(*def) : entryTitle(e));
        // right-click a part: inspect, export or (for an installed model) delete it
        if (in.rightPressed && cell.contains(in.mouse) && !ui.blocking()) {
            bool custom = idx >= (int)lib.size();
            std::string type = e.type;
            auto item = [](const std::string& label, std::function<void()> action) {
                MenuItem mi;
                mi.label = label;
                mi.action = std::move(action);
                return mi;
            };
            std::vector<MenuItem> menu;
            menu.push_back(item("Model Info…", [this, type]() { showPartModelInfo(type); }));
            menu.push_back(item("Export Model…", [this, type]() { exportPartModelDialog(type); }));
            PartModel installed;
            if (custom && userPartModel(type, installed)) {
                menu.push_back(MenuItem::sep());
                menu.push_back(item("Delete from Library", [this, type]() { deletePartModelConfirm(type); }));
            }
            ui.openMenu(Rect(in.mouse.x, in.mouse.y, 0, 0), menu);
        }
        // start dragging onto the canvas (mouse); touch uses tap-to-add
        if (!in.touch && in.down && binDrag < 0 && cell.contains(in.pressPos) && area.contains(in.pressPos) && dist(in.mouse, in.pressPos) > 6 * s && !ui.blocking())
            binDrag = idx;
        if (clicked && binDrag < 0) {
            Vec2 at = toWorld(rCanvas.center());
            addPart(e, at);
            if (compact) drawerParts = false;
            ui.toast(trf("bin.added", {def ? partTitle(*def) : entryTitle(e)}));
        }
    }
    ui.endScroll();
    if (shown.empty()) g.text({area.center().x, area.y + 30 * s}, trf("bin.no_match", {search}), 13 * s, ui.th.textDim, 1);

    if (binDrag >= 0 && in.released) {
        if (rCanvas.contains(in.mouse) && !(compact && rRight.contains(in.mouse)) && binDrag < (int)total) addPart(entryAt((size_t)binDrag), toWorld(in.mouse));
        binDrag = -1;
    }
}

// ---------------------------------------------------------------------------
// Inspector
// ---------------------------------------------------------------------------

static bool editableProp(const PropDef& pd) {
    static const char* keys[] = {"resistance", "capacitance", "inductance", "voltage", "frequency", "net", "outputs", "width", "height", "part", "wiper", "light", "chip label", "pin names", "pins", "diameter"};
    if (pd.options.empty()) return true;
    for (auto* k : keys) if (pd.key == k) return true;
    return false;
}

void App::drawInspector(Rect r) {
    float s = ui.s;
    g.fillRect(r, ui.th.panel);
    g.fillRect({r.x, r.y, std::max(1.f, s), r.h}, ui.th.border);
    g.fillRect({r.x, r.y, r.w, std::max(1.f, s)}, ui.th.border);
    float pad = 12 * s;
    g.text({r.x + pad, r.y + 22 * s}, tr("insp.title"), 16 * s, ui.th.text, 0);
    if (compact) {
        Rect cb(r.r() - 40 * s, r.y + 6 * s, 32 * s, 32 * s);
        if (ui.iconButton("closeinsp", cb, "close", "")) drawerInspector = false;
    }
    Rect area(r.x, r.y + 42 * s, r.w, r.h - 42 * s);
    static float lastH = 400;
    float oy = ui.beginScroll("inspector", area, lastH);
    float x = area.x + pad, w = area.w - 2 * pad;
    float y = area.y + oy + 4 * s;
    float rowH = 34 * s;
    auto heading = [&](const std::string& t) {
        y += 8 * s;
        g.text({x, y + 9 * s}, t, 12 * s, ui.th.textDim, 0);
        y += 22 * s;
    };
    auto label = [&](const std::string& t) {
        g.text({x, y + rowH / 2}, t, 13 * s, ui.th.textDim, 0);
    };
    float lw = std::min(110 * s, w * 0.38f);

    size_t nsel = selParts.size() + selWires.size() + selNotes.size();
    if (nsel == 1 && selParts.size() == 1) {
        PartInst* p = doc.part(*selParts.begin());
        if (p) {
            int pid = p->id;
            Rect icon(x, y, 72 * s, 72 * s);
            g.fillRoundRect(icon, 8 * s, rgba(255, 255, 255));
            g.strokeRoundRect(icon, 8 * s, std::max(1.f, s), ui.th.border);
            drawPartIcon(p->def, icon.inset(6 * s));
            g.text({x + 84 * s, y + 18 * s}, partTitle(*p->def), 16 * s, ui.th.text, 0);
            g.text({x + 84 * s, y + 42 * s}, p->label + "  ·  " + partCategory(*p->def), 12.5f * s, ui.th.textDim, 0);
            y += 82 * s;
            float dh = ui.wrappedText(Rect(x, y, w, 0), partDesc(*p->def), ui.th.textDim, 12.5f * s);
            y += dh + 6 * s;

            heading(tr("insp.heading.properties"));
            label(tr("insp.label.label"));
            std::string lbl = p->label;
            if (ui.textField("lbl" + std::to_string(pid), Rect(x + lw, y, w - lw, rowH - 4 * s), lbl) && lbl != p->label && !lbl.empty()) {
                checkpoint();
                if (PartInst* pp = doc.part(pid)) pp->label = lbl;
                changed();
            }
            y += rowH + 2 * s;
            Props merged = mergedProps(p->type, p->props);
            PartDefPtr def = p->def;  // keep alive while editing
            for (auto& pd : def->propDefs) {
                label(propLabel(pd));
                std::string val = merged[pd.key];
                if (ui.comboField("prop" + std::to_string(pid) + pd.key, Rect(x + lw, y, w - lw, rowH - 4 * s), val, pd.options, editableProp(pd),
                                  [&pd](const std::string& v) { return optionLabel(pd, v); }))
                    setPartProp(pid, pd.key, val);
                y += rowH + 2 * s;
            }
            p = doc.part(pid);
            if (!p) { ui.endScroll(); return; }

            if (p->def->sim == "board" || p->def->sim == "mcu") {
                heading(tr("insp.heading.program"));
                std::string info;
                if (p->code.empty()) info = tr("insp.msg.program_info");
                else {
                    int lines = 1;
                    for (char ch : p->code) lines += ch == '\n';
                    info = trf("insp.msg.program_lines", {std::to_string(lines)});
                }
                y += ui.wrappedText(Rect(x, y, w, 0), info, ui.th.textDim, 12.5f * s) + 6 * s;
                float bw = p->code.empty() ? w : (w - 8 * s) * 0.62f;
                if (ui.button("prog-edit", Rect(x, y, bw, rowH), tr(p->code.empty() ? "insp.btn.write_program" : "insp.btn.edit_program"), BTN_PRIMARY)) openCode(pid);
                if (!p->code.empty() && ui.button("prog-up", Rect(x + bw + 8 * s, y, w - bw - 8 * s, rowH), tr("insp.btn.upload"), BTN_NORMAL)) openUploadSheet(pid);
                y += rowH + 8 * s;
                p = doc.part(pid);
                if (!p) { ui.endScroll(); return; }
            }

            heading(trf("insp.heading.placement", {viewLabel(view)}));
            if (p->inView(view)) {
                Vec2 pos = p->place[view].pos;
                std::string posLine = trf("insp.msg.placement", {num(pos.x * 0.0254, 1), num(pos.y * 0.0254, 1),
                                                                 std::to_string(p->place[view].rot * 90),
                                                                 p->place[view].flip ? tr("insp.msg.flipped") : std::string()});
                g.text({x, y + 10 * s}, posLine, 12.5f * s, ui.th.text, 0, FONT_MONO);
                y += 26 * s;
                float bw = (w - 12 * s) / 3;
                if (ui.button("rotl", Rect(x, y, bw, rowH), tr("insp.btn.rotate_left"), BTN_NORMAL, false, !p->def->isBoard)) rotateSelection(-1);
                if (ui.button("rotr", Rect(x + bw + 6 * s, y, bw, rowH), tr("insp.btn.rotate_right"), BTN_NORMAL, false, !p->def->isBoard)) rotateSelection(1);
                if (ui.button("flip", Rect(x + 2 * (bw + 6 * s), y, bw, rowH), tr("insp.btn.flip"), BTN_NORMAL, false, !p->def->isBoard)) flipSelection();
                y += rowH + 6 * s;
            } else {
                y += ui.wrappedText(Rect(x, y, w, 0), trf("insp.msg.not_in_view", {viewLabel(view)}), ui.th.textDim, 12.5f * s) + 6 * s;
            }

            heading(tr("insp.heading.connections"));
            ensureConn();
            int connected = 0;
            for (size_t i = 0; i < p->def->pins.size(); i++) if (conn.pinConnectedInView(view, p->id, (int)i)) connected++;
            g.text({x, y + 10 * s}, trf("insp.msg.pins", {std::to_string(p->def->pins.size()), std::to_string(connected)}), 12.5f * s, ui.th.text, 0);
            y += 26 * s;
            if (!p->def->isBreadboard && p->def->pins.size() <= 40) {
                for (size_t i = 0; i < p->def->pins.size(); i++) {
                    int net = conn.netOf(p->id, (int)i);
                    std::string line = p->def->pins[i].name + "  →  " + conn.netName(net);
                    if (simOn && net >= 0 && net < (int)sim.netVoltage.size())
                        line += "  (" + num(sim.netVoltage[(size_t)net], 2) + " V)";
                    g.text({x + 6 * s, y + 9 * s}, line, 12 * s, conn.pinConnectedInView(view, p->id, (int)i) ? ui.th.text : ui.th.textDim, 0);
                    y += 19 * s;
                }
            }
            y += 8 * s;
            if (ui.button("delpart", Rect(x, y, w, rowH), tr("insp.btn.delete_part"), BTN_NORMAL)) deleteSelection();
            y += rowH + 12 * s;
        }
    } else if (nsel == 1 && selWires.size() == 1) {
        Wire* wr = doc.wire(*selWires.begin());
        if (wr) {
            int wid = wr->id;
            g.text({x, y + 14 * s}, tr(view == PCB ? "insp.item.trace" : "insp.item.wire"), 16 * s, ui.th.text, 0);
            y += 34 * s;
            float length = 0;
            for (size_t i = 0; i + 1 < wr->pts.size(); i++) length += dist(wr->pts[i], wr->pts[i + 1]);
            size_t bends = wr->pts.size() >= 2 ? wr->pts.size() - 2 : 0;
            ensureConn();
            y += ui.wrappedText(Rect(x, y, w, 0),
                                trf("insp.msg.length", {num(length * 0.0254, 1), trn("insp.msg.bends", (long long)bends), conn.netName(conn.wireNet(wid))}),
                                ui.th.textDim, 12.5f * s) + 6 * s;
            for (int e = 0; e < 2; e++) {
                const WireEnd& end = wr->ends[e];
                std::string t = tr(e == 0 ? "insp.msg.from" : "insp.msg.to");
                if (end.attached()) {
                    const PartInst* p = doc.part(end.part);
                    t += p ? p->label + " · " + p->def->pins[(size_t)end.pin].name : "?";
                } else t += tr("insp.msg.not_attached");
                g.text({x, y + 9 * s}, t, 12.5f * s, end.attached() ? ui.th.text : ui.th.danger, 0);
                y += 20 * s;
            }
            if (wr->view == BB) {
                heading(tr("insp.heading.color"));
                const uint32_t cs[] = {rgba(65, 140, 225), rgba(220, 50, 50), rgba(40, 40, 40), rgba(240, 200, 40), rgba(40, 170, 80),
                                       rgba(245, 140, 30), rgba(240, 240, 240), rgba(150, 150, 150), rgba(140, 70, 190)};
                float sw = 30 * s;
                float cx = x;
                for (int i = 0; i < 9; i++) {
                    if (cx + sw > x + w) { cx = x; y += sw + 6 * s; }
                    Rect b(cx, y, sw, sw);
                    bool cur = wr->color == cs[i];
                    if (ui.button("wcol" + std::to_string(i), b, "", BTN_FLAT)) {
                        checkpoint();
                        if (Wire* ww = doc.wire(wid)) ww->color = cs[i];
                        changed();
                    }
                    g.fillCircle(b.center(), sw * 0.38f, cs[i]);
                    g.strokeCircle(b.center(), sw * 0.38f, std::max(1.f, s), rgba(0, 0, 0, 60));
                    if (cur) g.strokeCircle(b.center(), sw * 0.48f, 2 * s, ui.th.accent);
                    cx += sw + 6 * s;
                }
                y += sw + 8 * s;
            } else if (wr->view == PCB) {
                heading(tr("insp.heading.layer"));
                float bw = (w - 6 * s) / 2;
                if (ui.button("ltop", Rect(x, y, bw, rowH), tr("insp.layer.top"), BTN_NORMAL, wr->layer == TOP)) { checkpoint(); doc.wire(wid)->layer = TOP; changed(); }
                if (ui.button("lbot", Rect(x + bw + 6 * s, y, bw, rowH), tr("insp.layer.bottom"), BTN_NORMAL, wr->layer == BOTTOM)) { checkpoint(); doc.wire(wid)->layer = BOTTOM; changed(); }
                y += rowH + 6 * s;
                wr = doc.wire(wid);
                heading(tr("insp.heading.width"));
                const float ws[] = {8, 12, 16, 24, 32, 48};
                float cx = x;
                for (float tw : ws) {
                    std::string lbl = std::to_string((int)tw) + " mil";
                    float bwid = g.textWidth(lbl, 12.5f * s) + 18 * s;
                    if (cx + bwid > x + w) { cx = x; y += 32 * s; }
                    if (ui.button("tw" + lbl, Rect(cx, y, bwid, 26 * s), lbl, BTN_CHIP, std::fabs(wr->width - tw) < 0.1f)) {
                        checkpoint();
                        doc.wire(wid)->width = tw;
                        changed();
                    }
                    cx += bwid + 6 * s;
                }
                y += 36 * s;
            }
            if (ui.button("straight", Rect(x, y, w, rowH), tr("insp.btn.straighten"), BTN_NORMAL)) {
                checkpoint();
                if (Wire* ww = doc.wire(wid)) ww->pts = {ww->pts.front(), ww->pts.back()};
                changed();
            }
            y += rowH + 6 * s;
            if (ui.button("delwire", Rect(x, y, w, rowH), tr(view == PCB ? "insp.btn.delete_trace" : "insp.btn.delete_wire"), BTN_NORMAL)) deleteSelection();
            y += rowH + 12 * s;
        }
    } else if (nsel == 1 && selNotes.size() == 1) {
        Note* n = doc.note(*selNotes.begin());
        if (n) {
            int nid = n->id;
            g.text({x, y + 14 * s}, tr("insp.item.note"), 16 * s, ui.th.text, 0);
            y += 34 * s;
            std::string t = n->text;
            if (ui.textField("note" + std::to_string(nid), Rect(x, y, w, rowH), t, tr("insp.msg.note_text"))) {
                checkpoint();
                if (Note* nn = doc.note(nid)) nn->text = t;
                changed();
            }
            y += rowH + 6 * s;
            // text size: world units, so it grows with the drawing like the note box does
            auto bump = [&](float d) {
                checkpoint();
                if (Note* nn = doc.note(nid)) nn->textSize = std::clamp(nn->textSize + d, 32.f, 240.f);
                changed();
            };
            float bw = 46 * s;
            if (ui.button("notefs-", Rect(x, y, bw, rowH), "A-", BTN_NORMAL, false, n->textSize > 33)) bump(-8);
            if (ui.button("notefs+", Rect(x + w - bw, y, bw, rowH), "A+", BTN_NORMAL, false, n->textSize < 239)) bump(8);
            g.text({x + w / 2, y + rowH / 2}, std::to_string((int)std::lround(n->textSize)), 13 * s, ui.th.textDim, 1);
            y += rowH + 6 * s;
            y += ui.wrappedText(Rect(x, y, w, 0), tr("insp.msg.note_tip"), ui.th.textDim, 12 * s) + 6 * s;
            if (ui.button("delnote", Rect(x, y, w, rowH), tr("insp.btn.delete_note"), BTN_NORMAL)) deleteSelection();
            y += rowH + 12 * s;
            if (Note* nn = doc.note(nid)) {
                size_t pos;
                while ((pos = nn->text.find("\\n")) != std::string::npos) nn->text.replace(pos, 2, "\n");
            }
        }
    } else if (nsel > 1) {
        g.text({x, y + 14 * s}, trn("insp.msg.items_selected", (long long)nsel), 16 * s, ui.th.text, 0);
        y += 40 * s;
        float bw = (w - 12 * s) / 3;
        if (ui.button("mrotl", Rect(x, y, bw, rowH), tr("insp.btn.rotate_left"))) rotateSelection(-1);
        if (ui.button("mrotr", Rect(x + bw + 6 * s, y, bw, rowH), tr("insp.btn.rotate_right"))) rotateSelection(1);
        if (ui.button("mflip", Rect(x + 2 * (bw + 6 * s), y, bw, rowH), tr("insp.btn.flip"))) flipSelection();
        y += rowH + 6 * s;
        if (ui.button("mdup", Rect(x, y, w, rowH), tr("insp.btn.duplicate"))) duplicate();
        y += rowH + 6 * s;
        if (ui.button("mdel", Rect(x, y, w, rowH), tr("insp.btn.delete_selection"))) deleteSelection();
        y += rowH + 12 * s;
    } else {
        heading(tr("insp.heading.project"));
        label(tr("insp.label.title"));
        std::string t = doc.title;
        if (ui.textField("ptitle", Rect(x + lw, y, w - lw, rowH - 4 * s), t) && t != doc.title) { checkpoint(); doc.title = t; changed(); }
        y += rowH + 2 * s;
        label(tr("insp.label.author"));
        std::string a = doc.author;
        if (ui.textField("pauthor", Rect(x + lw, y, w - lw, rowH - 4 * s), a, tr("insp.msg.author_placeholder")) && a != doc.author) { checkpoint(); doc.author = a; changed(); }
        y += rowH + 2 * s;
        label(tr("insp.label.notes"));
        std::string nt = doc.notesText;
        if (ui.textField("pnotes", Rect(x + lw, y, w - lw, rowH - 4 * s), nt, tr("insp.msg.notes_placeholder")) && nt != doc.notesText) { checkpoint(); doc.notesText = nt; changed(); }
        y += rowH + 2 * s;
        if (!doc.notesText.empty()) y += ui.wrappedText(Rect(x, y, w, 0), doc.notesText, ui.th.textDim, 12.5f * s) + 4 * s;

        ensureConn();
        heading(tr("insp.heading.status"));
        RoutingStatus st;
        conn.ratsnest(doc, view, &st);
        int nparts = 0, nwires = 0;
        for (auto& p : doc.parts) if (p.inView(view)) nparts++;
        for (auto& wr : doc.wires) if (wr.view == view) nwires++;
        g.text({x, y + 9 * s}, trf(view == PCB ? "insp.msg.parts_traces" : "insp.msg.parts_wires", {std::to_string(nparts), std::to_string(nwires)}),
               12.5f * s, ui.th.text, 0);
        y += 20 * s;
        std::string routeLine;
        if (st.connectionsToRoute == 0) routeLine = st.nets ? trf("insp.msg.routing_done", {std::to_string(st.nets)}) : tr("insp.msg.no_connections");
        else routeLine = trf("insp.msg.routing_todo", {std::to_string(st.routedNets), std::to_string(st.nets), std::to_string(st.connectionsToRoute)});
        y += ui.wrappedText(Rect(x, y, w, 0), routeLine, st.connectionsToRoute ? ui.th.danger : ui.th.ok, 12.5f * s) + 2 * s;
        std::string fileLine = filePath.empty() ? tr("insp.msg.not_saved") : filePath;
        y += ui.wrappedText(Rect(x, y, w, 0), fileLine + (dirty() ? tr("insp.msg.unsaved") : std::string()), ui.th.textDim, 12 * s) + 4 * s;

        if (view == PCB) {
            heading(tr("insp.heading.rules"));
            label(tr("insp.label.trace_width"));
            std::string tw = std::to_string((int)doc.rules.traceWidth);
            if (ui.comboField("rtw", Rect(x + lw, y, w - lw, rowH - 4 * s), tw, {"8", "12", "16", "24", "32", "48"}, true)) {
                checkpoint();
                doc.rules.traceWidth = std::clamp((float)std::atof(tw.c_str()), 4.f, 200.f);
                changed();
            }
            y += rowH + 2 * s;
            label(tr("insp.label.clearance"));
            std::string cl = std::to_string((int)doc.rules.clearance);
            if (ui.comboField("rcl", Rect(x + lw, y, w - lw, rowH - 4 * s), cl, {"6", "8", "10", "12", "16", "20"}, true)) {
                checkpoint();
                doc.rules.clearance = std::clamp((float)std::atof(cl.c_str()), 2.f, 100.f);
                changed();
            }
            y += rowH + 2 * s;
            y += ui.wrappedText(Rect(x, y, w, 0), tr("insp.msg.values_mils"), ui.th.textDim, 12 * s) + 4 * s;
        }
        heading(tr("insp.heading.tips"));
        const std::string& tips = tr(view == BB ? "tips.breadboard" : view == SCH ? "tips.schematic" : "tips.pcb");
        y += ui.wrappedText(Rect(x, y, w, 0), tips, ui.th.textDim, 12.5f * s) + 8 * s;
    }
    lastH = y - (area.y + oy) + 20 * s;
    ui.endScroll();
}

// ---------------------------------------------------------------------------
// Bottom toolbar
// ---------------------------------------------------------------------------

void App::drawToolbar() {
    float s = ui.s;
    Rect r = rBottom;
    g.fillRect(Rect(0, r.y, (float)winW, (float)winH - r.y), ui.th.panel2);
    g.fillRect({0, r.y, (float)winW, std::max(1.f, s)}, ui.th.border);
    struct Tool { std::string id, icon, label; std::function<void()> fn; bool sel; bool enabled; std::string tip; };
    std::vector<Tool> tools;
    bool anySel = !selParts.empty() || !selWires.empty() || !selNotes.empty();
    if (compact) {
        tools.push_back({"t-parts", "parts", tr("tb.parts"), [this]() { drawerParts = !drawerParts; drawerInspector = drawerLayers = false; }, drawerParts, true, tr("tip.parts_bin")});
        tools.push_back({"t-layers", "layer", tr("tb.layers"), [this]() { drawerLayers = !drawerLayers; drawerInspector = drawerParts = drawerAI = false; }, drawerLayers, true, tr("tip.layers")});
        if (agentsSupported())
            tools.push_back({"t-ai", "bolt", tr("tb.ai"), [this]() { drawerAI = !drawerAI; drawerInspector = drawerParts = drawerLayers = false; }, drawerAI, true, tr("tip.ai")});
        tools.push_back({"t-insp", "inspector", tr("tb.inspect"), [this]() { drawerInspector = !drawerInspector; drawerParts = drawerLayers = false; }, drawerInspector, true, tr("tip.inspector")});
    }
    if (!compact || view != PCB) tools.push_back({"t-note", "note", tr("tb.note"), [this]() { addNote(); }, false, true, tr("tip.add_note")});
    tools.push_back({"t-rot", "rotate", tr("tb.rotate"), [this]() { rotateSelection(1); }, false, !selParts.empty(), tr("tip.rotate")});
    if (!compact) tools.push_back({"t-flip", "flip", tr("tb.flip"), [this]() { flipSelection(); }, false, !selParts.empty(), tr("tip.flip")});
    tools.push_back({"t-del", "delete", tr("tb.delete"), [this]() { deleteSelection(); }, false, anySel, tr("tip.delete")});
    if (view == PCB) {
        tools.push_back({"t-layer", "layer", tr(activeLayer == TOP ? "tb.top" : "tb.bottom"), [this]() { activeLayer = 1 - activeLayer; }, false, true,
                         tr("tip.active_layer")});
        tools.push_back({"t-route", "route", tr("tb.autoroute"), [this]() { runAutoroute(); }, false, true, tr("tip.autoroute")});
        tools.push_back({"t-drc", "drc", tr("tb.drc"), [this]() { runDrc(); }, false, true, tr("tip.drc")});
    } else {
        tools.push_back({"t-sim", simOn ? "stop" : "play", tr(simOn ? "tb.stop" : "tb.simulate"), [this]() { toggleSim(); }, simOn, true, tr("tip.simulate")});
    }
    tools.push_back({"t-share", "share", tr(compact ? "tb.share" : "tb.export"), [this]() {}, false, true, tr("tip.share")});

    float bw = compact ? std::min(66 * s, r.w / (float)tools.size()) : 70 * s;
    float bh = r.h - 8 * s;
    float x = r.x + (compact ? (r.w - bw * (float)tools.size()) / 2 : 10 * s);
    for (auto& t : tools) {
        Rect b(x, r.y + 4 * s, bw, bh);
        if (ui.iconButton(t.id, b, t.icon, t.label, t.sel, t.enabled, t.tip)) {
            if (t.id == "t-share") {
                std::vector<MenuItem> m = {
                    item(tr("tb.share.save_project"), [this]() { saveAs(); }),
                    MenuItem::sep(),
                    item(tr("export.gerber"), [this]() { exportFile("gerber"); }),
                    item(tr("export.svg"), [this]() { exportFile("svg"); }),
                    item(tr("export.png"), [this]() { exportFile("png"); }),
                    item(tr("export.bom"), [this]() { exportFile("bom"); }),
                    item(tr("export.netlist"), [this]() { exportFile("netlist"); }),
                    item(tr("export.spice"), [this]() { exportFile("spice"); }),
                };
                ui.openMenu(b, m, true);
            } else if (t.fn) t.fn();
        }
        x += bw;
    }
    if (!compact) {
        // zoom controls on the right
        float zx = r.r() - 10 * s;
        float h = 34 * s, y = r.center().y - h / 2;
        Rect fit(zx - h, y, h, h);
        if (ui.iconButton("z-fit", fit, "fit", "", false, true, tr("tip.fit"))) fitView(view, rCanvas);
        Rect zin(fit.x - h - 2 * s, y, h, h);
        if (ui.iconButton("z-in", zin, "zoomin", "", false, true, tr("tip.zoom_in"))) zoomAt(1.25f, rCanvas.center());
        char buf[32];
        snprintf(buf, sizeof buf, "%d%%", (int)std::lround(cam[view].zoom / 0.1f * 100));
        float tw = 56 * s;
        g.text({zin.x - tw / 2 - 2 * s, r.center().y}, buf, 13 * s, ui.th.textDim, 1, FONT_MONO);
        Rect zout(zin.x - tw - h - 4 * s, y, h, h);
        if (ui.iconButton("z-out", zout, "zoomout", "", false, true, tr("tip.zoom_out"))) zoomAt(0.8f, rCanvas.center());
        // routing status in the middle
        ensureConn();
        RoutingStatus st;
        conn.ratsnest(doc, view, &st);
        std::string msg;
        uint32_t col = ui.th.textDim;
        if (st.nets == 0) msg = "";
        else if (st.connectionsToRoute == 0) { msg = tr("insp.msg.routing_done"); col = ui.th.ok; }
        else {
            // Same sentence as the inspector status line: one key, one translation.
            msg = trf("insp.msg.routing_todo", {std::to_string(st.routedNets), std::to_string(st.nets), std::to_string(st.connectionsToRoute)});
            col = ui.th.danger;
        }
        float left = x + 10 * s, right = zout.x - 10 * s;
        if (!msg.empty() && right - left > 120 * s) {
            while (msg.size() > 8 && g.textWidth(msg, 13 * s) > right - left) msg = msg.substr(0, msg.size() - 2);
            g.text({(left + right) / 2, r.center().y}, msg, 13 * s, col, 1);
        }
    }
}

// ---------------------------------------------------------------------------
// Canvas overlays (status pills, hints)
// ---------------------------------------------------------------------------

void App::drawCanvasOverlay() {
    float s = ui.s;
    Rect c = rCanvas;
    bool empty = true;
    for (auto& p : doc.parts) if (p.inView(view) && !p.def->isBoard && !p.def->isBreadboard) { empty = false; break; }
    if (empty && !readOnly) {
        std::string hint = tr(compact ? "overlay.hint_tap" : "overlay.hint_drag");
        float w = g.textWidth(hint, 15 * s) + 36 * s;
        Rect r(c.center().x - w / 2, c.y + 18 * s, w, 38 * s);
        g.fillRoundRect(r, 19 * s, rgba(255, 255, 255, 230));
        g.strokeRoundRect(r, 19 * s, std::max(1.f, s), ui.th.border);
        g.text(r.center(), hint, 15 * s, ui.th.textDim, 1);
    }
    float y = c.y + 12 * s;
    simNotesRect = Rect();
    if (simOn) {
        std::string msg = sim.message.empty() ? tr("overlay.simulating") : sim.message;
        float w = g.textWidth(msg, 13.5f * s) + 44 * s;
        Rect r(c.x + 12 * s, y, w, 30 * s);
        g.fillRoundRect(r, 15 * s, rgba(28, 116, 222, 235));
        g.fillCircle({r.x + 16 * s, r.center().y}, 5 * s, (SDL_GetTicks() / 500) % 2 ? rgba(255, 255, 255) : rgba(160, 200, 255));
        g.text({r.x + 28 * s, r.center().y}, msg, 13.5f * s, rgba(255, 255, 255), 0);
        y += 36 * s;
        // real problems in red; informational notes are folded into one grey line
        std::vector<std::string>& problems = sim.warnings;
        std::vector<std::string>& notes = sim.notes;
        auto box = [&](const std::string& wmsg, uint32_t bg, uint32_t fg) {
            auto lines = wrapText(g, wmsg, 13 * s, std::min(520 * s, c.w - 60 * s));
            float h = 12 * s + (float)lines.size() * 17 * s;
            float ww = 0;
            for (auto& l : lines) ww = std::max(ww, g.textWidth(l, 13 * s));
            Rect r2(c.x + 12 * s, y, ww + 24 * s, h);
            g.fillRoundRect(r2, 8 * s, bg);
            for (size_t i = 0; i < lines.size(); i++) g.text({r2.x + 12 * s, r2.y + 6 * s + 17 * s * ((float)i + 0.5f)}, lines[i], 13 * s, fg, 0);
            y += h + 6 * s;
        };
        for (auto& wmsg : problems) {
            box(wmsg, rgba(200, 50, 50, 235), rgba(255, 255, 255));
            if (y > c.b() - 80 * s) break;
        }
        if (!notes.empty() && y < c.b() - 80 * s) {
            std::string head = trf("overlay.notes", {trn("common.note_count", (long long)notes.size()), simNotesOpen ? "\u25B4" : "\u25BE"});
            float w = g.textWidth(head, 12.5f * s) + 24 * s;
            Rect r(c.x + 12 * s, y, w, 26 * s);
            simNotesRect = r;
            g.fillRoundRect(r, 13 * s, ui.hovered(r) ? rgba(90, 96, 108, 225) : rgba(110, 116, 128, 200));
            g.text({r.x + 12 * s, r.center().y}, head, 12.5f * s, rgba(255, 255, 255), 0);
            if (ui.hovered(r) && ui.in->pressed) {
                simNotesOpen = !simNotesOpen;
                ui.in->pressed = false;
            }
            y += 32 * s;
            if (simNotesOpen)
                for (auto& n : notes) {
                    box(n, rgba(245, 246, 248, 240), rgba(70, 76, 88));
                    if (y > c.b() - 80 * s) break;
                }
        }
    }
    if (view == PCB && !readOnly) {
        std::string lt = trf("overlay.active_layer", {tr(activeLayer == TOP ? "insp.layer.top" : "insp.layer.bottom")});
        float w = g.textWidth(lt, 12.5f * s) + 24 * s;
        Rect r(c.r() - w - 12 * s, c.y + 12 * s, w, 26 * s);
        g.fillRoundRect(r, 13 * s, rgba(0, 0, 0, 120));
        g.text(r.center(), lt, 12.5f * s, activeLayer == TOP ? rgba(255, 200, 120) : rgba(240, 220, 120), 1);
    }
    if (compact) {
        ensureConn();
        RoutingStatus st;
        conn.ratsnest(doc, view, &st);
        if (st.connectionsToRoute > 0 && !readOnly) {
            std::string m = trf("overlay.to_route", {std::to_string(st.connectionsToRoute)});
            float w = g.textWidth(m, 12.5f * s) + 24 * s;
            Rect r(c.x + 10 * s, c.b() - 38 * s, w, 28 * s);
            g.fillRoundRect(r, 14 * s, rgba(200, 50, 50, 220));
            g.text(r.center(), m, 12.5f * s, rgba(255, 255, 255), 1);
        }
        Rect fit(c.r() - 50 * s, c.b() - 50 * s, 40 * s, 40 * s);
        if (!(drawerParts || drawerInspector || drawerLayers || drawerAI) && !embedMode) {
            g.fillRoundRect(fit, 20 * s, rgba(255, 255, 255, 235));
            g.strokeRoundRect(fit, 20 * s, std::max(1.f, s), ui.th.border);
            if (ui.iconButton("cfit", fit.inset(4 * s), "fit", "")) fitView(view, rCanvas);
        }
    }
}
