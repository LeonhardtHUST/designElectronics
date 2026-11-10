// designElectronics — translations.
//
// Language packs are flat JSON objects ("menu.file.new": "新建") compiled into the
// binary by cmake/embed_parts.cmake, so every build (desktop, web, mobile) carries
// them without files on disk. A build may be overridden at runtime: any *.json in
// i18n/ next to the executable (or in the preferences folder) replaces or extends
// the built-in packs — see --export-i18n.
//
// Lookup order for a key: current language, then English, then the key itself.
// Returning the key is deliberate: a missing translation shows up as
// "menu.file.new" in the UI instead of silently falling back to English.
#pragma once
#include <string>
#include <vector>

struct LangInfo {
    std::string code;         // "en", "zh-CN"
    std::string nativeName;   // "English", "简体中文"
    std::string englishName;   // "Chinese (Simplified)"
    bool rtl = false;         // reserved; right-to-left layout is not implemented
};

namespace i18n {
// Parses the packs compiled into the binary. Touches no files and needs no SDL,
// so --selftest can call it before a window exists.
void initEmbedded();
// initEmbedded() + packs found on disk. `force` overrides the stored/system choice.
void init(const std::string& force = "");

const std::vector<LangInfo>& languages();
const std::string& lang();
bool setLang(const std::string& code);
// Best match for the OS locale ("zh-CN", "en", …); never empty.
std::string systemLanguage();

// Diagnostics: keys asked for that no pack provides.
const std::vector<std::string>& missing();
// True when the current language (or English) has this key. Used by the part
// display helpers, which fall back to the English text stored in the definition.
bool exists(const char* key);

// Every loaded pack code, plus a stable (sorted) JSON dump for --export-i18n.
std::vector<std::string> packCodes();
std::string dumpPack(const std::string& code);
}  // namespace i18n

// Short global names, because these are used hundreds of times.
const std::string& tr(const char* key);
const std::string& tr(const std::string& key);
// Substitutes {0}…{9}; "{{" and "}}" produce literal braces.
std::string trf(const char* key, std::initializer_list<std::string> args);
// Plural form: looks up "<key>.one" when n == 1, else "<key>.other"; {0} is n.
std::string trn(const char* key, long long n);
// Locale-independent number formatting ("%.2f" style) for trf/trn arguments.
std::string num(double v, int decimals = 0);

// "Analog ICs" -> "analog_ics" — used to build keys from English display names.
std::string slugify(const std::string& english);
// Display name of a view (0 breadboard, 1 schematic, 2 pcb). Not for file output:
// keep using viewName()/viewKey() from partlib.h when writing files.
std::string viewLabel(int v);
