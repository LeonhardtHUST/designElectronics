// designElectronics — the rebindable action table (see keys.h).
#include "keys.h"

#include <SDL3/SDL_keycode.h>

#include <cctype>
#include <cstring>
#include <string>

#include "i18n.h"

namespace {

struct NamedKey {
    const char* id;     // stored in config.toml
    int key;            // SDL keycode
    const char* label;  // shown in the interface
};

const NamedKey kNamed[] = {
    {"escape", SDLK_ESCAPE, "Esc"},      {"delete", SDLK_DELETE, "Del"},       {"backspace", SDLK_BACKSPACE, "Backspace"},
    {"enter", SDLK_RETURN, "Enter"},     {"space", SDLK_SPACE, "Space"},       {"tab", SDLK_TAB, "Tab"},
    {"left", SDLK_LEFT, "\u2190"},       {"right", SDLK_RIGHT, "\u2192"},      {"up", SDLK_UP, "\u2191"},
    {"down", SDLK_DOWN, "\u2193"},       {"home", SDLK_HOME, "Home"},          {"end", SDLK_END, "End"},
    {"pageup", SDLK_PAGEUP, "PgUp"},     {"pagedown", SDLK_PAGEDOWN, "PgDn"},   {"equals", SDLK_EQUALS, "="},
    {"plus", SDLK_PLUS, "+"},            {"minus", SDLK_MINUS, "-"},           {"comma", SDLK_COMMA, ","},
    {"period", SDLK_PERIOD, "."},        {"f1", SDLK_F1, "F1"},                {"f2", SDLK_F2, "F2"},
    {"f3", SDLK_F3, "F3"},               {"f4", SDLK_F4, "F4"},                {"f5", SDLK_F5, "F5"},
    {"f6", SDLK_F6, "F6"},               {"f7", SDLK_F7, "F7"},                {"f8", SDLK_F8, "F8"},
    {"f9", SDLK_F9, "F9"},               {"f10", SDLK_F10, "F10"},             {"f11", SDLK_F11, "F11"},
    {"f12", SDLK_F12, "F12"},
};

const NamedKey* namedByKey(int key) {
    for (auto& n : kNamed)
        if (n.key == key) return &n;
    return nullptr;
}

const NamedKey* namedById(const std::string& id) {
    for (auto& n : kNamed)
        if (id == n.id) return &n;
    return nullptr;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// "r" -> SDLK_r, "7" -> SDLK_7, "f5"/"escape" -> named key.
int keyFromToken(const std::string& token) {
    if (token.empty()) return 0;
    if (const NamedKey* n = namedById(token)) return n->key;
    // The name the interface shows ("Del", "Esc", "←") parses too, so the text in the
    // shortcuts dialog can be pasted back into config.toml.
    for (auto& n : kNamed)
        if (lower(n.label) == token) return n.key;
    if (token.size() == 1) {
        char c = token[0];
        if (c >= 'a' && c <= 'z') return SDLK_A + (c - 'a');
        if (c >= '0' && c <= '9') return SDLK_0 + (c - '0');
    }
    return 0;
}

std::string labelForKey(int key) {
    if (const NamedKey* n = namedByKey(key)) return n->label;
    if (key >= SDLK_A && key <= SDLK_Z) return std::string(1, (char)std::toupper(key));
    if (key >= SDLK_0 && key <= SDLK_9) return std::string(1, (char)key);
    return "?";
}

std::string tokenForKey(int key) {
    if (const NamedKey* n = namedByKey(key)) return n->id;
    if (key >= SDLK_A && key <= SDLK_Z) return std::string(1, (char)std::tolower(key));
    if (key >= SDLK_0 && key <= SDLK_9) return std::string(1, (char)key);
    return std::string();
}

}  // namespace

Bindings defaultBindings() {
    Bindings b;
    auto set = [&b](Action a, int key, bool ctrl = false, bool shift = false, bool alt = false) {
        b.chord[(int)a] = KeyChord{key, ctrl, shift, alt};
    };
    set(Action::Undo, SDLK_Z, true);
    set(Action::Redo, SDLK_Z, true, true);
    set(Action::Delete, SDLK_DELETE);
    set(Action::Copy, SDLK_C, true);
    set(Action::Paste, SDLK_V, true);
    set(Action::Rotate, SDLK_R);  // Shift+R turns the other way, see actionUsesShift
    set(Action::Flip, SDLK_F);
    // Adding a bend point starts on the middle button (see Bindings::mouse), so no key.
    set(Action::AddBend, 0);
    set(Action::ViewBB, SDLK_1, true);
    set(Action::ViewSCH, SDLK_2, true);
    set(Action::ViewPCB, SDLK_3, true);
    set(Action::ToolSelect, SDLK_ESCAPE);  // leaves a shape tool, otherwise clears the selection
    set(Action::ToolWire, SDLK_W, false, false, true);
    set(Action::ToolShape, 0);
    set(Action::ToolText, 0);
    set(Action::Save, SDLK_S, true);  // Shift+S saves as, see actionUsesShift
    b.mouse[SLOT_MIDDLE] = MouseRole::Auto;
    b.mouse[SLOT_RIGHT] = MouseRole::Auto;
    b.mouse[SLOT_SPACE_LEFT] = MouseRole::Auto;
    return b;
}

bool chordMatches(const KeyChord& c, const Input& in) {
    if (!c.bound()) return false;
    if (c.ctrl != in.cmd()) return false;
    if (c.shift != in.shift()) return false;
    if (c.alt != ((in.mods & SDL_KMOD_ALT) != 0)) return false;
    if (in.key(c.key)) return true;
    // Delete and Backspace are the one "delete" key on most keyboards (and the other
    // way round on a Mac); binding either should accept both.
    if (c.key == SDLK_DELETE) return in.key(SDLK_BACKSPACE);
    if (c.key == SDLK_BACKSPACE) return in.key(SDLK_DELETE);
    return false;
}

bool actionUsesShift(Action a) { return a == Action::Rotate || a == Action::Save; }

int actionFor(const Bindings& b, const Input& in) {
    for (int i = 0; i < kActionCount; i++)
        if (chordMatches(b.chord[i], in)) return i;
    // Shift-tolerant second pass, for the actions that treat Shift as a variant of
    // themselves (rotate the other way, save as). Everything else stays exact, so
    // Ctrl+Shift+Z is not mistaken for Ctrl+Z.
    for (int i = 0; i < kActionCount; i++) {
        const KeyChord& c = b.chord[i];
        if (!c.bound() || !actionUsesShift((Action)i)) continue;
        if (c.ctrl != in.cmd() || c.alt != ((in.mods & SDL_KMOD_ALT) != 0)) continue;
        if (in.key(c.key)) return i;
    }
    return -1;
}

std::string chordToText(const KeyChord& c) {
    if (!c.bound()) return tr("keys.unbound");
    std::string s;
    if (c.ctrl) s += "Ctrl+";
    if (c.shift) s += "Shift+";
    if (c.alt) s += "Alt+";
    s += labelForKey(c.key);
    return s;
}

bool chordFromText(const std::string& text, KeyChord* out) {
    if (!out) return false;
    KeyChord c;
    std::string t = lower(text);
    if (t.empty() || t == "none" || t == "unbound") {
        *out = c;
        return true;
    }
    size_t pos = 0;
    while (pos <= t.size()) {
        size_t plus = t.find('+', pos);
        std::string tok = t.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos);
        if (tok.empty()) return false;
        bool last = plus == std::string::npos;
        if (!last) {
            if (tok == "ctrl" || tok == "control" || tok == "cmd" || tok == "command" || tok == "meta") c.ctrl = true;
            else if (tok == "shift") c.shift = true;
            else if (tok == "alt" || tok == "option" || tok == "opt") c.alt = true;
            else return false;  // an unknown modifier
            pos = plus + 1;
            continue;
        }
        int key = keyFromToken(tok);
        if (!key) return false;
        c.key = key;
        *out = c;
        return true;
    }
    return false;
}

std::string chordToToken(const KeyChord& c) {
    if (!c.bound()) return std::string();
    std::string s;
    if (c.ctrl) s += "ctrl+";
    if (c.shift) s += "shift+";
    if (c.alt) s += "alt+";
    std::string key = tokenForKey(c.key);
    return key.empty() ? std::string() : s + key;
}

const char* actionId(Action a) {
    switch (a) {
        case Action::Undo: return "undo";
        case Action::Redo: return "redo";
        case Action::Delete: return "delete";
        case Action::Copy: return "copy";
        case Action::Paste: return "paste";
        case Action::Rotate: return "rotate";
        case Action::Flip: return "flip";
        case Action::AddBend: return "add_bend";
        case Action::ViewBB: return "view_bb";
        case Action::ViewSCH: return "view_sch";
        case Action::ViewPCB: return "view_pcb";
        case Action::ToolSelect: return "tool_select";
        case Action::ToolWire: return "tool_wire";
        case Action::ToolShape: return "tool_shape";
        case Action::ToolText: return "tool_text";
        case Action::Save: return "save";
        case Action::kCount: break;
    }
    return "?";
}

std::string actionLabel(Action a) { return tr(std::string("action.") + actionId(a)); }

bool chordAllowed(Action a, const KeyChord& c) {
    if (!c.bound()) return true;  // unbinding is always allowed
    if (a != Action::Delete) return true;
    if (c.ctrl || c.alt) return true;
    // Delete/Backspace and the function keys are safe on their own; a bare letter or
    // digit is not, because that is what a bare mouse button would be.
    if (c.key == SDLK_DELETE || c.key == SDLK_BACKSPACE) return true;
    if (c.key >= SDLK_F1 && c.key <= SDLK_F12) return true;
    return false;
}

int findChord(const Bindings& b, const KeyChord& c, int skip) {
    if (!c.bound()) return -1;
    for (int i = 0; i < kActionCount; i++) {
        if (i == skip) continue;
        if (b.chord[i] == c) return i;
    }
    return -1;
}

const char* mouseRoleId(MouseRole r) {
    switch (r) {
        case MouseRole::Auto: return "auto";
        case MouseRole::Pan: return "pan";
        case MouseRole::AddBend: return "add_bend";
        case MouseRole::ContextMenu: return "context_menu";
        case MouseRole::RubberBand: return "rubber_band";
        case MouseRole::None: return "none";
    }
    return "auto";
}

bool mouseRoleFromId(const std::string& id, MouseRole* out) {
    if (!out) return false;
    for (int i = 0; i < kMouseRoleCount; i++) {
        MouseRole r = (MouseRole)i;
        if (id == mouseRoleId(r)) {
            *out = r;
            return true;
        }
    }
    return false;
}

std::string mouseRoleLabel(MouseRole r) { return tr(std::string("mouse.") + mouseRoleId(r)); }
