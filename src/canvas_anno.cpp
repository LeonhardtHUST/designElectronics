// Canvas: the markup tools (wire, rectangle, line, circle, text), the drag preview
// while a shape is being drawn, and the wire colour/width that new wires inherit.
#include <algorithm>
#include <cmath>
#include <string>

#include "app.h"
#include "i18n.h"
#include "settings.h"

// ---------------------------------------------------------------------------
// Tool state
// ---------------------------------------------------------------------------

void App::setTool(CanvasTool t) {
    if (tool == t) return;
    tool = t;
    annoDrawing = false;
    drag = DRAG_NONE;
    dragAnno = -1;
    dragAnnoHandle = -1;
    hover = Hit();
    if (tool != TOOL_SELECT) {
        // A tool owns the pointer: nothing under it should look selected-but-armed.
        selParts.clear();
        selWires.clear();
        selNotes.clear();
        selAnnos.clear();
    }
}

// One shortcut for the shape tools: rectangle, line, circle and round again.
void App::cycleShapeTool() {
    switch (tool) {
        case TOOL_RECT: setTool(TOOL_LINE); break;
        case TOOL_LINE: setTool(TOOL_CIRCLE); break;
        case TOOL_CIRCLE: setTool(TOOL_RECT); break;
        default: setTool(TOOL_RECT); break;
    }
}

void App::cancelAnnoTool() {
    annoDrawing = false;
    tool = TOOL_SELECT;
    drag = DRAG_NONE;
}

// Turns a finished drag into an annotation. Only one undo step per shape: the
// checkpoint happens here, when the shape actually exists.
void App::createAnnoFromDrag(CanvasTool t, Vec2 a, Vec2 b) {
    if (t != TOOL_RECT && t != TOOL_LINE && t != TOOL_CIRCLE) return;
    Anno an;
    an.id = doc.newId();
    an.view = view;
    Vec2 pa = snapToGrid ? snapv(a, gridSize(view) / 2) : a;
    Vec2 pb = snapToGrid ? snapv(b, gridSize(view) / 2) : b;
    // A click (or a very short drag) still makes a shape of a usable size, so the
    // tool never looks like it did nothing.
    const float minSize = gridSize(view) * 2;
    switch (t) {
        case TOOL_RECT:
            an.kind = AnnoKind::Rect;
            if (std::fabs(pb.x - pa.x) < minSize) pb.x = pa.x + minSize * (pb.x < pa.x ? -1 : 1);
            if (std::fabs(pb.y - pa.y) < minSize) pb.y = pa.y + minSize * (pb.y < pa.y ? -1 : 1);
            an.a = pa;
            an.b = pb;
            break;
        case TOOL_LINE:
            an.kind = AnnoKind::Line;
            if (dist(pa, pb) < minSize) pb = pa + Vec2(minSize * 2, 0);
            an.a = pa;
            an.b = pb;
            break;
        case TOOL_CIRCLE:
            an.kind = AnnoKind::Circle;
            an.a = pa;
            an.b = dist(pa, pb) < minSize ? pa + Vec2(minSize, 0) : pb;
            break;
        default:
            return;
    }
    checkpoint();
    doc.annos.push_back(an);
    selParts.clear();
    selWires.clear();
    selNotes.clear();
    selAnnos = {an.id};
    changed();
    annoDrawing = false;
}

// Pointer input while a tool is active. Returns true when the tool handled this
// frame, so the selection code below it is skipped entirely.
bool App::annoToolInput() {
    if (tool == TOOL_SELECT || readOnly) return false;
    Vec2 m = in.mouse;
    bool overCanvas = rCanvas.contains(m) && !ui.overBlocking(m) &&
                      !(compact && (drawerParts || drawerInspector || drawerLayers || drawerAI) && rRight.contains(m));

    // Finish the tool: a right click, Return, or a double-click on the canvas.
    // Esc is handled in handleShortcuts(), where the keyboard lives.
    if (in.rightPressed || in.key(SDLK_RETURN) || in.key(SDLK_KP_ENTER) || (in.doubleClick && overCanvas && !annoDrawing)) {
        if (annoDrawing) {
            annoDrawing = false;
            canvasOwnsPointer = false;
        } else {
            cancelAnnoTool();
        }
        return true;
    }

    if (tool == TOOL_TEXT) {
        if (in.pressed && overCanvas) {
            Anno an;
            an.id = doc.newId();
            an.view = view;
            an.kind = AnnoKind::Text;
            an.a = toWorld(m);
            an.b = Vec2(1800, 0);
            annoSetText(an, tr("anno.placeholder"));
            checkpoint();
            doc.annos.push_back(an);
            selParts.clear();
            selWires.clear();
            selNotes.clear();
            selAnnos = {an.id};
            changed();
            // One text box per click. The inspector opens on a double-click, so the
            // click that placed the box does not also open (and close) the drawer.
            tool = TOOL_SELECT;
            return true;
        }
        return overCanvas || in.down;
    }

    if (tool == TOOL_WIRE) {
        // A drag from anywhere draws a wire; both ends snap onto pins when close.
        if (in.pressed && overCanvas) {
            annoDrawing = true;
            canvasOwnsPointer = true;
            annoStart = toWorld(m);
            annoNow = annoStart;
            return true;
        }
        if (annoDrawing) {
            annoNow = toWorld(m);
            if (in.released) {
                annoDrawing = false;
                canvasOwnsPointer = false;
                Vec2 a = annoStart, b = annoNow;
                if (snapToGrid) {
                    a = snapv(a, gridSize(view) / 2);
                    b = snapv(b, gridSize(view) / 2);
                }
                if (dist(a, b) > gridSize(view) / 2) {
                    Wire wr;
                    wr.id = doc.newId();
                    wr.view = view;
                    wr.pts = {a, b};
                    wr.layer = activeLayer;
                    wr.width = traceWidthMem[PCB] > 0 ? traceWidthMem[PCB] : doc.rules.traceWidth;
                    wr.color = wireColorMem[view];
                    int part = -1, pin = -1;
                    if (pinAt(toScreen(a), part, pin, -1, -1)) wr.ends[0] = {part, pin};
                    if (pinAt(toScreen(b), part, pin, -1, -1)) wr.ends[1] = {part, pin};
                    checkpoint();
                    doc.wires.push_back(wr);
                    if (autoTidy && wr.ends[0].attached() && wr.ends[1].attached()) tidyWire(doc, wr.id);
                    selParts.clear();
                    selNotes.clear();
                    selAnnos.clear();
                    selWires = {wr.id};
                    changed();
                }
            }
            return true;
        }
        return overCanvas;
    }

    // Rectangle, line, circle: press to start, drag to size, release to commit.
    if (in.pressed && overCanvas) {
        annoDrawing = true;
        canvasOwnsPointer = true;
        annoStart = toWorld(m);
        annoNow = annoStart;
        return true;
    }
    if (annoDrawing) {
        annoNow = toWorld(m);
        if (in.released) {
            canvasOwnsPointer = false;
            createAnnoFromDrag(tool, annoStart, annoNow);
        }
        return true;
    }
    return overCanvas;
}

void App::drawAnnoPreview(const Camera& c, float zoom, float uiScale) {
    if (!annoDrawing || tool == TOOL_SELECT || tool == TOOL_TEXT) return;
    if (tool == TOOL_WIRE) {
        Vec2 a = c.pan + annoStart * zoom, b = c.pan + annoNow * zoom;
        g.dashedLine(a, b, std::max(1.5f * uiScale, 1.f), 8 * uiScale, 6 * uiScale, rgba(28, 116, 222, 200));
        return;
    }
    Anno an;
    an.view = view;
    an.a = annoStart;
    an.b = annoNow;
    an.kind = tool == TOOL_RECT ? AnnoKind::Rect : tool == TOOL_LINE ? AnnoKind::Line : AnnoKind::Circle;
    if (an.kind == AnnoKind::Circle && dist(an.a, an.b) < 1.f) an.b = an.a + Vec2(1, 0);
    drawAnno(g, an, c.pan, zoom, uiScale, false, false);
}

// A one-line reminder of what the active tool does and how to leave it.
void App::drawToolHint() {
    if (tool == TOOL_SELECT || readOnly) return;
    float s = ui.s;
    std::string hint = tr(tool == TOOL_TEXT ? "tool.hint.text" : tool == TOOL_WIRE ? "tool.hint.wire" : "tool.hint.shape");
    // Centre it in the part of the canvas that is not behind a drawer.
    Rect visible = rCanvas;
    if (compact && (drawerParts || drawerInspector || drawerLayers || drawerAI) && rRight.x > rCanvas.x)
        visible.w = rRight.x - rCanvas.x;
    float w = std::min(g.textWidth(hint, 13.5f * s) + 40 * s, std::max(120.f * s, visible.w - 24 * s));
    Rect r(visible.center().x - w / 2, visible.b() - 46 * s, w, 32 * s);
    g.fillRoundRect(r, 16 * s, rgba(28, 116, 222, 235));
    g.text(r.center(), hint, 13.5f * s, rgba(255, 255, 255), 1);
}

// ---------------------------------------------------------------------------
// Remembered wire colour (breadboard/schematic) and trace width (PCB)
// ---------------------------------------------------------------------------

void App::loadWirePrefs() {
    static const char* colorKeys[NUM_VIEWS] = {"wire.color.bb", "wire.color.sch", "wire.color.pcb"};
    for (int v = 0; v < NUM_VIEWS; v++)
        wireColorMem[v] = parseColor(settingsGet(colorKeys[v], ""), wireColorMem[v]);
    traceWidthMem[PCB] = (float)settingsGetInt("wire.width.pcb", 0);
}

void App::saveWirePrefs(int v) {
    if (v < 0 || v >= NUM_VIEWS) return;
    if (v == PCB) {
        if (traceWidthMem[PCB] > 0) settingsSetInt("wire.width.pcb", (long long)std::lround(traceWidthMem[PCB]));
    }
    static const char* colorKeys[NUM_VIEWS] = {"wire.color.bb", "wire.color.sch", "wire.color.pcb"};
    settingsSet(colorKeys[v], colorToHex(wireColorMem[v]));
}

// The only way a wire colour may change. Anything that restores a design (undo,
// paste, opening a file) or that the program draws itself (the autorouter) writes
// Wire::color directly on purpose, so it never touches this memory.
void App::setWireColor(int v, int wireId, uint32_t c) {
    if (v < 0 || v >= NUM_VIEWS) return;
    Wire* w = doc.wire(wireId);
    bool sameColor = w && w->color == c;
    bool sameMem = wireColorMem[v] == c;
    if (sameColor && sameMem) return;
    if (w && !sameColor) {
        checkpoint();
        w->color = c;
        changed();
    }
    wireColorMem[v] = c;
    saveWirePrefs(v);
}

void App::setWireColor(int v, const std::set<int>& wireIds, uint32_t c) {
    if (v < 0 || v >= NUM_VIEWS) return;
    bool any = false;
    for (int id : wireIds)
        if (const Wire* w = doc.wire(id))
            if (w->color != c) any = true;
    if (any) {
        checkpoint();
        for (int id : wireIds)
            if (Wire* w = doc.wire(id)) w->color = c;
        changed();
    }
    if (wireColorMem[v] != c) {
        wireColorMem[v] = c;
        saveWirePrefs(v);
    }
}

void App::rememberTraceWidth(int v, float w) {
    if (v != PCB || w <= 0) return;
    if (std::fabs(traceWidthMem[PCB] - w) < 0.01f) return;
    traceWidthMem[PCB] = w;
    saveWirePrefs(PCB);
}
