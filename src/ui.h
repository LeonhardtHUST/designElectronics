// designElectronics — small immediate-mode UI toolkit (buttons, fields, menus,
// dialogs, scrolling) drawn with Gfx. Works with mouse, touch and keyboard.
#pragma once
#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx.h"

struct Input {
    Vec2 mouse;              // pixels
    bool down = false;       // primary button / finger held
    bool pressed = false;    // went down this frame
    bool released = false;   // went up this frame
    bool rightPressed = false;
    bool middleDown = false, rightDown = false;
    bool middlePressed = false;  // middle button went down this frame
    bool doubleClick = false;
    float wheelX = 0, wheelY = 0;
    bool touch = false;      // last pointer event came from a touchscreen
    bool wheelPrecise = false;  // trackpad-style smooth scrolling
    std::string text;        // UTF-8 typed this frame
    std::vector<SDL_Keycode> keys;
    SDL_Keymod mods = 0;
    Vec2 pressPos;
    uint32_t pressSerial = 0;
    Uint64 pressTime = 0;
    // pinch gesture (two fingers)
    bool pinching = false;
    float pinchScale = 1;    // multiplicative zoom since last frame
    Vec2 pinchCenter, pinchPan;

    bool key(SDL_Keycode k) const { for (auto x : keys) if (x == k) return true; return false; }
    bool cmd() const { return (mods & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0; }
    bool shift() const { return (mods & SDL_KMOD_SHIFT) != 0; }
    void endFrame() {
        pressed = released = rightPressed = doubleClick = middlePressed = false;
        wheelX = wheelY = 0;
        wheelPrecise = false;
        text.clear();
        keys.clear();
        pinchScale = 1;
        pinchPan = {0, 0};
    }
};

struct Theme {
    uint32_t bg = rgba(226, 226, 228);
    uint32_t panel = rgba(246, 246, 247);
    uint32_t panel2 = rgba(236, 236, 238);
    uint32_t border = rgba(204, 204, 208);
    uint32_t text = rgba(38, 38, 42);
    uint32_t textDim = rgba(112, 112, 120);
    uint32_t accent = rgba(28, 116, 222);
    uint32_t accentText = rgba(255, 255, 255);
    uint32_t button = rgba(255, 255, 255);
    uint32_t buttonHover = rgba(234, 240, 250);
    uint32_t buttonDown = rgba(214, 226, 246);
    uint32_t field = rgba(255, 255, 255);
    uint32_t danger = rgba(208, 52, 52);
    uint32_t ok = rgba(40, 160, 80);
    uint32_t brand = rgba(196, 40, 52);
};

struct MenuItem {
    std::string label, shortcut;
    std::function<void()> action;
    std::vector<MenuItem> children;
    bool enabled = true, checked = false, separator = false;
    static MenuItem sep() { MenuItem m; m.separator = true; return m; }
};

struct DialogButton {
    std::string label;
    std::function<void()> action;
    bool primary = false;
};

// State of a code editor (owned by the caller, one per edited text).
struct CodeEditorState {
    size_t cursor = 0, anchor = 0;
    float scrollX = 0, scrollY = 0;
    int desiredCol = -1;
    bool dragging = false;
    bool followExec = true;  // scroll to the executing line while not editing
    Uint64 lastEdit = 0;
    std::vector<std::pair<std::string, size_t>> undo, redo;
};

// Platform text editing. The web build sets these to overlay a real <input>/<textarea> on the
// focused field, which brings the phone's keyboard, cursor handles and cut/copy/paste.
struct NativeTextEdit {
    void (*begin)(Rect r, const std::string& text, bool multiline, float px, bool selectAll) = nullptr;
    void (*place)(Rect r) = nullptr;               // keep it over the field (sheets scroll, layouts change)
    void (*setText)(const std::string& text) = nullptr;
    int (*poll)(std::string& text) = nullptr;      // current text; returns 0 editing, 1 Enter, 2 Escape, 3 lost focus
    void (*end)() = nullptr;
    // This frame's fields, so a tap can focus the overlay inside the touch handler (iOS only opens
    // the keyboard from there). `box` is where the text goes, `hit` the whole field.
    struct Field { Rect hit, box; bool multiline; };
    void (*fields)(const std::vector<Field>& fields) = nullptr;
};

enum ButtonStyle { BTN_NORMAL, BTN_FLAT, BTN_TAB, BTN_PRIMARY, BTN_TOOL, BTN_CHIP };

class UI {
public:
    Gfx* g = nullptr;
    Input* in = nullptr;
    SDL_Window* window = nullptr;
    Theme th;
    float s = 1;           // ui scale (pixels per point)
    NativeTextEdit native;

    void begin();
    void end();            // draws popups, dialogs, toasts, tooltip

    // Is the pointer free for this widget (not blocked by a popup/dialog/capture)?
    bool interactive(Rect r) const;
    // The single rect-hit test every widget uses. While a dialog is up nothing behind
    // it is hot, even for the code paths that read the pointer directly.
    bool hotRect(Rect r) const;
    bool hovered(Rect r) const { return hotRect(r); }
    bool blocking() const { return !menus_.empty() || !dialogs_.empty() || (!sheets_.empty() && !inSheet_); }
    bool hasDialog() const { return !dialogs_.empty(); }
    bool overBlocking(Vec2 p) const;
    bool editingText() const { return focus_ != 0; }

    // widgets
    bool button(const std::string& id, Rect r, const std::string& label, ButtonStyle st = BTN_NORMAL, bool selected = false, bool enabled = true);
    bool iconButton(const std::string& id, Rect r, const std::string& icon, const std::string& label, bool selected = false, bool enabled = true, const std::string& tip = "");
    bool textField(const std::string& id, Rect r, std::string& value, const std::string& placeholder = "", bool live = false);
    // Multi-line, word-wrapping box that edits `value` live. Returns true when Enter is pressed (Shift+Enter = new line).
    // `padRight` keeps text clear of buttons drawn inside the box.
    bool textArea(const std::string& id, Rect r, std::string& value, const std::string& placeholder = "", float padRight = 0);
    // `display` maps an option value to the text shown to the user. Values are data
    // (they are saved in .dez and matched by the simulator), so they are never
    // translated in place — only what is drawn.
    bool comboField(const std::string& id, Rect r, std::string& value, const std::vector<std::string>& options, bool editable,
                    const std::function<std::string(const std::string&)>& display = {});
    bool checkbox(const std::string& id, Rect r, const std::string& label, bool& v);
    // Multi-line code editor (code_editor.cpp). Returns true when `text` changed.
    bool codeEditor(const std::string& id, Rect r, std::string& text, CodeEditorState& st, int execLine = 0, int errorLine = 0,
                    const std::string& errorMsg = "");
    void label(Rect r, const std::string& t, uint32_t col, float px, int align = 0, FontFace face = FONT_UI);
    float wrappedText(Rect r, const std::string& t, uint32_t col, float px, bool draw = true);
    void tooltip(Rect r, const std::string& tip);
    void drawIcon(const std::string& name, Rect r, uint32_t col);

    // scroll region: content drawn between begin/end is clipped; returns content origin offset
    float beginScroll(const std::string& id, Rect r, float contentH);
    void endScroll();
    void scrollTo(const std::string& id, float y) { scrollY_[hashId(id)] = y; }  // clamped on the next beginScroll
    bool scrollDragging() const { return scrollDrag_; }

    // overlays
    void openMenu(Rect anchor, std::vector<MenuItem> items, bool above = false);
    void closeMenus() { menus_.clear(); }
    void dialog(const std::string& title, const std::vector<std::string>& lines, std::vector<DialogButton> buttons);
    void toast(const std::string& msg, float seconds = 3.0f);
    // Modal sheet: a titled panel drawn over everything (below menus/dialogs). `draw`
    // is called every frame with the body rect and lays out its own widgets.
    void openSheet(const std::string& id, const std::string& title, float w, float h, std::function<void(Rect)> draw);
    void closeSheet() { sheets_.clear(); }
    bool sheetOpen(const std::string& id = "") const { return !sheets_.empty() && (id.empty() || sheets_.back().id == id); }
    void clearFocus();
    void focusField(const std::string& id, bool selectAll);
    void cancelPendingFocus() { pendingFocus_ = 0; pendingSelectAll_ = false; }
    bool fieldFocused(const std::string& id) const;

    bool hasMenu() const { return !menus_.empty(); }
    Rect menuBoundsForTest() const { return menus_.empty() ? Rect{} : menus_.front().rect; }
    float menuScrollForTest() const { return menus_.empty() ? 0.f : menus_.front().scroll; }
    float menuMaxScrollForTest() const;

private:
    struct Menu { Rect rect; std::vector<MenuItem> items; int open = -1; float scroll = 0; uint32_t openSerial = 0; };
    struct Dialog { std::string title; std::vector<std::string> lines; std::vector<DialogButton> buttons; };
    struct Sheet { std::string id, title; float w, h; std::function<void(Rect)> draw; };
    std::vector<Sheet> sheets_;
    bool inSheet_ = false;
    bool inDialogs_ = false;  // true while drawDialogs() draws: the dialog's own widgets work
    void drawSheet();
    std::vector<Menu> menus_;
    std::vector<Dialog> dialogs_;
    std::string toastMsg_;
    Uint64 toastUntil_ = 0;
    std::string tip_;
    Rect tipRect_;
    Uint64 tipStart_ = 0;
    std::string tipId_;

    size_t focus_ = 0;
    std::string editBuf_;
    size_t caret_ = 0, anchor_ = 0;
    size_t pendingFocus_ = 0;
    bool pendingSelectAll_ = false, fieldDragging_ = false;
    float fieldScrollX_ = 0;
    int fieldFirstLine_ = 0;
    bool eraseFieldSelection();
    void fieldPointer(size_t position);
    bool fieldKey(SDL_Keycode key, bool multiline);
    bool nativeOn_ = false, focusSeen_ = false;  // focused field is edited by `native`; drawn this frame
    std::vector<NativeTextEdit::Field> fieldRects_;
    void startEditing(Rect r, Rect box, bool multiline, float px);
    size_t pressedId_ = 0;
    bool menuJustOpened_ = false;
    std::string pendingCombo_;
    size_t pendingComboId_ = 0;

    struct Scroll { size_t id; Rect r; float* y; float contentH; };
    std::vector<Scroll> scrollStack_;
    std::unordered_map<size_t, float> scrollY_;
    size_t scrollDragId_ = 0;
    bool scrollDrag_ = false;
    float scrollDragStart_ = 0;
    Rect clipStack_;
    bool clipping_ = false;

    static size_t hashId(const std::string& s) { return std::hash<std::string>{}(s) | 1; }
    void drawMenus();
    void drawDialogs();
    Rect menuRect(Rect anchor, const std::vector<MenuItem>& items, bool above, bool side) const;
};

std::vector<std::string> wrapText(Gfx& g, const std::string& t, float px, float width, FontFace face = FONT_UI);

struct TooltipLayout {
    Rect bounds;
    std::vector<std::string> lines;
    float fontSize = 0, lineHeight = 0, padding = 0;
};
TooltipLayout layoutTooltip(Gfx& g, const std::string& text, Vec2 pointer, Rect viewport, float scale);
