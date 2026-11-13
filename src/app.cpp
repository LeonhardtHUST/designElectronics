#include "app.h"
#include "colorcode.h"
#include "i18n.h"
#include "logo.h"
#include "partmodels.h"
#include "settings.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Startup / shutdown
// ---------------------------------------------------------------------------

static void newProjectContents(Document& d) {
    d.clear();
    // A new project is one HUST EIC lesson breadboard: nothing soldered on it yet.
    d.addPart("breadboard", {{"size", "HUST EIC Lesson"}}, {0, 0});
    d.addPart("pcb_board", {}, {0, 0});
}

bool App::init(int argc, char** argv) {
    SDL_SetAppMetadata("designElectronics", kAppVersion, "org.designelectronics.app");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://github.com/rORrEtboLt/designElectronics");
    SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_TYPE_STRING, "application");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    // Preferences first: they hold the interface language, and a damaged or read-only
    // config.toml has to be reported. Migrating an old settings.json happens here too.
    settingsInit();
    // Translations before anything draws: the stored choice wins, otherwise the OS
    // locale picks the language. --lang overrides both for screenshots and tests.
    i18n::init(!startupLanguage.empty() ? startupLanguage : settingsGet("language"));
    SDL_WindowFlags wflags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __EMSCRIPTEN__
    wflags |= SDL_WINDOW_FILL_DOCUMENT;  // the canvas follows the page / iframe size
#endif
    win = SDL_CreateWindow("designElectronics", 1360, 860, wflags);
    if (!win) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return false; }
#ifndef __EMSCRIPTEN__
    SDL_SetWindowMinimumSize(win, 360, 480);
#endif
    loadGitHubSettings();
    loadWirePrefs();  // colours and widths the user picked for new wires
    loadBindings();   // keyboard and mouse mapping
    webRegisterApp(this);
    if (SDL_Surface* icon = makeLogoSurface(256)) {
        SDL_SetWindowIcon(win, icon);
        SDL_DestroySurface(icon);
    }
    ren = SDL_CreateRenderer(win, nullptr);
    if (!ren) { SDL_Log("CreateRenderer failed: %s", SDL_GetError()); return false; }
    SDL_SetRenderVSync(ren, 1);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    g.init(ren);
    imageCache.setRenderer(ren);  // picture annotations rasterize/cache here
    ui.g = &g;
    ui.in = &in;
    ui.window = win;
    // A settings problem is worth one toast, never a dialog on startup.
    if (!settingsWarning().empty()) ui.toast(tr(settingsWarning()), 6);

    // Installed part models live outside the project, so they are available in
    // every design and to the MCP tools.
    std::vector<std::string> modelProblems;
    loadUserPartModels(&modelProblems);
    for (auto& p : modelProblems) SDL_Log("part library: %s", p.c_str());

    std::string openPath;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("-", 0) == 0) continue;
        openPath = a;
    }
    bool loaded = false;
    if (!openPath.empty()) loaded = loadFile(openPath);
    if (!loaded && !readOnly && !scratchSession) {
        // Nothing is created yet: the first screen asks whether to open or start
        // a project, and keeps the autosave for an explicit "restore".
        bool ok = false;
        std::string autosave = readTextFile(autosavePath(), &ok);
        if (ok && !autosave.empty()) {
            Json j = Json::parse(autosave);
            // A clean autosave (already saved) is not worth offering again.
            autosaveAvailable = j["autosaveDirty"].boolean() || j["autosaveOf"].str().empty();
        }
        startScreen = true;
        doc.clear();
    }
    if (!loaded && (readOnly || scratchSession)) {
        // screenshots and the viewer still start on the default design
        newProjectContents(doc);
    }
    savedRevision = doc.revision;
    autosaveRevision = doc.revision;
    return true;
}

void App::shutdown() {
    autosave(true);
    g.shutdown();
    imageCache.clear();  // its textures belong to the renderer we are about to destroy
    imageCache.setRenderer(nullptr);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
}

// Switching is instant: every label is looked up again on the next frame, so no
// restart is needed. Cached text textures belong to the old language, drop them.
void App::setLanguage(const std::string& code) {
    if (!i18n::setLang(code)) return;
    settingsSet("language", i18n::lang());
    g.clearTextCache();
    ui.closeMenus();
    ui.clearFocus();  // a field may still show an old-language placeholder
}

std::string App::autosavePath() const {
    char* pref = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string p = pref ? pref : "";
    SDL_free(pref);
    return p + "autosave.dez";
}

void App::autosave(bool force) {
    if (readOnly || scratchSession || doc.revision == autosaveRevision) return;
    Uint64 now = SDL_GetTicks();
    if (!force && now - lastAutosave < 2500) return;
    if (!force && drag != DRAG_NONE) return;
    Json j = doc.toJson(true);
    j.set("autosaveOf", filePath);
    j.set("autosaveDirty", dirty());
    writeTextFile(autosavePath(), j.dump(-1));
    lastAutosave = now;
    autosaveRevision = doc.revision;
}

void App::runOnMain(std::function<void()> f) {
    std::lock_guard<std::mutex> lk(pendingMutex);
    pending.push_back(std::move(f));
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void App::layout() {
    SDL_GetWindowSizeInPixels(win, &winW, &winH);
    float density = SDL_GetWindowPixelDensity(win);
    float display = SDL_GetWindowDisplayScale(win);
    float base = std::max(1.0f, display);
    ui.s = base * userScale;
    float s = ui.s;
    compact = (float)winW / s < 860;
    SDL_Rect safe{0, 0, winW, winH};
    if (SDL_GetWindowSafeArea(win, &safe)) {
        safe.x = (int)(safe.x * density); safe.y = (int)(safe.y * density);
        safe.w = (int)(safe.w * density); safe.h = (int)(safe.h * density);
    } else safe = {0, 0, winW, winH};
    float top = (float)safe.y, bottom = (float)(safe.y + safe.h);
    if (embedMode) {
        // viewer: a slim bar on top, the rest is canvas
        rTop = Rect((float)safe.x, top, (float)safe.w, 44 * s);
        rBottom = Rect((float)safe.x, bottom, (float)safe.w, 0);
        rRight = rBin = rInspector = Rect((float)(safe.x + safe.w), rTop.b(), 0, 0);
        rCanvas = Rect((float)safe.x, rTop.b(), (float)safe.w, bottom - rTop.b());
        return;
    }
    rTop = Rect((float)safe.x, top, (float)safe.w, 48 * s);
    float tbH = (compact ? 62.f : 60.f) * s;
    rBottom = Rect((float)safe.x, bottom - tbH, (float)safe.w, tbH);
    float mid = rBottom.y - rTop.b();
    if (!compact) {
        float rw = std::min(330 * s, (float)safe.w * 0.32f);
        rRight = Rect((float)(safe.x + safe.w) - rw, rTop.b(), rw, mid);
        rCanvas = Rect((float)safe.x, rTop.b(), rRight.x - safe.x, mid);
        float binH = std::floor(mid * (rightTab == 2 ? 0.8f : rightTab == 1 ? 0.68f : 0.54f));  // layers and AI chat get more room
        rBin = Rect(rRight.x, rRight.y, rRight.w, binH);
        rInspector = Rect(rRight.x, rRight.y + binH, rRight.w, mid - binH);
    } else {
        rCanvas = Rect((float)safe.x, rTop.b(), (float)safe.w, mid);
        float dw = std::min(380 * s, (float)safe.w * 0.9f);
        rRight = Rect((float)(safe.x + safe.w) - dw, rTop.b(), dw, mid);
        rBin = rInspector = rRight;
    }
    // program panel docks under the canvas
    if (codeOpen && doc.part(codePart)) {
        float h = compact ? std::floor(rCanvas.h * 0.58f) : std::clamp(std::floor(rCanvas.h * 0.45f), 240 * s, rCanvas.h - 160 * s);
        rCode = Rect(rCanvas.x, rCanvas.b() - h, rCanvas.w, h);
        rCanvas.h -= h;
    } else rCode = Rect();
}

void App::ensureConn() {
    if (connRev == doc.revision) return;
    conn.build(doc);
    connRev = doc.revision;
}

// ---------------------------------------------------------------------------
// Undo / change tracking
// ---------------------------------------------------------------------------

void App::changed() { doc.touch(); }

void App::checkpoint() {
    undoStack.push_back(doc.snapshot());
    if (undoStack.size() > 150) undoStack.erase(undoStack.begin());
    redoStack.clear();
}

void App::beginEdit() {
    editSnapshot = doc.snapshot();
    editOpen = true;
}

void App::endEdit() {
    if (!editOpen) return;
    editOpen = false;
    if (doc.snapshot() != editSnapshot) {
        undoStack.push_back(editSnapshot);
        if (undoStack.size() > 150) undoStack.erase(undoStack.begin());
        redoStack.clear();
    }
    editSnapshot.clear();
}

static void restoreSnapshot(Document& doc, const std::string& s) {
    int rev = doc.revision;
    std::string title = doc.title;
    std::string err;
    // Snapshots carry no picture bytes (Document::toJson leaves them out): the store
    // is content addressed and immutable, so deserialize() keeps what is in memory.
    doc.deserialize(s, &err);
    doc.revision = rev + 1;
}

void App::undo() {
    if (undoStack.empty()) { ui.toast(tr("toast.nothing_to_undo")); return; }
    redoStack.push_back(doc.snapshot());
    restoreSnapshot(doc, undoStack.back());
    undoStack.pop_back();
    pruneSelection();
}

void App::redo() {
    if (redoStack.empty()) { ui.toast(tr("toast.nothing_to_redo")); return; }
    undoStack.push_back(doc.snapshot());
    restoreSnapshot(doc, redoStack.back());
    redoStack.pop_back();
    pruneSelection();
}

void App::pruneSelection() {
    for (auto it = selParts.begin(); it != selParts.end();) it = doc.part(*it) ? std::next(it) : selParts.erase(it);
    for (auto it = selWires.begin(); it != selWires.end();) it = doc.wire(*it) ? std::next(it) : selWires.erase(it);
    for (auto it = selNotes.begin(); it != selNotes.end();) it = doc.note(*it) ? std::next(it) : selNotes.erase(it);
    for (auto it = selAnnos.begin(); it != selAnnos.end();) it = doc.anno(*it) ? std::next(it) : selAnnos.erase(it);
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

void App::confirmDiscard(std::function<void()> then) {
    if (!dirty()) { then(); return; }
    ui.dialog(tr("dlg.discard.title"), {trf("dlg.discard.body", {doc.title})},
              {{tr("dlg.dont_save"), then, false},
               {tr("dlg.cancel"), nullptr, false},
               {tr("dlg.save"), [this, then]() {
                    if (filePath.empty()) { saveAs(); return; }
                    save();
                    then();
                }, true}});
}

void App::newDoc() {
    confirmDiscard([this]() {
        clearCustomParts();
        reloadUserPartModels();  // the installed library survives "New design"
        customEntries.clear();
        newProjectContents(doc);
        filePath.clear();
        savedRevision = doc.revision;
        undoStack.clear();
        redoStack.clear();
        selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
        for (auto& c : cam) c.valid = false;
        simOn = false;
        drcIssues.clear();
    });
}

// ---------------------------------------------------------------------------
// First screen: open an existing project or start a new one
// ---------------------------------------------------------------------------

bool App::restoreAutosave() {
    bool ok = false;
    std::string text = readTextFile(autosavePath(), &ok);
    if (!ok || text.empty()) return false;
    Json j = Json::parse(text);
    std::string err;
    if (!doc.fromJson(j, &err)) { ui.dialog(tr("dlg.open_failed"), {err}, {}); return false; }
    filePath = j["autosaveOf"].str();
    savedRevision = j["autosaveDirty"].boolean() ? -1 : doc.revision;
    autosaveRevision = doc.revision;
    customEntries.clear();
    for (auto& lj : j["library"].items())
        if (isCustomType(lj["type"].str())) customEntries.push_back({lj["type"].str(), lj["title"].str(), "Custom", {}});
    undoStack.clear();
    redoStack.clear();
    selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
    for (auto& c : cam) c.valid = false;
    simOn = false;
    drcIssues.clear();
    startScreen = false;
    return true;
}

void App::startScreenNew() {
    newProjectContents(doc);
    filePath.clear();
    savedRevision = doc.revision;
    autosaveRevision = doc.revision;
    undoStack.clear();
    redoStack.clear();
    selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
    for (auto& c : cam) c.valid = false;
    simOn = false;
    drcIssues.clear();
    startScreen = false;
}

void App::drawStartScreen() {
    float s = ui.s;
    auto& th = ui.th;
    Rect full(0, 0, (float)winW, (float)winH);
    g.fillRect(full, th.bg);

    float cw = std::min(540 * s, full.w - 40 * s);
    float ch = std::min((autosaveAvailable ? 428.f : 388.f) * s, full.h - 40 * s);
    Rect card(full.center().x - cw / 2, full.center().y - ch / 2, cw, ch);
    g.fillRoundRect(Rect(card.x + 2 * s, card.y + 4 * s, card.w, card.h), 18 * s, rgba(0, 0, 0, 26));
    g.fillRoundRect(card, 18 * s, th.panel);
    g.strokeRoundRect(card, 18 * s, std::max(1.f, s), th.border);

    float pad = 30 * s;
    g.text({card.center().x, card.y + 46 * s}, "designElectronics", 30 * s, th.text, 1);
    g.text({card.center().x, card.y + 76 * s}, tr("view.breadboard") + "  ·  " + tr("view.schematic") + "  ·  " + tr("view.pcb"),
           11.5f * s, th.textDim, 1);
    float ty = card.y + 104 * s;
    ty += ui.wrappedText(Rect(card.x + pad, ty, card.w - 2 * pad, 0), tr("welcome.subtitle"), th.textDim, 13.5f * s) + 18 * s;

    Rect open(card.x + pad, ty, card.w - 2 * pad, 46 * s);
    if (ui.button("w-open", open, tr("welcome.open"), BTN_PRIMARY)) startScreenOpen();
    Rect nw(card.x + pad, open.b() + 10 * s, card.w - 2 * pad, 46 * s);
    if (ui.button("w-new", nw, tr("welcome.new"), BTN_NORMAL)) startScreenNew();
    if (autosaveAvailable) {
        Rect rr(card.x + pad, nw.b() + 10 * s, card.w - 2 * pad, 30 * s);
        if (ui.button("w-restore", rr, tr("welcome.restore"), BTN_FLAT)) {
            if (!restoreAutosave()) ui.toast(tr("welcome.restore_failed"));
        }
    }
    // Tools: small utilities that need no project (the same list as the top bar's Tools menu)
    float toolsY = (autosaveAvailable ? nw.b() + 10 * s + 30 * s : nw.b()) + 18 * s;
    if (toolsY + 60 * s < card.b()) {
        g.text({card.x + pad, toolsY + 8 * s}, tr("welcome.tools"), 12 * s, th.textDim, 0);
        float bx = card.x + pad, by = toolsY + 22 * s;
        for (auto& t : toolList()) {
            std::string name = tr(std::string("tool.") + t.id + ".name");
     float bw = g.textWidth(name, 14 * s) + 52 * s;
     Rect tb(bx, by, std::min(bw, card.r() - pad - bx), 36 * s);
     if (ui.button(std::string("w-tool-") + t.id, tb, "", BTN_NORMAL)) (this->*t.open)();
     ui.drawIcon(t.icon, Rect(tb.x + 10 * s, tb.center().y - 9 * s, 18 * s, 18 * s), th.text);
     g.text({tb.x + 36 * s, tb.center().y}, name, 14 * s, th.text, 0);
     bx += tb.w + 8 * s;
        }
    }
}

static bool endsWith(const std::string& s, const std::string& suf) {
    if (s.size() < suf.size()) return false;
    std::string a = s.substr(s.size() - suf.size());
    for (auto& c : a) c = (char)std::tolower((unsigned char)c);
    return a == suf;
}

// A dropped or picked file is treated as a picture when its name says so; the bytes
// themselves are sniffed by imageImport, never trusted from the name.
static bool isImagePath(const std::string& p) {
    static const char* exts[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".psd", ".tga", ".targa"};
    for (const char* e : exts)
        if (endsWith(p, e)) return true;
    return false;
}

bool App::loadFile(const std::string& path) {
    bool ok = false;
    std::string text = readTextFile(path, &ok);
    if (!ok) { ui.dialog(tr("dlg.open_failed"), {path, SDL_GetError()}, {}); return false; }
    std::string perr;
    Json j = Json::parse(text, &perr);
    if (!perr.empty()) { ui.dialog(tr("dlg.open_failed"), {tr("dlg.open_failed.json"), perr}, {}); return false; }
    if (j["format"].str() == "designElectronics-part" || (j.has("pins") && j.has("graphics"))) {
        PartModel m;
        std::string err;
        if (!partModelFromJson(j, m, &err)) { ui.dialog(tr("dlg.import_failed"), {err}, {}); return false; }
        if (isCustomType(m.def.type)) {
            PartModel copy = m;
            ui.dialog(tr("dlg.installed.title"),
                      {trf("dlg.installed.body", {m.meta.name, m.def.type})},
                      {{tr("dlg.cancel"), []() {}, false},
                       {tr("dlg.replace"), [this, copy]() {
                            std::string e;
                            if (!installPartModel(copy, true, &e, nullptr)) { ui.dialog(tr("dlg.import_failed"), {e}, {}); return; }
                            category = -2;
                            ui.toast(trf("toast.replaced", {copy.meta.name}));
                        }, true}});
            return true;
        }
        std::vector<std::string> warnings;
        if (!installPartModel(m, false, &err, &warnings)) { ui.dialog(tr("dlg.import_failed"), {err}, {}); return false; }
        category = -2;
        std::string msg = trf("toast.imported", {m.meta.name});
        if (!warnings.empty()) msg += " " + warnings.front();
        ui.toast(msg, 5);
        return true;
    }
    std::string err;
    std::vector<std::string> before;
    if (!doc.fromJson(j, &err)) { ui.dialog(tr("dlg.open_failed"), {err}, {}); return false; }
    // fromJson keeps the pictures already in memory (undo snapshots carry no bytes);
    // for a different design the previous ones are just dead weight.
    doc.pruneImages();
    customEntries.clear();
    for (auto& lj : j["library"].items())
        if (isCustomType(lj["type"].str())) customEntries.push_back({lj["type"].str(), lj["title"].str(), "Custom", {}});
    filePath = path;
    savedRevision = doc.revision;
    startScreen = false;  // an opened project replaces the first screen
    undoStack.clear();
    redoStack.clear();
    selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
    for (auto& c : cam) c.valid = false;
    drcIssues.clear();
    simOn = false;
    ui.toast(trf("toast.opened", {doc.title}));
    return true;
}

bool App::saveFile(const std::string& pathIn, const std::string& versionMessage) {
    std::string path = pathIn;
    bool uri = path.rfind("content://", 0) == 0;
    if (!uri && !endsWith(path, kFileExt)) path += kFileExt;
    if (doc.title == "Untitled" || doc.title.empty()) doc.title = uri ? doc.title : fileBaseName(path);
    history::ensureUid(doc);
    std::string data = doc.serialize(true);
    if (!writeTextFile(path, data)) {
        ui.dialog(tr("dlg.save_failed"), {path, SDL_GetError()}, {});
        return false;
    }
    filePath = path;
    savedRevision = doc.revision;
    autosave(true);
    ui.toast(trf("toast.saved", {uri ? doc.title : path}));
    recordVersion(data, versionMessage);  // local version history (+ GitHub when auto-push is on)
    return true;
}

void App::save() {
    if (filePath.empty() || filePath.rfind("content://", 0) == 0) { saveAs(); return; }
    saveFile(filePath);
}

static std::string documentsDir() {
    const char* d = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
    if (d && *d) return d;
    char* pref = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string p = pref ? pref : "";
    SDL_free(pref);
    return p;
}

static std::string sanitize(std::string s) {
    for (auto& c : s) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    return s.empty() ? "Untitled" : s;
}

struct DialogCtx {
    App* app;
    std::function<void(const std::string&)> onPath;
    std::function<void()> onUnsupported;
};

static void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int) {
    auto* ctx = (DialogCtx*)userdata;
    App* app = ctx->app;
    if (!filelist) {
        std::string err = SDL_GetError();
        auto fallback = ctx->onUnsupported;
        app->runOnMain([app, err, fallback]() {
            if (fallback) fallback();
            else app->ui.toast(trf("toast.dialog_unavailable", {err}));
        });
    } else if (filelist[0]) {
        std::string path = filelist[0];
        auto f = ctx->onPath;
        app->runOnMain([f, path]() { f(path); });
    }
    delete ctx;
}

// The welcome screen's "Open project": no discard prompt, nothing to lose yet.
void App::startScreenOpen() {
#ifdef __EMSCRIPTEN__
    webPickFile(".dez,.dezpart,.json");
    return;
#endif
    const SDL_DialogFileFilter f[] = {{tr("filter.project").c_str(), "dez"}};
    auto* ctx = new DialogCtx{this, [this](const std::string& p) { loadFile(p); }, nullptr};
    SDL_ShowOpenFileDialog(dialogCallback, ctx, win, f, 1, documentsDir().c_str(), false);
}

static void showSave(App* app, const std::string& defName, const SDL_DialogFileFilter* filters, int n,
                     std::function<void(const std::string&)> onPath) {
#ifdef __EMSCRIPTEN__
    (void)app; (void)filters; (void)n;
    webSaveAndDownload(defName, onPath);  // browsers: the file becomes a download
    return;
#endif
    std::string def = documentsDir();
    if (!def.empty() && def.back() != '/' && def.back() != '\\') def += "/";
    def += defName;
    auto* ctx = new DialogCtx{app, onPath, [app, def, onPath]() {
        // No native dialog (e.g. iOS): save into the app's Documents folder, which
        // is visible in the Files app and can be shared from there.
        onPath(def);
        app->ui.toast(trf("toast.saved_to", {def}));
    }};
    SDL_ShowSaveFileDialog(dialogCallback, ctx, app->win, filters, n, def.c_str());
}

void App::showSaveDialog(const std::string& name, const SDL_DialogFileFilter* filters, int n, std::function<void(const std::string&)> onPath) {
    showSave(this, name, filters, n, std::move(onPath));
}

void App::saveAs() {
    // Not static: the filter label has to follow the interface language.
    const SDL_DialogFileFilter f[] = {{tr("filter.project").c_str(), "dez"}};
    std::string name = sanitize(doc.title) + kFileExt;
    showSave(this, name, f, 1, [this](const std::string& p) { saveFile(p); });
}

void App::openDialog() {
    confirmDiscard([this]() {
#ifdef __EMSCRIPTEN__
        webPickFile(".dez,.dezpart,.json");
        return;
#endif
        const SDL_DialogFileFilter f[] = {{tr("filter.files").c_str(), "dez;dezpart;json"}, {tr("filter.all").c_str(), "*"}};
        auto* ctx = new DialogCtx{this, [this](const std::string& p) { loadFile(p); }, nullptr};
        std::string dir = documentsDir();
        SDL_ShowOpenFileDialog(dialogCallback, ctx, win, f, 2, dir.c_str(), false);
    });
}

void App::importPartDialog() {
#ifdef __EMSCRIPTEN__
    webPickFile(".dezpart,.json");
    return;
#endif
    const SDL_DialogFileFilter f[] = {{tr("filter.part").c_str(), "dezpart;json"}};
    auto* ctx = new DialogCtx{this, [this](const std::string& p) { loadFile(p); }, nullptr};
    SDL_ShowOpenFileDialog(dialogCallback, ctx, win, f, 1, documentsDir().c_str(), false);
}

void App::exportFile(const std::string& kind) {
    ensureConn();
    std::string base = sanitize(doc.title);
    if (kind == "gerber") {
        const SDL_DialogFileFilter f[] = {{tr("filter.zip").c_str(), "zip"}};
        showSave(this, base + "_gerber.zip", f, 1, [this](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".zip")) path += ".zip";
            ensureConn();
            std::string msg;
            if (exportGerberZip(doc, conn, path, &msg)) ui.toast(msg, 5);
            else ui.dialog(tr("dlg.export_failed"), {msg}, {});
        });
    } else if (kind == "svg") {
        const SDL_DialogFileFilter f[] = {{tr("filter.svg").c_str(), "svg"}};
        int v = view;
        showSave(this, base + "_" + viewKey(v) + ".svg", f, 1, [this, v](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".svg")) path += ".svg";
            ensureConn();
            if (writeTextFile(path, exportSvg(doc, conn, v))) ui.toast(trf("toast.exported", {path}));
            else ui.dialog(tr("dlg.export_failed"), {SDL_GetError()}, {});
        });
    } else if (kind == "png") {
        const SDL_DialogFileFilter f[] = {{tr("filter.png").c_str(), "png"}};
        int v = view;
        showSave(this, base + "_" + viewKey(v) + ".png", f, 1, [this, v](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".png")) path += ".png";
            std::string msg;
            if (renderViewPng(v, path, &msg)) ui.toast(trf("toast.exported", {path}));
            else ui.dialog(tr("dlg.export_failed"), {msg}, {});
        });
    } else if (kind == "bom" || kind == "netlist" || kind == "spice") {
        const char* ext = kind == "bom" ? "csv" : kind == "netlist" ? "xml" : "cir";
        SDL_DialogFileFilter f[1];
        f[0] = {tr(kind == "bom" ? "filter.csv" : kind == "netlist" ? "filter.xml" : "filter.spice").c_str(), ext};
        std::string suffix = kind == "bom" ? "_bom.csv" : kind == "netlist" ? "_netlist.xml" : ".cir";
        showSave(this, base + suffix, f, 1, [this, kind, ext](const std::string& p) {
            std::string path = p;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, std::string(".") + ext)) path += std::string(".") + ext;
            ensureConn();
            std::string data = kind == "bom" ? exportBomCsv(doc) : kind == "netlist" ? exportNetlistXml(doc, conn) : exportSpice(doc, conn);
            if (writeTextFile(path, data)) ui.toast(trf("toast.exported", {path}));
            else ui.dialog(tr("dlg.export_failed"), {SDL_GetError()}, {});
        });
    } else if (kind == "part") {
        if (selParts.size() != 1) { ui.toast(tr("toast.select_one_part")); return; }
        const PartInst* p = doc.part(*selParts.begin());
        if (!p) return;
        PartDef def = *p->def;
        def.defaults = mergedProps(p->type, p->props);
        PartModel m;
        m.def = def;
        m.meta.id = def.type;
        m.meta.name = def.title;
        m.meta.category = def.category;
        m.meta.datasheet = def.datasheet;
        m.meta.source = "json";
        std::string data = partModelToJson(m).dump(1);
        const SDL_DialogFileFilter f[] = {{tr("filter.part").c_str(), "dezpart"}};
        showSave(this, sanitize(p->def->title) + ".dezpart", f, 1, [this, data](const std::string& path0) {
            std::string path = path0;
            if (path.rfind("content://", 0) != 0 && !endsWith(path, ".dezpart")) path += ".dezpart";
            if (writeTextFile(path, data)) ui.toast(trf("toast.exported_part", {path}));
        });
    }
}

// ---------------------------------------------------------------------------
// Part models: the custom parts in the user library
// ---------------------------------------------------------------------------

void App::showPartModelInfo(const std::string& type) {
    PartModel m;
    if (!userPartModel(type, m)) {
        PartDefPtr d = getPartDef(type, {});
        if (!d) return;
        m.def = *d;
        m.meta.id = type;
        m.meta.name = d->title;
        m.meta.category = d->category;
        m.meta.datasheet = d->datasheet;
        m.meta.source = "built-in";
    }
    std::vector<std::string> lines;
    lines.push_back(trf("dlg.model_info.id", {m.def.type}));
    lines.push_back(trf("dlg.model_info.category", {partCategory(m.def)}));
    lines.push_back(m.def.sim.empty() ? trf("dlg.model_info.pins_nosim", {std::to_string(m.def.pins.size())})
                                      : trf("dlg.model_info.pins_sim", {std::to_string(m.def.pins.size()), m.def.sim}));
    if (!m.def.silkscreen.empty()) lines.push_back(trf("dlg.model_info.silkscreen", {m.def.silkscreen}));
    if (!m.meta.datasheet.empty()) lines.push_back(trf("dlg.model_info.datasheet", {m.meta.datasheet}));
    if (!m.def.tags.empty()) lines.push_back(trf("dlg.model_info.tags", {m.def.tags}));
    if (!m.path.empty()) lines.push_back(trf("dlg.model_info.file", {m.path}));
    if (!m.def.description.empty()) {
        lines.push_back("");
        lines.push_back(m.def.description);
    }
    ui.dialog(m.meta.name.empty() ? partTitle(m.def) : m.meta.name, lines, {});
}

void App::exportPartModelDialog(const std::string& type) {
    PartModel m;
    if (!userPartModel(type, m)) {
        PartDefPtr d = getPartDef(type, {});
        if (!d) return;
        m.def = *d;
        m.meta.id = type;
        m.meta.name = d->title;
        m.meta.category = d->category;
        m.meta.datasheet = d->datasheet;
        m.meta.source = "json";
    }
    std::string data = partModelToJson(m).dump(1);
    const SDL_DialogFileFilter f[] = {{tr("filter.part").c_str(), "dezpart"}};
    showSaveDialog(sanitize(m.def.title) + ".dezpart", f, 1, [this, data](const std::string& path0) {
        std::string path = path0;
        if (path.rfind("content://", 0) != 0 && !endsWith(path, ".dezpart")) path += ".dezpart";
        if (writeTextFile(path, data)) ui.toast(trf("toast.exported_model", {path}));
        else ui.dialog(tr("dlg.export_failed"), {SDL_GetError()}, {});
    });
}

void App::deletePartModelConfirm(const std::string& type) {
    PartModel m;
    if (!userPartModel(type, m)) return;
    std::string name = m.meta.name.empty() ? type : m.meta.name;
    ui.dialog(tr("dlg.delete_model.title"),
              {trf("dlg.delete_model.body", {name})},
              {{tr("dlg.cancel"), []() {}, false},
               {tr("dlg.delete"), [this, type]() {
                    std::string err;
                    if (!removePartModel(type, &err)) ui.dialog(tr("dlg.delete_failed"), {err}, {});
                    else ui.toast(trf("toast.deleted_model", {type}));
                }, true}});
}

void App::loadExample(size_t i) {
    confirmDiscard([this, i]() {
        doc.clear();
        examples()[i].build(doc);
        doc.syncWireEnds();
        doc.touch();
        filePath.clear();
        savedRevision = doc.revision;
        undoStack.clear();
        redoStack.clear();
        selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
        for (auto& c : cam) c.valid = false;
        drcIssues.clear();
        simOn = false;
        ui.toast(trf("toast.loaded_example", {examples()[i].title}));
    });
}

void App::showAbout() {
    const char* keys[] = {"about.p1", "about.p2", "about.p3", "about.p4", "about.p5", "about.p6"};
    std::vector<std::string> lines;
    for (const char* k : keys) {
        if (!lines.empty()) lines.push_back("");
        lines.push_back(tr(k));
    }
    ui.dialog("designElectronics " + std::string(kAppVersion), lines, {});
}

// The dialog is generated from the binding table, so it always shows what the keys
// really do — including whatever the user rebound in Settings.
std::vector<std::string> App::shortcutLines() const {
    std::vector<std::string> lines;
    lines.reserve(kActionCount + 7);
    for (int i = 0; i < kActionCount; i++) {
        Action a = (Action)i;
        lines.push_back(trf("shortcut.entry", {actionLabel(a), chordToText(binds.chord[i])}));
    }
    lines.push_back(std::string());
    lines.push_back(tr("shortcut.10"));
    lines.push_back(tr("shortcut.11"));
    lines.push_back(tr("shortcut.14"));  // add / remove a bend point, on the canvas
    lines.push_back(tr("shortcut.12"));
    lines.push_back(tr("shortcut.13"));
    lines.push_back(tr("shortcut.15"));  // where all of this can be changed
    return lines;
}

void App::showShortcuts() { ui.dialog(tr("dlg.shortcuts.title"), shortcutLines(), {}); }

// ---------------------------------------------------------------------------
// Keyboard & mouse mapping (the table lives in keys.cpp)
// ---------------------------------------------------------------------------

void App::loadBindings() {
    binds = defaultBindings();
    for (int i = 0; i < kActionCount; i++) {
        std::string key = std::string("shortcut.") + actionId((Action)i);
        std::string stored = settingsGet(key.c_str(), "\x01");
        if (stored == "\x01") continue;  // never saved: keep the default
        KeyChord c;
        if (!chordFromText(stored, &c)) {
            SDL_Log("settings: %s = '%s' is not a key combination, using the default", key.c_str(), stored.c_str());
            continue;
        }
        binds.chord[i] = c;
    }
    static const char* slots[kMouseSlots] = {"mouse.middle", "mouse.right", "mouse.space_left"};
    for (int i = 0; i < kMouseSlots; i++) {
        MouseRole r;
        if (mouseRoleFromId(settingsGet(slots[i], ""), &r)) binds.mouse[i] = r;
    }
}

void App::saveBindings() {
    for (int i = 0; i < kActionCount; i++) {
        std::string key = std::string("shortcut.") + actionId((Action)i);
        settingsSet(key.c_str(), chordToToken(binds.chord[i]));
    }
    static const char* slots[kMouseSlots] = {"mouse.middle", "mouse.right", "mouse.space_left"};
    for (int i = 0; i < kMouseSlots; i++) settingsSet(slots[i], mouseRoleId(binds.mouse[i]));
}

void App::applyBinding(Action a, const KeyChord& c, int conflictAction) {
    if (conflictAction >= 0 && conflictAction < kActionCount && conflictAction != (int)a) binds.chord[conflictAction] = KeyChord();
    binds.chord[(int)a] = c;
    saveBindings();
}

// Runs one table action. An action that does not apply is a no-op.
void App::runAction(Action a) {
    switch (a) {
        case Action::Undo: undo(); break;
        case Action::Redo: redo(); break;
        case Action::Delete: deleteSelection(); break;
        case Action::Copy: copySelection(false); break;
        case Action::Paste: paste(); break;
        case Action::Rotate: rotateSelection(in.shift() ? -1 : 1); break;
        case Action::Flip: flipSelection(); break;
        case Action::AddBend: addBendAtPointer(); break;
        case Action::ViewBB: setView(BB); break;
        case Action::ViewSCH: setView(SCH); break;
        case Action::ViewPCB: setView(PCB); break;
        case Action::ToolSelect:
            if (tool != TOOL_SELECT) cancelAnnoTool();
            else { selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear(); }
            break;
        case Action::ToolWire: setTool(TOOL_WIRE); break;
        case Action::ToolShape: cycleShapeTool(); break;
        case Action::ToolText: setTool(TOOL_TEXT); break;
        case Action::Save: in.shift() ? saveAs() : save(); break;
        case Action::kCount: break;
    }
}

// Adds a bend point where the pointer is, on the wire segment under it.
void App::addBendAtPointer() {
    if (readOnly || !rCanvas.contains(in.mouse)) return;
    Hit h = hitTest(in.mouse);
    if (h.kind == Hit::WIRE_SEG) insertBendAt(h, toWorld(in.mouse));
}

// ---------------------------------------------------------------------------
// Editing commands
// ---------------------------------------------------------------------------

void App::deleteSelection() {
    if (selParts.empty() && selWires.empty() && selNotes.empty() && selAnnos.empty()) return;
    std::set<int> keep;
    for (int id : selParts) if (auto* p = doc.part(id)) if (p->locked) keep.insert(id);
    if (!keep.empty()) ui.toast(trn("toast.locked_kept", (long long)keep.size()));
    if (keep.size() == selParts.size() && selWires.empty() && selNotes.empty() && selAnnos.empty()) return;
    checkpoint();
    for (int id : selWires) doc.removeWire(id);
    for (int id : selNotes) doc.removeNote(id);
    for (int id : selAnnos) doc.removeAnno(id);
    for (int id : selParts) if (!keep.count(id)) doc.removePart(id);
    selParts = keep; selWires.clear(); selNotes.clear(); selAnnos.clear();
    changed();
}

static void snapPart(PartInst& p, int v, float grid) {
    if (!p.def || p.def->pins.empty() || grid <= 0) { p.place[v].pos = snapv(p.place[v].pos, grid); return; }
    Vec2 pin0 = p.pinPos(0, v);
    p.place[v].pos += snapv(pin0, grid) - pin0;
}

void App::rotateSelection(int dir) {
    if (selParts.empty()) return;
    checkpoint();
    for (int id : selParts) {
        PartInst* p = doc.part(id);
        if (!p || !p->inView(view) || p->def->isBoard || p->locked) continue;
        Vec2 c = p->worldBounds(view).center();
        p->place[view].rot = ((p->place[view].rot + dir) % 4 + 4) % 4;
        p->place[view].pos += c - p->worldBounds(view).center();
        if (snapToGrid) snapPart(*p, view, gridSize(view));
    }
    doc.syncWireEnds();
    retidyAround(selParts);
    changed();
}

void App::flipSelection() {
    if (selParts.empty()) return;
    checkpoint();
    for (int id : selParts) {
        PartInst* p = doc.part(id);
        if (!p || !p->inView(view) || p->def->isBoard || p->locked) continue;
        Vec2 c = p->worldBounds(view).center();
        p->place[view].flip = !p->place[view].flip;
        p->place[view].pos += c - p->worldBounds(view).center();
        if (snapToGrid) snapPart(*p, view, gridSize(view));
    }
    doc.syncWireEnds();
    retidyAround(selParts);
    changed();
}

void App::copySelection(bool cut) {
    if (selParts.empty() && selWires.empty() && selNotes.empty() && selAnnos.empty()) return;
    Document clip;
    clip.title = "clipboard";
    for (auto& p : doc.parts) if (selParts.count(p.id)) clip.parts.push_back(p);
    for (auto& w : doc.wires) {
        bool both = w.ends[0].attached() && w.ends[1].attached() && selParts.count(w.ends[0].part) && selParts.count(w.ends[1].part);
        if (both || selWires.count(w.id)) {
            Wire c = w;
            for (auto& e : c.ends) if (e.attached() && !selParts.count(e.part)) e = WireEnd();
            clip.wires.push_back(c);
        }
    }
    for (auto& n : doc.notes) if (selNotes.count(n.id)) clip.notes.push_back(n);
    for (auto& a : doc.annos) if (selAnnos.count(a.id)) clip.annos.push_back(a);
    // …and the pictures those annotations refer to, or a paste elsewhere would show empty
    // frames. The store is content addressed, so copying a picture twice costs one entry.
    for (auto& a : clip.annos)
        if (!a.image.empty())
            if (const ImageBlob* b = doc.image(a.image)) clip.images.emplace(a.image, *b);
    clip.nextId = doc.nextId;
    // Pictures travel with the markup that refers to them. The clipboard asks for the
    // bytes without the rest of the full form (no timestamp, no part library); on paste
    // they are merged back into the content-addressed store.
    SDL_SetClipboardText(clip.toJson(false, true).dump(-1).c_str());
    if (cut) deleteSelection();
    else ui.toast(trn("toast.copied", (long long)(clip.parts.size() + clip.wires.size() + clip.notes.size() + clip.annos.size())));
}

void App::paste() {
    char* t = SDL_GetClipboardText();
    std::string text = t ? t : "";
    SDL_free(t);
    Document clip;
    std::string err;
    if (text.empty() || !clip.deserialize(text, &err)) {
        // Not our clipboard JSON: a copied picture (a screenshot, an image from a browser),
        // then a copied path to a picture file. Our own JSON goes first, so copying markup
        // in this app and pasting it never turns into a picture by accident.
        if (pasteClipboardImage()) return;
        if (text.find('\n') == std::string::npos && isImagePath(text) && SDL_GetPathInfo(text.c_str(), nullptr)) {
            insertImageFile(text);
            return;
        }
        ui.toast(tr("toast.nothing_to_paste"));
        return;
    }
    checkpoint();
    // The pictures the pasted markup refers to: the store is content addressed, so
    // adding them is idempotent and undo/redo keeps them (see Document::fromJson).
    for (auto& kv : clip.images) doc.images.emplace(kv.first, kv.second);
    std::map<int, int> idMap;
    selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
    Vec2 off(200, 200);
    for (auto& p : clip.parts) {
        PartInst n = p;
        n.id = doc.newId();
        n.group = 0;  // a pasted copy starts on its own level
        idMap[p.id] = n.id;
        for (auto& pl : n.place) pl.pos += off;
        // keep labels unique
        bool clash = false;
        for (auto& q : doc.parts) if (q.label == n.label) clash = true;
        if (clash) n.label = doc.nextLabel(n.def ? n.def->labelPrefix : "U");
        if (n.def && (n.def->isBoard || n.def->isBreadboard)) doc.parts.insert(doc.parts.begin(), n);
        else doc.parts.push_back(n);
        selParts.insert(n.id);
    }
    for (auto& w : clip.wires) {
        Wire n = w;
        n.id = doc.newId();
        for (auto& pt : n.pts) pt += off;
        for (auto& e : n.ends) {
            if (!e.attached()) continue;
            auto it = idMap.find(e.part);
            if (it == idMap.end()) e = WireEnd(); else e.part = it->second;
        }
        doc.wires.push_back(n);
        if (n.view == view) selWires.insert(n.id);
    }
    for (auto& nt : clip.notes) {
        Note n = nt;
        n.id = doc.newId();
        n.pos += off;
        doc.notes.push_back(n);
        selNotes.insert(n.id);
    }
    for (auto& at : clip.annos) {
        Anno a = at;
        a.id = doc.newId();
        annoTranslate(a, off);
        doc.annos.push_back(a);
        if (a.view == view) selAnnos.insert(a.id);
    }
    doc.syncWireEnds();
    changed();
}

void App::duplicate() {
    char* t = SDL_GetClipboardText();
    std::string saved = t ? t : "";
    SDL_free(t);
    copySelection(false);
    paste();
    SDL_SetClipboardText(saved.c_str());
}

void App::selectAll() {
    selParts.clear(); selWires.clear(); selNotes.clear(); selAnnos.clear();
    for (auto& p : doc.parts) if (p.inView(view)) selParts.insert(p.id);
    for (auto& w : doc.wires) if (w.view == view) selWires.insert(w.id);
    for (auto& n : doc.notes) if (n.view == view) selNotes.insert(n.id);
    for (auto& a : doc.annos) if (a.view == view && !a.hidden) selAnnos.insert(a.id);
}

void App::addPart(const LibEntry& e, Vec2 world) {
    PartDefPtr def = getPartDef(e.type, e.props);
    if (!def) return;
    if (!def->inView(view)) {
        for (int v = 0; v < NUM_VIEWS; v++)
            if (def->inView(v)) {
                ui.toast(trf("toast.not_in_view", {partTitle(*def), viewLabel(v)}));
                setView(v);
                Rect vp = rCanvas;
                if (!cam[v].valid) fitView(v, vp);
                world = (vp.center() - cam[v].pan) / cam[v].zoom;
                break;
            }
    }
    checkpoint();
    PartInst& p = doc.addPart(e.type, e.props, {0, 0});
    int cascade = (int)(doc.parts.size() % 8);
    for (int v = 0; v < NUM_VIEWS; v++) {
        Vec2 pos;
        if (v == view) pos = world - p.def->bounds[v].center();
        else if (cam[v].valid) pos = (rCanvas.center() - cam[v].pan) / cam[v].zoom - p.def->bounds[v].center() + Vec2(cascade * 100.f, cascade * 100.f);
        else pos = Vec2(cascade * 300.f, cascade * 200.f);
        p.place[v].pos = pos;
        snapPart(p, v, gridSize(v));
    }
    selParts = {p.id};
    selWires.clear(); selNotes.clear(); selAnnos.clear();
    changed();
}

void App::addNote() {
    checkpoint();
    Note n;
    n.id = doc.newId();
    n.view = view;
    n.size = Vec2(1600, 700);
    n.pos = toWorld(rCanvas.center()) - n.size / 2;
    n.text = tr("note.placeholder");
    doc.notes.push_back(n);
    selParts.clear(); selWires.clear();
    selNotes = {n.id};
    changed();
    if (compact) drawerInspector = true;
}

// A new annotation in the middle of the canvas, ready to be moved or resized.
void App::addAnno(AnnoKind kind) {
    checkpoint();
    Anno a;
    a.id = doc.newId();
    a.view = view;
    a.kind = kind;
    Vec2 ctr = toWorld(rCanvas.center());
    switch (kind) {
        case AnnoKind::Rect:
            a.a = ctr - Vec2(900, 600);
            a.b = ctr + Vec2(900, 600);
            break;
        case AnnoKind::Line:
            a.a = ctr - Vec2(1200, 0);
            a.b = ctr + Vec2(1200, 0);
            break;
        case AnnoKind::Circle:
            a.a = ctr;
            a.b = ctr + Vec2(800, 0);
            break;
        case AnnoKind::Text:
            a.a = ctr - Vec2(900, 300);
            a.b = Vec2(1800, 0);
            annoSetText(a, tr("anno.placeholder"));
            break;
    }
    doc.annos.push_back(a);
    selParts.clear(); selWires.clear(); selNotes.clear();
    selAnnos = {a.id};
    changed();
    if (compact) drawerInspector = true;
}

// ---------------------------------------------------------------------------
// Pictures: insert, paste, drop (formats and limits live in image.h)
// ---------------------------------------------------------------------------

Anno* App::addImageAnno(const ImageBlob& blob) {
    if (readOnly || blob.bytes.empty() || blob.w <= 0 || blob.h <= 0) return nullptr;
    // The natural size in world units, then capped to about 60% of what is on screen:
    // a screenshot or a phone photo lands as something that can be worked with.
    constexpr float kWorldPerPx = 12.f;  // a 96 dpi picture at the app's scale
    Vec2 size((float)blob.w * kWorldPerPx, (float)blob.h * kWorldPerPx);
    const float z = std::max(cam[view].zoom, 1e-6f);
    const float maxW = std::max(240.f, rCanvas.w * 0.6f / z), maxH = std::max(160.f, rCanvas.h * 0.6f / z);
    const float k = std::min(1.f, std::min(maxW / size.x, maxH / size.y));
    size = size * k;
    Anno a;
    a.id = doc.newId();
    a.view = view;
    a.kind = AnnoKind::Image;
    a.image = doc.addImage(blob);  // content addressed: the same file placed twice is one copy
    a.b = size;
    a.a = toWorld(rCanvas.center()) - size / 2;
    a.stroke = annoPalette()[0];
    a.dashed = false;  // a picture is frameless by default; the border is opt-in
    a.framed = false;
    checkpoint();
    doc.annos.push_back(a);
    selParts.clear();
    selWires.clear();
    selNotes.clear();
    selAnnos = {a.id};
    changed();
    if (compact) drawerInspector = true;
    return doc.anno(a.id);
}

bool App::insertImageBytes(const std::string& bytes, const std::string& name) {
    if (readOnly) return false;
    ImageBlob blob;
    std::string err, note;
    if (!imageImport(bytes, name, &blob, &err, &note)) {
        ui.toast(trf("toast.image_failed", {err}), 6);
        return false;
    }
    if (!addImageAnno(blob)) return false;
    if (!note.empty()) ui.toast(trf("toast.image_scaled", {note}), 5);
    return true;
}

bool App::insertImageFile(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        ui.toast(trf("toast.image_open_failed", {SDL_GetError()}), 6);
        return false;
    }
    std::string bytes((const char*)data, size);
    SDL_free(data);
    return insertImageBytes(bytes, path);
}

void App::insertImageDialog() {
#ifdef __EMSCRIPTEN__
    webPickImage();
    return;
#endif
    const SDL_DialogFileFilter f[] = {{tr("filter.images").c_str(), "png;jpg;jpeg;gif;bmp;webp;svg;tga"}};
    auto* ctx = new DialogCtx{this, [this](const std::string& p) { insertImageFile(p); }, nullptr};
    SDL_ShowOpenFileDialog(dialogCallback, ctx, win, f, 1, documentsDir().c_str(), false);
}

// Ctrl+V with a picture on the system clipboard: the same import path the menu and a
// dropped file use. Windows (and most desktops) hand bitmaps over as image/bmp.
bool App::pasteClipboardImage() {
    static const char* kMimes[] = {"image/png", "image/webp", "image/jpeg", "image/svg+xml", "image/bmp"};
    for (const char* mime : kMimes) {
        if (!SDL_HasClipboardData(mime)) continue;
        size_t len = 0;
        void* data = SDL_GetClipboardData(mime, &len);
        if (!data) continue;
        std::string bytes((const char*)data, len);
        SDL_free(data);
        if (bytes.empty()) continue;
        return insertImageBytes(bytes, mime);
    }
    return false;
}

void App::setImageRotation(const std::set<int>& ids, float degrees) {
    float deg = std::fmod(degrees, 360.f);
    if (deg <= -180.f) deg += 360.f;
    if (deg > 180.f) deg -= 360.f;
    std::vector<Anno*> targets;
    for (int id : ids) {
        Anno* a = doc.anno(id);
        if (a && a->kind == AnnoKind::Image && std::fabs(a->rotation - deg) > 0.001f) targets.push_back(a);
    }
    if (targets.empty()) return;
    checkpoint();
    for (Anno* a : targets) a->rotation = deg;
    changed();
}

void App::setPartProp(int partId, const std::string& key, const std::string& value) {
    setPartProps(partId, Props{{key, value}});
}

void App::setPartProps(int partId, const Props& values) {
    PartInst* p = doc.part(partId);
    if (!p) return;
    Props wanted = values;
    // "4k7" and "R47" are resistances, not "4 kilo" and "R47": store the canonical text
    // so the simulator, the BOM and the printed marking all read the same number.
    auto r = wanted.find("resistance");
    if (r != wanted.end() && p->type == "resistor") {
 double ohms = cc::parseResistance(r->second, -1);
        if (ohms >= 0) r->second = cc::formatOhms(ohms);
    }
    bool differs = false;
    for (auto& kv : wanted) differs = differs || p->prop(kv.first) != kv.second;
    if (!differs) return;
    checkpoint();
    Props before = p->props;
    for (auto& kv : wanted) p->props[kv.first] = kv.second;
    p->refresh();
    if (!p->def) { p->props = before; p->refresh(); }
    doc.syncWireEnds();
    changed();
}

void App::bringToFront(bool front) {
    if (selParts.empty()) return;
    checkpoint();
    std::vector<PartInst> moved, rest;
    for (auto& p : doc.parts) (selParts.count(p.id) && !p.def->isBoard && !p.def->isBreadboard ? moved : rest).push_back(p);
    doc.parts.clear();
    if (front) { doc.parts = rest; doc.parts.insert(doc.parts.end(), moved.begin(), moved.end()); }
    else {
        // keep boards/breadboards at the very back
        size_t k = 0;
        while (k < rest.size() && (rest[k].def->isBoard || rest[k].def->isBreadboard)) k++;
        doc.parts.assign(rest.begin(), rest.begin() + (long)k);
        doc.parts.insert(doc.parts.end(), moved.begin(), moved.end());
        doc.parts.insert(doc.parts.end(), rest.begin() + (long)k, rest.end());
    }
    changed();
}

void App::runAutoroute() {
    ensureConn();
    checkpoint();
    RouteResult r = autoroute(doc, conn, true);
    doc.touch();
    ensureConn();
    ui.toast(r.message, 5);
}

// Tidies the selected wires in this view, or all of them.
void App::tidyCommand() {
    std::set<int> only;
    for (int id : selWires) if (const Wire* w = doc.wire(id)) if (w->view == view) only.insert(id);
    checkpoint();
    int n = tidyWires(doc, view, only.empty() ? nullptr : &only);
    doc.syncWireEnds();
    changed();
    ui.toast(n ? trn("toast.tidied", n) : tr("toast.nothing_to_tidy"));
}

// Moving or rotating parts stretches their wires; lay the tidy ones out again.
void App::retidyAround(const std::set<int>& parts, const std::set<int>& skipWires) {
    if (!autoTidy || parts.empty()) return;
    std::set<int> ids;
    for (auto& w : doc.wires) {
        if (w.view != view || !w.tidy || skipWires.count(w.id)) continue;
        for (auto& e : w.ends)
            if (e.attached() && parts.count(e.part)) ids.insert(w.id);
    }
    if (!ids.empty()) tidyWires(doc, view, &ids);
}

void App::runDrc() {
    ensureConn();
    drcIssues = ::runDrc(doc, conn);
    drcRev = doc.revision;
    if (drcIssues.empty()) {
        ui.dialog(tr("dlg.drc.title"), {tr("dlg.drc.ok")}, {});
        return;
    }
    std::vector<std::string> lines;
    lines.push_back(trn("dlg.drc.issues", (long long)drcIssues.size()));
    lines.push_back("");
    for (auto& i : drcIssues) lines.push_back("• " + i.message);
    ui.dialog(tr("dlg.drc.title"), lines, {});
}

void App::toggleSim() {
    simOn = !simOn;
    if (simOn) serialLog.clear();  // like resetting the board
    simRev = -1;
    simulator.reset();
    lastSimTicks = 0;
    simPressed.clear();
    if (simOn && view == PCB) setView(BB);
    ui.toast(tr(simOn ? "toast.sim_on" : "toast.sim_off"));
}

void App::setView(int v) {
    if (v == view) return;
    view = v;
    ui.clearFocus();
    ui.closeMenus();
}

void App::handleShortcuts() {
    // The rebindable table comes first, and an action it handles ends the frame's
    // keyboard work. Unbinding an action therefore really disables it: the keys that
    // belong to the table are gone from the fallback switch below.
    int act = actionFor(binds, in);
    if (act >= 0) {
        runAction((Action)act);
        return;
    }
    bool c = in.cmd(), sh = in.shift();
    for (auto k : in.keys) {
        if (c) {
            switch (k) {
                case SDLK_Y: redo(); break;  // Ctrl+Y is a second redo, as in most editors
                case SDLK_O: openDialog(); break;
                case SDLK_N: newDoc(); break;
                case SDLK_H: if (sh) openHistory(); break;
                case SDLK_K: if (sh) openCommitSheet(); break;
                case SDLK_X: copySelection(true); break;
                case SDLK_D: duplicate(); break;
                case SDLK_A: selectAll(); break;
                case SDLK_0: fitView(view, rCanvas); break;
                case SDLK_EQUALS: case SDLK_PLUS: zoomAt(1.25f, rCanvas.center()); break;
                case SDLK_MINUS: zoomAt(0.8f, rCanvas.center()); break;
                default: break;
            }
            continue;
        }
        switch (k) {
            case SDLK_T: tidyCommand(); break;
            case SDLK_EQUALS: case SDLK_PLUS: case SDLK_KP_PLUS: zoomAt(1.25f, rCanvas.center()); break;
            case SDLK_MINUS: case SDLK_KP_MINUS: zoomAt(0.8f, rCanvas.center()); break;
            case SDLK_0: fitView(view, rCanvas); break;
            case SDLK_LEFT: case SDLK_RIGHT: case SDLK_UP: case SDLK_DOWN: {
                if (selParts.empty() && selWires.empty() && selNotes.empty() && selAnnos.empty()) break;
                float step = gridSize(view) * (sh ? 10 : 1);
                Vec2 d = k == SDLK_LEFT ? Vec2(-step, 0) : k == SDLK_RIGHT ? Vec2(step, 0) : k == SDLK_UP ? Vec2(0, -step) : Vec2(0, step);
                checkpoint();
                for (int id : selParts) if (auto* p = doc.part(id)) if (!p->locked) p->place[view].pos += d;
                for (int id : selWires) if (auto* w = doc.wire(id)) for (size_t i = 0; i < w->pts.size(); i++) {
                    bool endAttached = (i == 0 && w->ends[0].attached()) || (i + 1 == w->pts.size() && w->ends[1].attached());
                    if (!endAttached) w->pts[i] += d;
                }
                for (int id : selNotes) if (auto* n = doc.note(id)) n->pos += d;
                for (int id : selAnnos) if (auto* an = doc.anno(id)) annoTranslate(*an, d);
                doc.syncWireEnds();
                changed();
                break;
            }
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Events & frame
// ---------------------------------------------------------------------------

SDL_AppResult App::event(const SDL_Event& e) {
    float density = win ? SDL_GetWindowPixelDensity(win) : 1.f;
    switch (e.type) {
        case SDL_EVENT_QUIT:
            if (dirty() && !quitNow) {
                confirmDiscard([this]() { quitNow = true; });
                return SDL_APP_CONTINUE;
            }
            return SDL_APP_SUCCESS;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            // keep the same world point in the middle of the canvas
            Rect before = rCanvas;
            layout();
            for (auto& c : cam) if (c.valid) c.pan += rCanvas.center() - before.center();
            break;
        }
        case SDL_EVENT_TERMINATING:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_LOW_MEMORY:
            autosave(true);
            break;
        case SDL_EVENT_MOUSE_MOTION:
            in.mouse = Vec2(e.motion.x, e.motion.y) * density;
            in.touch = e.motion.which == SDL_TOUCH_MOUSEID;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            in.mouse = Vec2(e.button.x, e.button.y) * density;
            in.touch = e.button.which == SDL_TOUCH_MOUSEID;
            if (e.button.button == SDL_BUTTON_LEFT) {
                ++in.pressSerial;
                in.down = true;
                in.pressed = true;
                in.pressPos = in.mouse;
                in.pressTime = SDL_GetTicks();
                in.doubleClick = e.button.clicks >= 2;
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                ++in.pressSerial;
                in.rightPressed = true;
                in.rightDown = true;
                in.pressPos = in.mouse;
            } else if (e.button.button == SDL_BUTTON_MIDDLE) {
                ++in.pressSerial;
                in.middleDown = true;
                in.middlePressed = true;
                in.pressPos = in.mouse;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            in.mouse = Vec2(e.button.x, e.button.y) * density;
            if (e.button.button == SDL_BUTTON_LEFT) { in.down = false; in.released = true; }
            else if (e.button.button == SDL_BUTTON_RIGHT) in.rightDown = false;
            else if (e.button.button == SDL_BUTTON_MIDDLE) in.middleDown = false;
            break;
        case SDL_EVENT_MOUSE_WHEEL: {
            float dir = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
            in.wheelX += e.wheel.x * dir;
            in.wheelY += e.wheel.y * dir;
            if (e.wheel.integer_y == 0 && e.wheel.integer_x == 0) in.wheelPrecise = true;
            if (std::fabs(e.wheel.x) > 0) in.wheelPrecise = true;
            in.mouse = Vec2(e.wheel.mouse_x, e.wheel.mouse_y) * density;
            break;
        }
        case SDL_EVENT_PINCH_UPDATE:
            in.pinchScale *= e.pinch.scale;
            in.pinchCenter = in.mouse;
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            Vec2 p(e.tfinger.x * (float)winW, e.tfinger.y * (float)winH);
            std::map<SDL_FingerID, Vec2> before = fingers;
            if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) fingers.erase(e.tfinger.fingerID);
            else fingers[e.tfinger.fingerID] = p;
            if (fingers.size() >= 2 && before.size() >= 2 && e.type == SDL_EVENT_FINGER_MOTION) {
                auto stats = [](const std::map<SDL_FingerID, Vec2>& f, Vec2& c, float& d) {
                    auto it = f.begin();
                    Vec2 a = it->second, b = std::next(it)->second;
                    c = (a + b) / 2;
                    d = dist(a, b);
                };
                Vec2 c0, c1;
                float d0, d1;
                stats(before, c0, d0);
                stats(fingers, c1, d1);
                if (d0 > 1) in.pinchScale *= d1 / d0;
                in.pinchPan += c1 - c0;
                in.pinchCenter = c1;
            }
            in.pinching = fingers.size() >= 2;
            break;
        }
        case SDL_EVENT_TEXT_INPUT:
            in.text += e.text.text;
            break;
        case SDL_EVENT_KEY_DOWN:
            in.keys.push_back(e.key.key);
            in.mods = e.key.mod;
            if (e.key.key == SDLK_SPACE && !ui.editingText()) spaceDown = true;
            if (e.key.key == SDLK_AC_BACK) {  // Android back button
                if (ui.hasMenu()) ui.closeMenus();
                else if (drawerParts || drawerInspector || drawerLayers || drawerAI) drawerParts = drawerInspector = drawerLayers = drawerAI = false;
            }
            break;
        case SDL_EVENT_KEY_UP:
            in.mods = e.key.mod;
            if (e.key.key == SDLK_SPACE) spaceDown = false;
            break;
        case SDL_EVENT_DROP_FILE: {
            std::string path = e.drop.data ? e.drop.data : "";
            if (!path.empty()) runOnMain([this, path]() {
                if (endsWith(path, ".dezpart")) loadFile(path);
                else if (isImagePath(path)) insertImageFile(path);  // a picture lands on the canvas
                else confirmDiscard([this, path]() { loadFile(path); });
            });
            break;
        }
        default:
            break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult App::iterate() {
    layout();
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        work.swap(pending);
    }
    for (auto& f : work) f();
    if (quitNow) return SDL_APP_SUCCESS;

    ensureConn();
    stepVideoExport();  // Export ▸ Video: renders a slice of frames off-screen
    if (simOn) {
        Uint64 now = SDL_GetTicks();
        double dtReal = lastSimTicks ? std::clamp((now - lastSimTicks) / 1000.0, 0.001, 0.06) : 0.016;
        lastSimTicks = now;
        sim = simulator.step(doc, conn, simPressed, dtReal);
        for (auto& kv : sim.serial) {  // Serial Monitor
            std::string& log = serialLog[kv.first];
            log += kv.second;
            if (log.size() > 40000) log.erase(0, log.size() - 30000);
        }
        simRev = doc.revision;
        simPressedLast = simPressed;
    }
    for (int v = 0; v < NUM_VIEWS; v++) if (!cam[v].valid) fitView(v, rCanvas);

    ui.begin();
    if (startScreen && !embedMode) {
        // Nothing else is drawn and no shortcut is handled: the first screen owns
        // the frame until the user opens a project or starts a new one.
        drawStartScreen();
        ui.end();
        g.flush();
        if (!capturePath.empty()) {
            SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr);
            captureOk = shot && SDL_SavePNG(shot, capturePath.c_str());
            if (shot) SDL_DestroySurface(shot);
            capturePath.clear();
        }
        SDL_RenderPresent(ren);
        in.endFrame();
        return SDL_APP_CONTINUE;
    }
    if (!ui.editingText() && !ui.blocking()) { if (readOnly) handleViewerKeys(); else handleShortcuts(); }
    canvasInput();

    SDL_SetRenderDrawColor(ren, 226, 226, 228, 255);
    SDL_RenderClear(ren);
    drawCanvas(view, cam[view], rCanvas, false);
    drawCanvasOverlay();
    if (embedMode) {
        drawEmbedBar();
    } else {
    drawTopBar();
    if (!compact) {
        if (rightTab == 1) drawLayers(rBin);
        else if (rightTab == 2) drawAIPanel(rBin);
        else drawPartsBin(rBin);
        drawInspector(rInspector);
    } else if (drawerParts || drawerInspector || drawerLayers || drawerAI) {
        // drawers slide over the canvas
        g.fillRect(rCanvas, rgba(0, 0, 0, 50));
        g.fillRect({rRight.x - 6 * ui.s, rRight.y, 6 * ui.s, rRight.h}, rgba(0, 0, 0, 25));
        if (drawerParts) drawPartsBin(rRight);
        else if (drawerLayers) drawLayers(rRight);
        else if (drawerAI) drawAIPanel(rRight);
        else drawInspector(rRight);
        if (in.pressed && rCanvas.contains(in.mouse) && !rRight.contains(in.mouse) && !ui.blocking() && binDrag < 0)
            drawerParts = drawerInspector = drawerLayers = drawerAI = false;
    }
    if (codeOpen && rCode.h > 0) drawCodePanel(rCode);
    drawToolbar();
    if (binDrag >= 0) {
        const std::vector<LibEntry>& lib = partLibrary();
        if (binDrag < (int)(lib.size() + customEntries.size())) {
            const LibEntry& e = (size_t)binDrag < lib.size() ? lib[(size_t)binDrag] : customEntries[(size_t)binDrag - lib.size()];
            PartDefPtr def = getPartDef(e.type, e.props);
            float sz = 72 * ui.s;
            Rect r(in.mouse.x - sz / 2, in.mouse.y - sz / 2, sz, sz);
            g.fillRoundRect(r, 8 * ui.s, rgba(255, 255, 255, 200));
            if (def) drawPartIcon(def, r.inset(6 * ui.s));
        }
    }
    }
    ui.end();
    g.flush();
    if (frameTap) {
        if (SDL_Surface* f = SDL_RenderReadPixels(ren, nullptr)) {
            frameTap(f);
            SDL_DestroySurface(f);
        }
    }
    if (!capturePath.empty()) {
        SDL_Surface* shot = SDL_RenderReadPixels(ren, nullptr);
        captureOk = shot && SDL_SavePNG(shot, capturePath.c_str());
        if (shot) SDL_DestroySurface(shot);
        capturePath.clear();
    }
    SDL_RenderPresent(ren);
    autosave(false);
    in.endFrame();
    if (in.released) {}
    return SDL_APP_CONTINUE;
}
