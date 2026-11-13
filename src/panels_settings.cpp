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
#include "settings.h"

void App::loadUIFontSize() {
    uiFontSize = normalizedUIFontSize(settingsGetInt("appearance.font_size", 14));
}

void App::setUIFontSize(int value) {
    uiFontSize = normalizedUIFontSize(value);
    if (!scratchSession) settingsSetInt("appearance.font_size", uiFontSize);
}

void App::openSettingsSheet() {
    captureChord = -1;
    ui.openSheet("settings", tr("settings.title"), 620, 640, [this](Rect body) {
        float s = ui.s;
        auto& th = ui.th;
        float x = body.x, w = body.w;
        float rowH = 36 * s;
        // The sheet holds more rows than fit on a short window, so it scrolls: measure
        // the paragraph texts first, then let the scroll region own the offset.
        auto noteHeight = [&](const char* key) {
            return ui.wrappedText(Rect(x, 0, w, 0), tr(key), th.textDim, 12 * s, false);
        };
        bool stacked = w < 440 * s;
        float pairedWidth = stacked ? w : 224 * s;
        float mouseWidth = stacked ? w : 260 * s;
        float resetWidth = std::min(68 * s, pairedWidth * 0.32f);
        float controlGap = std::min(6 * s, pairedWidth * 0.04f);
        auto labelHeight = [&](const std::string& label, float controlsWidth) {
            float labelWidth = stacked ? w : w - controlsWidth - 12 * s;
            return ui.wrappedText(Rect(x, 0, labelWidth, 0), label, th.text, 13.5f * s, false);
        };
        auto settingRowHeight = [&](const std::string& label, float controlsWidth) {
            float height = labelHeight(label, controlsWidth);
            return stacked ? height + 6 * s + rowH : std::max(rowH, height + 6 * s);
        };
        static const char* slotKeys[kMouseSlots] = {"settings.mouse.middle", "settings.mouse.right", "settings.mouse.space_left"};
        bool addBendBound = binds.chord[(int)Action::AddBend].bound();
        float appearanceH = 30 * s + settingRowHeight(tr("settings.font_size"), pairedWidth) + 6 * s +
                            noteHeight("settings.font_size.note") + 12 * s;
        float keyboardH = 30 * s + 8 * s + rowH + 16 * s;
        for (int index = 0; index < kActionCount; index++)
            keyboardH += settingRowHeight(actionLabel((Action)index), pairedWidth);
        float mouseRowsH = 0;
        for (int index = 0; index < kMouseSlots; index++)
            mouseRowsH += settingRowHeight(tr(slotKeys[index]), mouseWidth);
        float mouseH = 30 * s + mouseRowsH + 6 * s + noteHeight("settings.mouse_note") + 4 * s +
                       (addBendBound ? noteHeight("settings.add_bend_bound") + 4 * s : 0) +
                       noteHeight("settings.saved_hint") + 8 * s;
        float contentH = appearanceH + keyboardH + mouseH;
        float oy = ui.beginScroll("settings-body", body, contentH);
        float y = body.y + oy;
        auto heading = [&](const std::string& t) {
            y += 6 * s;
            g.text({x, y + 9 * s}, t, 12 * s, th.textDim, 0);
            y += 24 * s;
        };

        auto settingRow = [&](const std::string& label, float controlsWidth) {
            float height = settingRowHeight(label, controlsWidth);
            float textHeight = labelHeight(label, controlsWidth);
            float labelWidth = stacked ? w : w - controlsWidth - 12 * s;
            float labelY = stacked ? y : y + (height - textHeight) / 2;
            ui.wrappedText(Rect(x, labelY, labelWidth, 0), label, th.text, 13.5f * s);
            float controlsY = stacked ? y + textHeight + 6 * s : y + (height - rowH) / 2;
            Rect controls(x + w - controlsWidth, controlsY + 3 * s, controlsWidth, rowH - 6 * s);
            y += height;
            return controls;
        };

        heading(tr("settings.section.appearance"));
        Rect fontControls = settingRow(tr("settings.font_size"), pairedWidth);
        std::string fontSize = std::to_string(uiFontSize);
        const std::vector<std::string> fontSizes = {"12", "14", "16", "18", "20"};
        if (ui.comboField("settings-font-size", Rect(fontControls.x, fontControls.y, pairedWidth - resetWidth - controlGap, fontControls.h),
                          fontSize, fontSizes, false, [](const std::string& value) {
                              return value == "14" ? tr("settings.font_size.default") : value;
                          })) {
            for (const auto& value : fontSizes)
                if (fontSize == value) setUIFontSize(std::stoi(value));
        }
        if (ui.button("settings-font-reset", Rect(fontControls.x + pairedWidth - resetWidth, fontControls.y, resetWidth, fontControls.h),
                      tr("settings.reset"), BTN_FLAT))
            setUIFontSize(14);
        y += 6 * s;
        y += ui.wrappedText(Rect(x, y, w, 0), tr("settings.font_size.note"), th.textDim, 12 * s) + 12 * s;

        // ---- keyboard ----
        heading(tr("settings.section.keys"));
        for (int i = 0; i < kActionCount; i++) {
            Action a = (Action)i;
            bool capturing = captureChord == i;
            Rect keyControls = settingRow(actionLabel(a), pairedWidth);
            Rect kb(keyControls.x, keyControls.y, pairedWidth - resetWidth - controlGap, keyControls.h);
            if (ui.button("kch" + std::to_string(i), kb, capturing ? tr("settings.press_keys") : chordToText(binds.chord[i]),
                          capturing ? BTN_PRIMARY : BTN_NORMAL))
                captureChord = capturing ? -1 : i;
            if (ui.button("krs" + std::to_string(i), Rect(keyControls.x + pairedWidth - resetWidth, keyControls.y, resetWidth, keyControls.h), tr("settings.reset"),
                          BTN_FLAT))
                { binds.chord[i] = defaultBindings().chord[i]; saveBindings(); captureChord = -1; }
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
        std::vector<std::string> options;
        for (int r = 0; r < kMouseRoleCount; r++) options.push_back(mouseRoleId((MouseRole)r));
        for (int i = 0; i < kMouseSlots; i++) {
            Rect mouseControls = settingRow(tr(slotKeys[i]), mouseWidth);
            std::string value = mouseRoleId(binds.mouse[i]);
            if (ui.comboField("msl" + std::to_string(i), mouseControls, value, options, false,
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
