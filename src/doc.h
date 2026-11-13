// designElectronics — document model, .dez file format and connectivity.
#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "anno.h"
#include "geom.h"
#include "image.h"
#include "json.h"
#include "partlib.h"

constexpr const char* kFileFormat = "designElectronics";
constexpr int kFileVersion = 3;  // 2 adds "annos"; 3 adds part "group" (hierarchy levels)
constexpr const char* kFileExt = ".dez";
constexpr const char* kAppVersion = "0.8.2";

struct PartInst {
    int id = 0;
    std::string type;
    Props props;          // only values that differ from / add to the defaults
    std::string label;    // R1, LED2, ...
    Xform place[NUM_VIEWS];
    PartDefPtr def;
    std::string code;     // program running on a microcontroller board (Arduino sketch)
    unsigned hidden = 0;  // bit per view: hidden there (visual only — still connected, simulated, exported)
    bool locked = false;  // can't be moved, rotated or deleted
    int group = 0;        // hierarchy group (0 = none); a group's parts share one level
    bool hiddenIn(int v) const { return (hidden >> v) & 1u; }

    void refresh() { def = getPartDef(type, props); }
    std::string prop(const std::string& k) const {
        auto m = mergedProps(type, props);
        auto it = m.find(k);
        return it == m.end() ? std::string() : it->second;
    }
    // What is printed next to the part on the PCB silkscreen: reference + value, so
    // changing the value in the Inspector changes the ink too.
    std::string silkLabel() const {
        if (!def || def->valueProp.empty() || def->valueProp == "pins" || def->netProp == "net") return label;
        std::string v = prop(def->valueProp);
        return v.empty() ? label : label + "  " + v;
    }
    std::string pinAddress(int pin) const;
    bool inView(int v) const { return def && def->inView(v); }
    Vec2 pinPos(int pin, int view) const { return place[view].apply(def->pins[pin].pos[view]); }
    Rect worldBounds(int view) const { return place[view].applyRect(def->bounds[view]); }
};

struct WireEnd {
    int part = -1;  // part id the end is attached to (-1 = free)
    int pin = -1;   // pin index on that part
    bool attached() const { return part >= 0 && pin >= 0; }
};

enum Layer { TOP = 0, BOTTOM = 1 };

struct Wire {
    int id = 0;
    int view = BB;
    std::vector<Vec2> pts;  // at least two points; ends are pts.front()/pts.back()
    WireEnd ends[2];
    uint32_t color = rgba(65, 140, 225);
    float width = 24;       // PCB trace width (mils); breadboard/schematic use fixed widths
    int layer = TOP;        // PCB only
    bool autorouted = false;
    bool tidy = false;      // laid out by the tidy router: re-tidied when its parts move
};

// Default note text size in world units (the note is drawn at textSize * zoom).
// kNoteTextSize itself lives in anno.h, which notes and annotations share.

struct Note {
    int id = 0;
    int view = BB;
    Vec2 pos;
    Vec2 size{1500, 800};
    std::string text;
    float textSize = kNoteTextSize;
};

struct DesignRules {
    float traceWidth = 24;    // mils
    float clearance = 12;     // mils
    float viaPad = 40, viaDrill = 20;
};

// Where a design is kept on GitHub (empty repo = not linked).
struct GitLink {
    std::string repo;    // "owner/name"
    std::string path;    // file path inside the repo
    std::string branch;  // empty = the repo's default branch
};

class Document {
public:
    std::string uid;         // stable id of the design (local version history follows it)
    GitLink git;
    std::string title = "Untitled";
    std::string author;
    std::string notesText;   // free-form project notes
    int nextId = 1;
    std::vector<PartInst> parts;
    std::vector<Wire> wires;
    std::vector<Note> notes;
    std::vector<Anno> annos;   // markup drawn over the design (never connected)
    // Pictures used by image annotations, keyed by their content hash (imageKey).
    // Immutable in a session: importing the same picture twice stores it once.
    // Saved with the design (only the entries an annotation still points at), but
    // never into an undo snapshot — see Document::toJson.
    std::map<std::string, ImageBlob> images;
    DesignRules rules;
    int revision = 0;        // bumped on every change, used for caches
    int nextGroup = 1;       // hierarchy group ids (0 = not grouped)

    void clear();
    int newId() { return nextId++; }
    int newGroupId() { return nextGroup++; }

    PartInst* part(int id);
    const PartInst* part(int id) const;
    Wire* wire(int id);
    Note* note(int id);
    Anno* anno(int id);
    const Anno* anno(int id) const;
    int partIndex(int id) const;

    // Pictures. addImage() stores the blob under its content key (replacing an
    // identical one) and returns that key; image() is nullptr for unknown keys.
    std::string addImage(const ImageBlob& blob);
    const ImageBlob* image(const std::string& key) const;
    // Drops pictures no annotation refers to. Loading a design calls this (the
    // previous design's pictures would otherwise stay in memory); the editor may
    // also call it after an image annotation is deleted.
    int pruneImages();

    // ---- hierarchy --------------------------------------------------------
    // The z-order is the order of `parts` (back to front). A group is a run of
    // neighbouring parts that move as one level; groups never nest, so a part
    // carries at most one group id.
    int levelKey(int partId) const;                 // group id, or -part id when ungrouped
    std::vector<int> groupMembers(int gid) const;
    bool groupParts(const std::vector<int>& ids);   // false: <2 parts or one is already grouped
    void ungroup(int gid);
    bool moveLevel(int partId, int dir);            // dir +1 = towards the front
    bool canMoveLevel(int partId, int dir) const;
    void normalizeGroups();                         // dissolve groups left with <2 members

    PartInst& addPart(const std::string& type, const Props& props, Vec2 posAllViews);
    void removePart(int id);
    void removeWire(int id);
    void removeNote(int id);
    void removeAnno(int id);
    std::string nextLabel(const std::string& prefix) const;

    // Moves attached wire ends to follow their pins.
    void syncWireEnds();
    void touch() { revision++; }

    // full = include save timestamp and embedded part library (off for undo snapshots)
    // withImages = include the pictures the annotations refer to. Full saves always do;
    // undo snapshots never do (150 of them would hold 150 copies of every photo), while
    // the internal clipboard asks for them without the rest of the "full" form.
    Json toJson(bool full = true, bool withImages = false) const;
    bool fromJson(const Json& j, std::string* err);
    std::string serialize(bool pretty = true) const { return toJson().dump(pretty ? 1 : -1); }
    std::string snapshot() const { return toJson(false).dump(-1); }
    // Smallest self-contained form (for share links): only custom part definitions are embedded,
    // built-in parts are regenerated by any copy of the app.
    std::string compactJson() const;
    bool deserialize(const std::string& text, std::string* err);
};

// ---------------------------------------------------------------------------
// Connectivity: global nets (connections made in any view count everywhere)
// plus per-view groups used for ratsnest lines and routing status.
// ---------------------------------------------------------------------------

struct PinRef {
    int part = -1, pin = -1;
    bool operator==(const PinRef& o) const { return part == o.part && pin == o.pin; }
    bool operator<(const PinRef& o) const { return part < o.part || (part == o.part && pin < o.pin); }
};

struct RatLine {
    Vec2 a, b;
    PinRef pa, pb;
    int net = -1;
};

struct RoutingStatus {
    int nets = 0;            // nets with ≥2 pins in this view
    int routedNets = 0;
    int connectionsToRoute = 0;
};

class Connectivity {
public:
    void build(const Document& doc);

    int numNets() const { return numNets_; }
    int netOf(int partId, int pin) const;
    int wireNet(int wireId) const;
    int viewGroup(int view, int partId, int pin) const;
    const std::vector<PinRef>& netPins(int net) const { return netPins_[net]; }
    std::string netName(int net) const { return net >= 0 && net < (int)netNames_.size() ? netNames_[net] : "?"; }

    // True if the pin is touched by anything in this view (wire end, another pin).
    bool pinConnectedInView(int view, int partId, int pin) const;

    std::vector<RatLine> ratsnest(const Document& doc, int view, RoutingStatus* status = nullptr) const;

private:
    int numNets_ = 0;
    std::unordered_map<int, int> partBase_;       // part id -> first node index
    std::unordered_map<int, int> wireNode_;       // wire id -> node index
    std::vector<int> globalNet_;                  // node -> net id
    std::vector<int> viewRoot_[NUM_VIEWS];        // node -> group root within view
    std::vector<unsigned char> touched_[NUM_VIEWS];
    std::vector<std::vector<PinRef>> netPins_;
    std::vector<std::string> netNames_;
    int node(int partId, int pin) const;
};

std::string readTextFile(const std::string& path, bool* ok = nullptr);
bool writeTextFile(const std::string& path, const std::string& data);
std::string fileBaseName(const std::string& path);
