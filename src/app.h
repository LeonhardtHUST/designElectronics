// designElectronics — application state, interaction and UI composition.
#pragma once
#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "colorcode.h"
#include "doc.h"
#include "gfx.h"
#include "history.h"
#include "keys.h"
#include "tools.h"
#include "ui.h"
#include "wiregrid.h"

// Per-view drawing layers (toggled in the Layers panel; visual only, not saved).
enum CanvasLayer { LY_PARTS, LY_WIRES, LY_LABELS, LY_NOTES, LY_ANNO, LY_RATSNEST, LY_BOARD, LY_COPPER_BOTTOM, LY_COPPER_TOP, LY_PADS, LY_SILK, LY_COUNT };

class App;
// web.cpp — browser glue (no-ops on native builds)
void webRegisterApp(App* app);
void webSaveAndDownload(const std::string& name, const std::function<void(const std::string&)>& write);
void webPickFile(const char* accept);
void webPickImage();  // <input type=file accept="image/*">: hands the bytes to App::insertImageFile

struct Camera {
    Vec2 pan;          // screen position of world origin (pixels)
    float zoom = 0.15f;  // pixels per mil
    bool valid = false;
};

enum DragMode {
    DRAG_NONE,
    DRAG_PENDING,      // pressed, waiting to see if this becomes a drag or a click
    DRAG_PAN,
    DRAG_MOVE,
    DRAG_WIRE_NEW,
    DRAG_WIRE_END,
    DRAG_WIRE_BEND,
    DRAG_RUBBER,
    DRAG_NOTE_RESIZE,
    DRAG_ANNO,         // moving or resizing an annotation (dragAnnoHandle picks which)
    DRAG_BIN,
};

// Active canvas tool. Everything except TOOL_SELECT changes what a drag on the
// canvas means: the shape tools draw markup instead of moving things around.
enum CanvasTool { TOOL_SELECT, TOOL_WIRE, TOOL_RECT, TOOL_LINE, TOOL_CIRCLE, TOOL_TEXT };

// What is under the pointer in the canvas.
struct Hit {
    enum Kind { NONE, PIN, PART, WIRE_END, WIRE_BEND, WIRE_SEG, NOTE, NOTE_CORNER, RATSNEST, ANNO, ANNO_HANDLE } kind = NONE;
    int part = -1, pin = -1, wire = -1, index = -1, note = -1, anno = -1;
    RatLine rat;
};

// One candidate returned by App::collectHits, ranked by priority (small wins) and
// then by how close it is to the pointer on screen.
struct HitCand {
    Hit h;
    int prio = 99;
    float d = 1e30f;  // distance in pixels
};

class App {
public:
    bool init(int argc, char** argv);
    SDL_AppResult event(const SDL_Event& e);
    SDL_AppResult iterate();
    void shutdown();

    // ---- state ----
    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    Gfx g;
    // Textures for the picture annotations: keyed by content key and size bucket, so a
    // zoomed SVG is re-rasterized instead of stretched. Empty (and harmless) without a
    // renderer, which is what the self-test sees.
    ImageCache imageCache;
    UI ui;
    Input in;
    Document doc;
    Connectivity conn;
    int connRev = -1;

    int view = BB;
    Camera cam[NUM_VIEWS];
    std::set<int> selParts, selWires, selNotes, selAnnos;
    CanvasTool tool = TOOL_SELECT;
    int inlineAnnoEdit = -1;
    Anno inlineAnnoOriginal;
    std::string inlineAnnoText;
    bool inlineAnnoCheckpoint = false;
    size_t inlineAnnoUndoDepth = 0;
    std::vector<std::string> inlineAnnoRedo;
    int inlineAnnoRevision = -1;
    void beginInlineAnnoEdit(int id);
    void updateInlineAnnoText(const std::string& text);
    void finishInlineAnnoEdit(bool cancel = false);
    Rect inlineAnnoRect();
    void drawInlineAnnoEditor();
    // Shape being dragged out by a tool: press point, current point, and whether a
    // drag is in progress (a click without a drag still makes a usable shape).
    bool annoDrawing = false;
    Vec2 annoStart, annoNow;
    WireEnd schematicWireStart, schematicWireTarget;
    bool spaceRotatePending = false;
    int spaceRotateDirection = 1;
    // Colour and trace width remembered per view for new wires (persisted in config.toml).
    uint32_t wireColorMem[NUM_VIEWS] = {rgba(65, 140, 225), rgba(65, 140, 225), rgba(65, 140, 225)};
    float traceWidthMem[NUM_VIEWS] = {0, 0, 0};

    // interaction
    DragMode drag = DRAG_NONE;
    Hit pressHit;
    Vec2 pressWorld;
    bool canvasOwnsPointer = false;
    std::map<int, Vec2> moveStart;                 // part id -> start pos
    std::map<int, std::vector<Vec2>> moveWireStart;  // wire id -> start points
    std::map<int, Vec2> moveNoteStart;
    std::map<int, std::pair<Vec2, Vec2>> moveAnnoStart;  // anno id -> (a, b) at drag start
    int dragAnno = -1;                             // annotation being moved or resized
    int dragAnnoHandle = -1;                       // handle index being dragged, -1 = whole shape
    float dragAnnoAspect = 0.f;                    // picture w/h when its handle drag began (Shift keeps it)
    int anchorPart = -1;
    int dragWire = -1, dragWireEnd = -1, dragBend = -1;
    Vec2 rubberA, rubberB;
    Hit hover;
    bool longPressFired = false;
    int binDrag = -1;                // library index being dragged from the bin
    std::map<SDL_FingerID, Vec2> fingers;
    bool spaceDown = false;
    Vec2 lastMouse;
    bool middleDrag = false;   // the drag in progress was started with the middle button
    int wireTargetPart = -1, wireTargetPin = -1;

    // Cache behind wireEndConnected(): mutable because drawing is const.
    mutable WireGrid wireGridCache;
    mutable int wireGridRev = -1, wireGridView = -1, wireGridDrag = -1;
    mutable int wireGridDragWire = -2, wireGridDragEnd = -2, wireGridTarget = -2;

    // keyboard + mouse mapping (keys.cpp, config.toml under shortcut.* / mouse.*)
    Bindings binds;
    int captureChord = -1;   // action waiting for the next key press in the settings sheet
    void loadBindings();
    void saveBindings();
    // Installs a chord. `conflictAction` (from findChord) is cleared first: that is what
    // the settings sheet does once the user confirms the replacement.
    void applyBinding(Action a, const KeyChord& c, int conflictAction = -1);
    void runAction(Action a);
    void addBendAtPointer();   // the AddBend action, at the pointer
    void cycleShapeTool();     // rectangle -> line -> circle -> rectangle
    // Auto resolves to the built-in behaviour of that button; a bound AddBend turns
    // the middle button back into a plain pan, so the two never fight.
    MouseRole mouseRoleFor(int slot) const;
    void openSettingsSheet();  // panels_settings.cpp

    // undo
    std::vector<std::string> undoStack, redoStack;
    std::string editSnapshot;
    bool editOpen = false;

    // file
    std::string filePath;
    int savedRevision = 0;
    Uint64 lastAutosave = 0;
    int autosaveRevision = -1;

    // tools
    bool simOn = false;
    bool simNotesOpen = false;
    Rect simNotesRect;          // where that toggle was drawn (the canvas ignores clicks there)  // canvas overlay: "not simulated" notes expanded
    SimResult sim;
    Simulator simulator;
    Uint64 lastSimTicks = 0;
    int simRev = -1;
    std::set<int> simPressed;
    std::set<int> simPressedLast;
    std::vector<DrcIssue> drcIssues;
    int drcRev = -1;
    int activeLayer = TOP;
    bool showGrid = true;
    bool snapToGrid = true;
    bool autoTidy = true;  // new wires are laid out as tidy right-angle runs
    bool showRatsnest = true;
    bool showPartLabels = true;
    bool layerOff[NUM_VIEWS][LY_COUNT] = {};
    bool layerShown(int v, int l) const { return !layerOff[v][l]; }
    bool partShown(const PartInst& p, int v) const {
        if (!p.def || !p.inView(v) || p.hiddenIn(v)) return false;
        if (v == PCB) return p.def->isBoard ? layerShown(PCB, LY_BOARD) : (layerShown(PCB, LY_PADS) || layerShown(PCB, LY_SILK));
        return layerShown(v, LY_PARTS);
    }
    bool wireShown(const Wire& w) const {
        if (w.view == PCB) return layerShown(PCB, w.layer == TOP ? LY_COPPER_TOP : LY_COPPER_BOTTOM);
        return layerShown(w.view, LY_WIRES);
    }
    // layers panel (panels_layers.cpp)
    int rightTab = 0;            // 0 parts bin, 1 layers
    bool drawerLayers = false;   // compact layout
    std::string layerFilter;
    std::set<int> layerCollapsed;  // collapsed group ids (breadboard part ids; -1 wires, -2 notes)
    Uint64 lastLayerClick = 0;
    int lastLayerClickPart = -1;

    // layout
    float userScale = 1.0f;
    int uiFontSize = 14;
    void loadUIFontSize();
    void setUIFontSize(int points);
    std::string startupLanguage;  // --lang: overrides the stored/system choice for this run
    bool compact = false;
    bool drawerParts = false, drawerInspector = false;
    std::string search;
    int category = -1;
    Rect rTop, rCanvas, rRight, rBottom, rBin, rInspector;
    int winW = 0, winH = 0;

    std::mutex pendingMutex;
    std::vector<std::function<void()>> pending;  // work queued from other threads (file dialogs)
    std::vector<LibEntry> customEntries;     // imported / embedded custom parts shown in the bin
    bool quitNow = false;
    std::function<void(SDL_Surface*)> frameTap;
    bool scratchSession = false;
    bool startScreen = false;        // first launch: pick Open or New before anything else
    bool autosaveAvailable = false;  // an unsaved session can be restored from the welcome screen
    std::function<void(double t)> videoAnimate;
    // --record --zoom-to: world rect to move in on, and how far (0 = whole design, 1 = that rect) at time t
    std::function<float(double t, Rect& focus)> videoZoom;  // --record --animate: change part properties over the clip  // screenshots/recordings: never restore or overwrite the user's autosave  // --record-ui: sees every rendered frame
    std::string capturePath;   // --screenshot: save the next frame before presenting
    bool captureOk = false;

    // ---- app.cpp ----
    void layout();
    void ensureConn();
    bool dirty() const { return doc.revision != savedRevision; }
    void runOnMain(std::function<void()> f);
    void checkpoint();
    void beginEdit();
    void endEdit();
    void undo();
    void redo();
    void changed();
    void pruneSelection();

    void newDoc();
    void drawStartScreen();
    void startScreenNew();
    void startScreenOpen();
    bool restoreAutosave();
    void confirmDiscard(std::function<void()> then);
    bool loadFile(const std::string& path);
    bool saveFile(const std::string& path, const std::string& versionMessage = "");
    void save();
    void saveAs();
    void openDialog();
    void exportFile(const std::string& kind);
    void importPartDialog();
    void showPartModelInfo(const std::string& type);
    void exportPartModelDialog(const std::string& type);
    void deletePartModelConfirm(const std::string& type);
    void loadExample(size_t i);
    void autosave(bool force);
    std::string autosavePath() const;
    void showAbout();
    void showShortcuts();
    // The lines of that dialog, generated from the binding table.
    std::vector<std::string> shortcutLines() const;

    void deleteSelection();
    void rotateSelection(int dir);
    void flipSelection();
    void copySelection(bool cut);
    void paste();
    void duplicate();
    void selectAll();
    void addPart(const LibEntry& e, Vec2 world);
    void addNote();
    void addAnno(AnnoKind kind);  // a new annotation in the middle of the canvas

    // ---- pictures (app.cpp + canvas.cpp) ----
    // Insert a picture: the menu opens a file dialog (a <input type=file> on the web),
    // dropping a file and Ctrl+V call the same two functions underneath.
    void insertImageDialog();
    bool insertImageFile(const std::string& path);                            // read → import → place
    bool insertImageBytes(const std::string& bytes, const std::string& name); // import → place
    bool pasteClipboardImage();  // true when the clipboard held a picture and it was placed
    // One picture annotation: `blob` is stored under its content key, sized to the
    // visible canvas (never wider/higher than ~60% of it) and centred on `centreWorld`
    // (the middle of the canvas when the caller has no better idea).
    Anno* addImageAnno(const ImageBlob& blob);
    // Draws one picture annotation: backdrop, texture (or a placeholder), frame.
    void drawImageAnno(const Anno& a, int v, const Camera& c, float zoom, float uiScale, bool selected, bool hovered);
    void setImageRotation(const std::set<int>& ids, float degrees);  // inspector / handles
    void setPartProp(int partId, const std::string& key, const std::string& value);
    void setPartProps(int partId, const Props& values);  // several properties, one undo step
    void bringToFront(bool front);
    void runAutoroute();
    void tidyCommand();
    void retidyAround(const std::set<int>& parts, const std::set<int>& skipWires = {});
    void runDrc();
    void toggleSim();
    void restartSimulation();
    void openSimulationDetails();
    void setView(int v);
    void setLanguage(const std::string& code);
    void handleShortcuts();

    // ---- panels_code.cpp: a board's program (Arduino sketch) ----
    bool codeOpen = false;
    int codePart = -1;
    Rect rCode;
    CodeEditorState codeState;
    bool codeEditing = false;
    Uint64 lastCodeEdit = 0;
    int codeCheckRev = -1, codeCheckLine = 0;
    std::string codeCheckMsg;
    std::map<int, std::string> serialLog;  // per board
    std::string serialSend;
    void openCode(int partId);
    void drawCodePanel(Rect r);
    std::string defaultSketch(const PartInst& p);
    void exportSketch(int partId);
    void showSaveDialog(const std::string& name, const SDL_DialogFileFilter* filters, int n, std::function<void(const std::string&)> onPath);
    // ---- upload.cpp: real boards via arduino-cli ----
    struct UploadJob;
    std::shared_ptr<UploadJob> uploadJob;
    int uploadPart = -1;
    std::string arduinoCli, uploadFqbn, uploadPort, uploadStatus, uploadResult;
    bool uploadResultOk = true;
    std::vector<std::string> uploadPorts;
    void openUploadSheet(int partId);
    void runCli(const std::vector<std::string>& args, const std::string& what, std::function<void(int, const std::string&)> done);
    void refreshPorts();
    void installCore();
    void startUpload(bool compileOnly = false);

    // ---- panels_video.cpp: Export ▸ Video or GIF ----
    struct VideoJob;
    std::shared_ptr<VideoJob> videoJob;
    int videoPreset = 0, videoView = 0, videoSeconds = 10;
    bool videoGif = false, videoSimulate = true, videoWatermark = true;
    std::string videoTitle, lastVideoError;
    void openVideoSheet();
    void startVideo(const std::string& path);
    bool renderVideoFrame();
    void stepVideoExport();
    ~App();

    // ---- agent.cpp + panels_ai.cpp: the AI tab (Claude Code & other CLI agents) ----
    struct ChatMsg {
        enum Role { R_USER, R_ASSISTANT, R_TOOL, R_TOOL_ERROR, R_ERROR, R_INFO } role;
        std::string text;
        bool streaming = false;
    };
    struct AgentState;
    std::shared_ptr<AgentState> agent;
    std::string agentKind = "claude", agentCustomCommand, agentInput;
    bool drawerAI = false;
    size_t agentSeen = 0;  // messages already scrolled to
    bool agentWasRunning = false;
    int agentStartRev = 0;
    bool agentsSupported() const;
    void detectAgents();
    void ensureAgentBridge();
    void startAgent(const std::string& prompt);
    void stopAgent();
    void newAgentChat();
    std::vector<ChatMsg> agentMessages(bool& running, bool& detected, std::map<std::string, std::string>& paths);
    std::string standaloneMcpConfig() const;
    void drawAIPanel(Rect r);
    void drawRightTabs(Rect r, float y);

    // ---- web.cpp: browser build + embeddable viewer ----
    bool embedMode = false;      // <iframe> viewer: canvas + small bar only
    bool readOnly = false;       // no editing (pan, zoom, simulate, press buttons)
    std::string editorUrl;       // "Open in designElectronics" target (set by the web page)
    void drawEmbedBar();
    void handleViewerKeys();
    void openEmbedSheet();       // Share ▸ Embed in a website…
    std::string shareFragment(); // "#d=…" with the design packed into the link

    // ---- app_history.cpp: version history + GitHub ----
    GitHubSettings gh;
    bool ghBusy = false;
    std::string ghStatus, ghCommitsErr, lastCommitUrl;
    bool ghStatusOk = false;  // ghStatus is a success message (green), not an error
    bool ghStatusIsCommit = false;  // …and it came from a push, so "View on GitHub" applies
    std::vector<GitCommit> ghCommits;
    bool ghCommitsLoaded = false;
    int historyTab = 0;
    std::string commitMsg, commitRepo, commitPath, tokenInput, repoInput;
    bool githubConnected() const;
    void loadGitHubSettings();
    void recordVersion(const std::string& data, const std::string& message);
    void pushToGitHub(const std::string& message);
    void refreshGitHubHistory();
    void restoreVersionText(const std::string& text, const std::string& label);
    void openVersionCopy(const std::string& text, const std::string& label);
    void fetchGitHubVersion(const GitCommit& c, bool asCopy);
    void openHistory();
    void openCommitSheet();
    void openGitHubAccount();

    // ---- canvas_anno.cpp: markup tools and the remembered wire colour ----
    void setTool(CanvasTool t);
    void cancelAnnoTool();                       // back to selection, dropping an unfinished shape
    Vec2 schematicWirePoint(Vec2 screen, WireEnd& end);
    bool commitSchematicWire();
    bool annoToolInput();                        // true when the tool consumed the pointer
    bool annoToolActive() const { return tool != TOOL_SELECT; }
    void drawAnnoPreview(const Camera& c, float zoom, float uiScale);
    void drawToolHint();                         // one-line reminder of what to do next
    void createAnnoFromDrag(CanvasTool t, Vec2 a, Vec2 b);
    void loadWirePrefs();
    void saveWirePrefs(int v);
    // The only way a wire colour may change: it also becomes the colour of new wires.
    void setWireColor(int v, int wireId, uint32_t c);
    void setWireColor(int v, const std::set<int>& wireIds, uint32_t c);
    // Remember the width of a hand-picked trace (PCB).
    void rememberTraceWidth(int v, float w);

    // ---- canvas.cpp ----
    Vec2 toWorld(Vec2 s) const { return (s - cam[view].pan) / cam[view].zoom; }
    Vec2 toScreen(Vec2 w) const { return cam[view].pan + w * cam[view].zoom; }
    float gridSize(int v) const { return v == PCB ? 50.f : 100.f; }
    void fitView(int v, Rect viewport);
    void zoomAt(float factor, Vec2 screen);
    // Every candidate under the pointer, ranked by layer priority and screen distance.
    void collectHits(Vec2 screen, std::vector<HitCand>& out);
    // Re-measures every point text in `v` so its box (and hit box, and selection frame)
    // follows the glyphs. Called once per frame before input handling.
    void fitPointTexts(int v);
    // The winner of collectHits(); the public shape of "what did I click".
    Hit hitTest(Vec2 screen);
    // Screen-space minimum (6 px, 14 on touch) folded into world units, never
    // thinner than the shape itself.
    float hitRadius(int v, float visualHalfWorld) const;
    bool pinAt(Vec2 screen, int& part, int& pin, int excludePart, int excludePin);
    void startDrag();
    // Middle button on the canvas: add a bend point on a wire segment, remove one on a
    // bend point, otherwise pan. Returns true when the canvas should start panning.
    bool middleButtonPress(Vec2 screen);
    // Inserts a bend point on the segment named by `h`, at `world`, and selects that
    // wire. Returns false when the point would land on an existing end or bend.
    bool insertBendAt(const Hit& h, Vec2 world);
    void finishPointer();
    void drawPrims(const PartInst& p, int v, const Camera& c, bool forIcon = false);
    void canvasInput();
    void drawCanvas(int v, const Camera& c, Rect viewport, bool exporting);
    // Is wire end `end` really connected? Structure plus geometry: a snap target only
    // becomes an attachment when the drag ends, so the red "unconnected" dot must
    // follow the geometry, not only ends[].
    bool wireEndConnected(const Wire& w, int end) const;
    // World tolerance for "these two wire points are the same node", per view.
    float wireJoinTol(int v) const;
    // The one view's wires as a grid, rebuilt only when the drawing has changed, so
    // drawing a wire is not a scan over every other wire.
    const WireGrid& wireProximity(int v, float tol) const;
    void drawOledFrame(const ard::OledFrame& f, const Prim& screen, const Xform& xf, const Camera& c);
    void drawPartIcon(const PartDefPtr& def, Rect r);
    std::vector<MenuItem> contextMenu(const Hit& h);
    bool renderViewPng(int v, const std::string& path, std::string* msg);

    // ---- panels_tools.cpp: the Tools collection (menu, start page) ----
    // Tools are small stand-alone utilities. Each opens as a sheet, so they work from the
    // start page (before any project exists) and over a design alike.
    struct ToolEntry {
 const char* id;       // stable id; i18n keys are tool.<id>.name / tool.<id>.desc
 const char* icon;
        void (App::*open)();
    };
    static const std::vector<ToolEntry>& toolList();
    std::vector<MenuItem> toolsMenu();
    void openColorCodeTool();   // Tools ▸ Resistor color code
    cc::Spec toolCalc;   // the stand-alone calculator remembers its last resistor
    int toolCalcBand = 0;
    int inspBandSel = 0; // selected band of the Inspector's calculator
    // The calculator itself: bands drawn on a resistor, a palette, the value fields.
    // Lays out from (x, y) downwards, advances y, returns true when `spec` changed.
    bool colorCalcWidget(const std::string& id, float x, float& y, float w, cc::Spec& spec, int& sel);

    // ---- panels.cpp ----
    void drawTopBar();
    void drawPartsBin(Rect r);
    void drawInspector(Rect r);
    void drawLayers(Rect r);
    void setPartHidden(int partId, int v, bool hidden);
    void setPartLocked(int partId, bool locked);
    void zoomToPart(int partId);
    void drawToolbar();
    void drawCanvasOverlay();
    std::vector<MenuItem> fileMenu();
    std::vector<MenuItem> editMenu();
    std::vector<MenuItem> partMenu();
    std::vector<MenuItem> viewMenu();
    std::vector<MenuItem> routingMenu();
    std::vector<MenuItem> helpMenu();   // (toolsMenu is declared with the tools above)
    std::vector<MenuItem> allMenus();
};
