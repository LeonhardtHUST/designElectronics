#include "licenses.h"

#include <algorithm>
#include <memory>

#include "i18n.h"
#include "ui.h"

extern "C" const unsigned char de_licenses[];
extern "C" const unsigned long de_licenses_size;

std::vector<std::pair<std::string, std::string>> licenseDocuments() {
    std::string blob(reinterpret_cast<const char*>(de_licenses), de_licenses_size);
    while (!blob.empty() && blob.back() == '\0') blob.pop_back();
    std::vector<std::pair<std::string, std::string>> documents;
    size_t position = 0;
    while (position < blob.size()) {
        if (blob.compare(position, 6, "@file ") != 0) break;
        size_t nameEnd = blob.find('\n', position);
        if (nameEnd == std::string::npos) break;
        std::string name = blob.substr(position + 6, nameEnd - position - 6);
        if (!name.empty() && name.back() == '\r') name.pop_back();
        size_t next = blob.find("\n@file ", nameEnd);
        size_t textEnd = next == std::string::npos ? blob.size() : next;
        documents.emplace_back(name, blob.substr(nameEnd + 1, textEnd - nameEnd - 1));
        if (next == std::string::npos) break;
        position = next + 1;
    }
    return documents;
}

void openSourceLicenses(UI& ui) {
    struct Reader {
        std::vector<std::pair<std::string, std::string>> documents;
        std::vector<std::string> names, lines;
        std::string selected, wrappedDocument;
        float width = -1, fontSize = -1;
    };
    auto reader = std::make_shared<Reader>();
    reader->documents = licenseDocuments();
    for (const auto& document : reader->documents) reader->names.push_back(document.first);
    if (!reader->names.empty()) reader->selected = reader->names.front();
    ui.scrollTo("licenses-text", 0);
    ui.openSheet("licenses", tr("about.licenses"), 820, 700, [&ui, reader](Rect body) {
        float scale = ui.s;
        float fontSize = 14 * scale;
        float lineHeight = fontSize * 1.4f;
        float selectorHeight = 36 * scale;
        if (reader->documents.empty()) {
            ui.wrappedText(body, tr("about.licenses_missing"), ui.th.danger, fontSize);
            return;
        }
        if (ui.comboField("licenses-document", Rect(body.x, body.y, body.w, selectorHeight),
                          reader->selected, reader->names, false))
            ui.scrollTo("licenses-text", 0);
        Rect textRect(body.x, body.y + selectorHeight + 10 * scale, body.w,
                      std::max(0.f, body.h - selectorHeight - 10 * scale));
        float textWidth = std::max(1.f, textRect.w - 16 * scale);
        if (reader->width != textWidth || reader->fontSize != fontSize || reader->wrappedDocument != reader->selected) {
            for (const auto& document : reader->documents) {
                if (document.first != reader->selected) continue;
                reader->lines = wrapText(*ui.g, document.second, fontSize, textWidth);
                break;
            }
            reader->width = textWidth;
            reader->fontSize = fontSize;
            reader->wrappedDocument = reader->selected;
        }
        float offset = ui.beginScroll("licenses-text", textRect, reader->lines.size() * lineHeight + 8 * scale);
        for (size_t index = 0; index < reader->lines.size(); index++) {
            float top = textRect.y + offset + index * lineHeight;
            if (top + lineHeight < textRect.y || top > textRect.b()) continue;
            ui.g->text({textRect.x, top + lineHeight * 0.5f}, reader->lines[index], fontSize, ui.th.text, 0);
        }
        ui.endScroll();
    });
}
