// designElectronics — the Settings sheet: keyboard shortcuts and mouse roles.
//
// Everything here edits App::binds and saves config.toml straight away; the canvas
// reads the same table every frame, so a change is live without a restart.
#include <algorithm>
#include <string>
#include <vector>

#include "app.h"
#include "i18n.h"
#include "keys.h"

void App::openSettingsSheet() {
    captureChord = -1;
    ui.openSheet("settings", tr("settings.title"), 620, 640, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float x = body.x, w = body.w;
        float rowH = 36 * s;
        // The sheet holds more rows than fit on a short window, so it scrolls: measure
        // the paragraph texts first, then let the scroll region own the offset.
        float notesH = ui.wrappedText(Rect(x, 0, w, 0), tr("settings.mouse_note"), th.textDim, 12 * s, false) +
                       ui.wrappedText(Rect(x, 0, w, 0), tr("settings.saved_hint"), th.textDim, 12 * s, false) + 12 * s;
        float contentH = 24 * s + (float)kActionCount * rowH + 8 * s + rowH + 16 * s + 24 * s + (float)kMouseSlots * rowH + notesH + 40 * s;
        float oy = ui.beginScroll("settings-body", body, contentH);
        float y = body.y + oy;
        auto heading = [&](const std::string& t) {
            y += 6 * s;
            g.text({x, y + 9 * s}, t, 12 * s, th.textDim, 0);
            y += 24 * s;
        };

        // ---- keyboard ----
        heading(tr("settings.section.keys"));
        for (int i = 0; i < kActionCount; i++) {
            Action a = (Action)i;
            bool capturing = captureChord == i;
            float bt = 150 * s;
            g.text({x, y + rowH / 2}, actionLabel(a), 13.5f * s, th.text, 0);
            Rect kb(x + w - bt - 74 * s, y + 3 * s, bt, rowH - 6 * s);
            if (ui.button("kch" + std::to_string(i), kb, capturing ? tr("settings.press_keys") : chordToText(binds.chord[i]),
                          capturing ? BTN_PRIMARY : BTN_NORMAL))
                captureChord = capturing ? -1 : i;
            if (ui.button("krs" + std::to_string(i), Rect(x + w - 68 * s, y + 3 * s, 68 * s, rowH - 6 * s), tr("settings.reset"),
                          BTN_FLAT))
                { binds.chord[i] = defaultBindings().chord[i]; saveBindings(); captureChord = -1; }
            y += rowH;
        }
        y += 8 * s;
        if (ui.button("kset-reset", Rect(x, y, w, rowH), tr("settings.restore_all"), BTN_NORMAL)) {
            ui.dialog(tr("settings.restore_all_title"), {tr("settings.restore_all_body")},
                      {{tr("settings.restore_all"), [this]() { binds = defaultBindings(); saveBindings(); captureChord = -1; }, true},
                       {tr("dlg.cancel"), nullptr}});
        }
        y += rowH + 16 * s;

        // ---- mouse ----
        heading(tr("settings.section.mouse"));
        static const char* slotKeys[kMouseSlots] = {"settings.mouse.middle", "settings.mouse.right", "settings.mouse.space_left"};
        std::vector<std::string> options;
        for (int r = 0; r < kMouseRoleCount; r++) options.push_back(mouseRoleId((MouseRole)r));
        for (int i = 0; i < kMouseSlots; i++) {
            g.text({x, y + rowH / 2}, tr(slotKeys[i]), 13.5f * s, th.text, 0);
            std::string value = mouseRoleId(binds.mouse[i]);
            if (ui.comboField("msl" + std::to_string(i), Rect(x + w - 260 * s, y + 3 * s, 260 * s, rowH - 6 * s), value, options, false,
                              [](const std::string& v) {
                                  MouseRole r;
                                  return mouseRoleFromId(v, &r) ? mouseRoleLabel(r) : v;
                              })) {
                MouseRole r;
                if (mouseRoleFromId(value, &r)) {
                    binds.mouse[i] = r;
                    saveBindings();
                }
            }
            y += rowH;
        }
        y += 6 * s;
        y += ui.wrappedText(Rect(x, y, w, 0), tr("settings.mouse_note"), th.textDim, 12 * s) + 4 * s;
        if (binds.chord[(int)Action::AddBend].bound())
            y += ui.wrappedText(Rect(x, y, w, 0), tr("settings.add_bend_bound"), th.textDim, 12 * s) + 4 * s;
        y += ui.wrappedText(Rect(x, y, w, 0), tr("settings.saved_hint"), th.textDim, 12 * s) + 8 * s;
        ui.endScroll();

        // ---- capture the next chord ----
        if (captureChord >= 0 && captureChord < kActionCount && !ui.hasDialog()) {
            if (in.key(SDLK_ESCAPE)) {  // Esc always cancels the capture, it never binds
                captureChord = -1;
            } else if (!in.keys.empty()) {
                KeyChord c;
                c.key = in.keys.front();
                c.ctrl = in.cmd();
                c.shift = in.shift();
                c.alt = (in.mods & SDL_KMOD_ALT) != 0;
                Action a = (Action)captureChord;
                if (!chordAllowed(a, c)) {
                    ui.toast(tr("settings.rejected_bare"), 5);
                } else {
                    int other = findChord(binds, c, captureChord);
                    if (other >= 0) {
                        Action loser = (Action)other;
                        std::string loserLabel = actionLabel(loser);
                        std::string chordText = chordToText(c);
                        ui.dialog(tr("settings.conflict_title"), {trf("settings.conflict_body", {chordText, loserLabel})},
                                  {{tr("settings.replace"), [this, a, c, other]() {
                                        applyBinding(a, c, other);
                                        captureChord = -1;
                                    }, true},
                                   {tr("dlg.cancel"), [this]() { captureChord = -1; }}});
                    } else {
                        applyBinding(a, c);
                        captureChord = -1;
                    }
                }
            }
        }
    });
}
