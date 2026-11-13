// Canvas: the markup tools (wire, rectangle, line, circle, text), the drag preview
// while a shape is being drawn, and the wire colour/width that new wires inherit.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "app.h"
#include "i18n.h"
#include "settings.h"

// ---------------------------------------------------------------------------
// Tool state
// ---------------------------------------------------------------------------

void App::beginInlineAnnoEdit(int id) {
    Anno* anno = doc.anno(id);
    if (readOnly || tool != TOOL_SELECT || !anno || anno->view != view || anno->kind != AnnoKind::Text || anno->hidden) return;
    if (inlineAnnoEdit >= 0) finishInlineAnnoEdit();
    inlineAnnoEdit = id;
    inlineAnnoOriginal = *anno;
    inlineAnnoText = anno->text;
    inlineAnnoCheckpoint = false;
    inlineAnnoRevision = doc.revision;
    ui.focusField("annoinline" + std::to_string(id), true);
}

void App::updateInlineAnnoText(const std::string& text) {
    Anno* anno = doc.anno(inlineAnnoEdit);
    if (!anno || readOnly || doc.revision != inlineAnnoRevision || anno->text == text) return;
    if (!inlineAnnoCheckpoint) {
        inlineAnnoUndoDepth = undoStack.size();
        inlineAnnoRedo = redoStack;
        checkpoint();
        inlineAnnoCheckpoint = true;
    }
    inlineAnnoText = text;
    annoSetText(*anno, text);
    annoFitText(*anno, g);  // a point text's box follows what is being typed
    changed();
    inlineAnnoRevision = doc.revision;
}

void App::finishInlineAnnoEdit(bool cancel) {
    if (inlineAnnoEdit < 0) return;
    if (cancel && inlineAnnoCheckpoint && doc.revision == inlineAnnoRevision) {
        if (Anno* anno = doc.anno(inlineAnnoEdit)) {
            *anno = inlineAnnoOriginal;
            changed();
            undoStack.resize(inlineAnnoUndoDepth);
            redoStack = std::move(inlineAnnoRedo);
        }
    }
    ui.cancelPendingFocus();
    if (ui.fieldFocused("annoinline" + std::to_string(inlineAnnoEdit))) ui.clearFocus();
    inlineAnnoEdit = -1;
    inlineAnnoText.clear();
    inlineAnnoCheckpoint = false;
    inlineAnnoUndoDepth = 0;
    inlineAnnoRedo.clear();
}

Rect App::inlineAnnoRect() {
    const Anno* anno = doc.anno(inlineAnnoEdit);
    if (!anno || anno->view != view) return {};
    Vec2 top = toScreen(anno->a);
    float width, height;
    if (anno->pointText) {
        // A point text has no frame to type into, so the editor grows with the text: it
        // always fits the widest line, with a little room to keep going.
        float px = anno->textSize * cam[view].zoom;
        float lh = px * 1.35f;
        float widest = 0;
        int lines = 0;
        size_t start = 0;
        while (true) {
            size_t nl = inlineAnnoText.find('\n', start);
            std::string line = inlineAnnoText.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            widest = std::max(widest, g.textWidth(line, px, anno->font));
            lines++;
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
        width = std::max(180 * ui.s, widest + 48 * ui.s);
        height = std::max(64 * ui.s, lh * (float)std::max(1, lines) + 16 * ui.s);
    } else {
        Vec2 bottom = toScreen(anno->a + anno->b);
        width = std::max(180 * ui.s, bottom.x - top.x);
        height = std::max(64 * ui.s, bottom.y - top.y);
    }
    float rightEdge = compact && (drawerParts || drawerInspector || drawerLayers || drawerAI) ? rRight.x : rCanvas.r();
    float leftEdge = rCanvas.x;
    float topEdge = rCanvas.y;
    float bottomEdge = rCanvas.b();
    width = std::min(width, std::max(24 * ui.s, rightEdge - leftEdge));
    height = std::min(height, std::max(24 * ui.s, bottomEdge - topEdge));
    float left = std::clamp(top.x, leftEdge, rightEdge - width);
    float upper = std::clamp(top.y, topEdge, bottomEdge - height);
    return {left, upper, width, height};
}

void App::drawInlineAnnoEditor() {
    if (inlineAnnoEdit < 0) return;
    const Anno* anno = doc.anno(inlineAnnoEdit);
    if (readOnly || tool != TOOL_SELECT || !anno || anno->view != view || anno->hidden || anno->kind != AnnoKind::Text || doc.revision != inlineAnnoRevision) {
        finishInlineAnnoEdit();
        return;
    }
    if (in.key(SDLK_ESCAPE)) { finishInlineAnnoEdit(true); return; }
    Rect rect = inlineAnnoRect();
    if (rect.w < 24 * ui.s || rect.h < 24 * ui.s) {
        finishInlineAnnoEdit();
        return;
    }
    std::string id = "annoinline" + std::to_string(inlineAnnoEdit);
    bool submit = ui.textArea(id, rect, inlineAnnoText);
    updateInlineAnnoText(inlineAnnoText);
    if (submit || !ui.fieldFocused(id)) finishInlineAnnoEdit();
}

void App::setTool(CanvasTool t) {
    if (tool == t) return;
    tool = t;
    annoDrawing = false;
    schematicWireStart = {};
    schematicWireTarget = {};
    canvasOwnsPointer = false;
    spaceRotatePending = false;
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
    schematicWireStart = {};
    schematicWireTarget = {};
    canvasOwnsPointer = false;
    spaceRotatePending = false;
    tool = TOOL_SELECT;
    drag = DRAG_NONE;
}

Vec2 App::schematicWirePoint(Vec2 screen, WireEnd& end) {
    end = {};
    int part = -1, pin = -1;
    if (pinAt(screen, part, pin, -1, -1)) {
        end = {part, pin};
        return doc.part(part)->pinPos(pin, view);
    }
    Vec2 point = toWorld(screen);
    return snapToGrid ? snapv(point, gridSize(view) / 2) : point;
}

bool App::commitSchematicWire() {
    if (readOnly || view != SCH || tool != TOOL_WIRE || !annoDrawing || dist(annoStart, annoNow) < 0.01f) return false;
    Wire wire;
    wire.view = SCH;
    wire.pts = {annoStart, annoNow};
    wire.ends[0] = schematicWireStart;
    wire.ends[1] = schematicWireTarget;
    wire.layer = activeLayer;
    wire.width = traceWidthMem[PCB] > 0 ? traceWidthMem[PCB] : doc.rules.traceWidth;
    wire.color = wireColorMem[SCH];
    checkpoint();
    wire.id = doc.newId();
    doc.wires.push_back(wire);
    if (autoTidy && wire.ends[0].attached() && wire.ends[1].attached()) tidyWire(doc, wire.id);
    selParts.clear();
    selNotes.clear();
    selAnnos.clear();
    selWires = {wire.id};
    annoDrawing = false;
    schematicWireStart = {};
    schematicWireTarget = {};
    canvasOwnsPointer = false;
    changed();
    return true;
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
    if (tool == TOOL_SELECT || readOnly || ui.editingText() || ui.blocking()) return false;
    // A shape grabbed with a tool is run by canvas.cpp's DRAG_ANNO code — move,
    // resize, and the commit on release. Hand the pointer back to it until then.
    if (drag == DRAG_ANNO) return false;
    Vec2 m = in.mouse;
    bool overCanvas = rCanvas.contains(m) && !ui.overBlocking(m) &&
                      !(compact && (drawerParts || drawerInspector || drawerLayers || drawerAI) && rRight.contains(m));

    if (tool == TOOL_WIRE && view == SCH) {
        overCanvas = overCanvas && !(simOn && simNotesRect.contains(m));
        if ((in.rightPressed && overCanvas) || in.key(SDLK_ESCAPE) || in.key(SDLK_RETURN) || in.key(SDLK_KP_ENTER)) {
            cancelAnnoTool();
            in.rightPressed = false;
            return true;
        }
        if (spaceDown || drag == DRAG_PAN) return false;
        if (overCanvas) {
            annoNow = schematicWirePoint(m, schematicWireTarget);
            if (in.pressed) {
                if (!annoDrawing) {
                    annoStart = annoNow;
                    schematicWireStart = schematicWireTarget;
                    annoDrawing = true;
                } else {
                    commitSchematicWire();
                }
            }
        }
        canvasOwnsPointer = false;
        return overCanvas || annoDrawing;
    }

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
        // Click: point text, whose box hugs the glyphs and grows as you type.
        // Drag: a text box the size of the frame dragged out (dashed preview on the way).
        // Both open the in-place editor straight away, placeholder selected.
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
                Vec2 dPx = toScreen(annoNow) - toScreen(annoStart);
                bool dragged = std::fabs(dPx.x) > 6 * ui.s || std::fabs(dPx.y) > 6 * ui.s;
                Anno an;
                an.id = doc.newId();
                an.view = view;
                an.kind = AnnoKind::Text;
                an.a = annoStart;
                if (dragged) {
                    // The frame is dragged out corner to corner: it never flips, and it never
                    // shrinks below the size the resize handles allow.
                    an.a = Vec2(std::min(annoStart.x, annoNow.x), std::min(annoStart.y, annoNow.y));
                    an.b = Vec2(std::max(kTextBoxMinW, std::fabs(annoNow.x - annoStart.x)),
                                std::max(kTextBoxMinH, std::fabs(annoNow.y - annoStart.y)));
                } else {
                    an.pointText = true;
                }
                annoSetText(an, tr("anno.placeholder"));
                annoFitText(an, g);
                checkpoint();
                doc.annos.push_back(an);
                selParts.clear();
                selWires.clear();
                selNotes.clear();
                selAnnos = {an.id};
                changed();
                tool = TOOL_SELECT;  // one text per click: the next drag selects again
                beginInlineAnnoEdit(an.id);
                return true;
            }
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

    // Rectangle, line, circle: press to start, drag to size, release to commit —
    // unless the press lands on existing markup. Then the shape is grabbed (moved, or
    // resized from one of its handles) exactly as the selection tool would, so a
    // tool left active never keeps the user from fixing what is already drawn. Only
    // a press on empty canvas starts a new shape.
    //
    // A drawing tool only cares about markup: parts are drawn over annotations (their
    // priority 5 beats the annotation's 6), so a frame drawn around an IC would
    // otherwise never be grabbable. The candidates are filtered down to handles first,
    // then the nearest annotation stroke; pins, wires and part bodies are ignored here.
    auto grabHit = [&](Vec2 at) {
        std::vector<HitCand> cands;
        collectHits(at, cands);
        Hit best;
        int bestPrio = 99;
        float bestD = 1e30f;
        for (auto& c : cands) {
            int prio = c.h.kind == Hit::ANNO_HANDLE ? 0 : c.h.kind == Hit::ANNO ? 1 : 99;
            if (prio == 99) continue;
            // The body of a picture or a text box is a big hit area: drawing a frame over a
            // screenshot must start a new shape, so only strokes (and handles) are grabbed.
            if (c.h.kind == Hit::ANNO) {
                const Anno* an = doc.anno(c.h.anno);
                if (!an || an->kind == AnnoKind::Text || an->kind == AnnoKind::Image) continue;
            }
            if (prio < bestPrio || (prio == bestPrio && c.d < bestD)) {
                bestPrio = prio;
                bestD = c.d;
                best = c.h;
            }
        }
        return best;
    };
    if (in.pressed && overCanvas) {
        pressHit = grabHit(m);
        if (pressHit.kind == Hit::ANNO || pressHit.kind == Hit::ANNO_HANDLE) {
            canvasOwnsPointer = true;
            pressWorld = toWorld(m);
            startDrag();
            if (in.released) {  // press and release in the same frame: commit now
                finishPointer();
                canvasOwnsPointer = false;
            }
            return true;
        }
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
    // The tool owns the pointer, so canvas.cpp never gets to compute a hover: mark the
    // markup a press here would grab, so the shape lights up under the pointer.
    hover = Hit();
    if (overCanvas && !in.touch) {
        Hit h = grabHit(m);
        if (h.kind == Hit::ANNO || h.kind == Hit::ANNO_HANDLE) hover = h;
    }
    return overCanvas;
}

void App::drawAnnoPreview(const Camera& c, float zoom, float uiScale) {
    if (!annoDrawing || tool == TOOL_SELECT) return;
    if (tool == TOOL_WIRE) {
        Vec2 a = c.pan + annoStart * zoom, b = c.pan + annoNow * zoom;
        g.dashedLine(a, b, std::max(1.5f * uiScale, 1.f), 8 * uiScale, 6 * uiScale, rgba(28, 116, 222, 200));
        if (view == SCH) {
            if (schematicWireStart.attached()) g.strokeCircle(a, 7 * uiScale, 2 * uiScale, rgba(28, 116, 222));
            if (schematicWireTarget.attached()) g.strokeCircle(b, 7 * uiScale, 2 * uiScale, rgba(28, 116, 222));
        }
        return;
    }
    if (tool == TOOL_TEXT) {
        // The frame a dragged-out text box would get. A click makes point text instead,
        // so nothing is shown until the pointer has actually moved.
        Vec2 a = c.pan + annoStart * zoom, b = c.pan + annoNow * zoom;
        if (std::fabs(b.x - a.x) <= 6 * uiScale && std::fabs(b.y - a.y) <= 6 * uiScale) return;
        Rect r = Rect::fromPoints(a, b);
        float w = std::max(1.5f * uiScale, 1.f), dash = 8 * uiScale, gap = 6 * uiScale;
        uint32_t col = rgba(28, 116, 222, 200);
        g.dashedLine({r.x, r.y}, {r.r(), r.y}, w, dash, gap, col);
        g.dashedLine({r.r(), r.y}, {r.r(), r.b()}, w, dash, gap, col);
        g.dashedLine({r.r(), r.b()}, {r.x, r.b()}, w, dash, gap, col);
        g.dashedLine({r.x, r.b()}, {r.x, r.y}, w, dash, gap, col);
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
    std::string hint = tr(tool == TOOL_TEXT ? "tool.hint.text" : tool == TOOL_WIRE ? (view == SCH ? "tool.hint.wire.sch" : "tool.hint.wire") : "tool.hint.shape");
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
