// designElectronics — the rebindable keyboard actions and mouse roles.
//
// One table drives four things: the shortcut handler, the "Keyboard & touch
// shortcuts" dialog, the pointer roles on the canvas, and config.toml. Adding an
// action means adding a name here, a default in defaultBindings(), a case in
// App::runAction() and a translation for "action.<id>".
#pragma once
#include <string>

#include "ui.h"

enum class Action : int {
    Undo,
    Redo,
    Delete,
    Copy,
    Paste,
    Rotate,
    Flip,
    AddBend,
    ViewBB,
    ViewSCH,
    ViewPCB,
    ToolSelect,
    ToolWire,
    ToolShape,
    ToolText,
    Save,
    kCount
};
inline constexpr int kActionCount = (int)Action::kCount;

// What a mouse button does on the canvas. Auto is the built-in contextual behaviour.
enum class MouseRole : int { Auto, Pan, AddBend, ContextMenu, RubberBand, None };
inline constexpr int kMouseRoleCount = 6;

// Which pointer the role belongs to.
enum MouseSlotIndex { SLOT_MIDDLE = 0, SLOT_RIGHT = 1, SLOT_SPACE_LEFT = 2, kMouseSlots = 3 };

struct KeyChord {
    int key = 0;  // SDL keycode; 0 = not bound
    bool ctrl = false, shift = false, alt = false;
    bool bound() const { return key != 0; }
    bool operator==(const KeyChord& o) const {
        return key == o.key && ctrl == o.ctrl && shift == o.shift && alt == o.alt;
    }
    bool operator!=(const KeyChord& o) const { return !(*this == o); }
};

struct Bindings {
    KeyChord chord[kActionCount];
    MouseRole mouse[kMouseSlots] = {MouseRole::Auto, MouseRole::Auto, MouseRole::Auto};
};

Bindings defaultBindings();

// True when this chord is what the user just pressed (modifiers must match exactly).
bool chordMatches(const KeyChord& c, const Input& in);
// The action a frame's keys trigger, or -1. Actions whose handler reads Shift itself
// (rotate, save as) also match with Shift held.
int actionFor(const Bindings& b, const Input& in);

// "Ctrl+Shift+R" for the interface, or the translated "not bound". Modifier and key
// names stay ASCII: they are the same in every language, and the settings file stores
// exactly this spelling.
std::string chordToText(const KeyChord& c);
// "ctrl+shift+r", "escape", "none" or "" (clears it). Case-insensitive.
bool chordFromText(const std::string& text, KeyChord* out);
// The canonical spelling stored in config.toml ("ctrl+shift+r", "" when unbound).
std::string chordToToken(const KeyChord& c);

// "undo", "add_bend", … — the key suffix in config.toml and in the translation keys.
const char* actionId(Action a);
std::string actionLabel(Action a);   // translated
bool actionUsesShift(Action a);
// A destructive action needs a modifier (or a dedicated key such as Delete): a bare
// letter must not delete anything, just as a bare mouse button must not.
bool chordAllowed(Action a, const KeyChord& c);
// Which action already uses this chord (-1 = free), ignoring index `skip`.
int findChord(const Bindings& b, const KeyChord& c, int skip = -1);

const char* mouseRoleId(MouseRole r);
bool mouseRoleFromId(const std::string& id, MouseRole* out);
std::string mouseRoleLabel(MouseRole r);  // translated
