#include "settings.h"

#include <SDL3/SDL.h>

#include <ctime>
#include <string>

#include "json.h"
#include "toml.h"

namespace {

// SDL_GetPrefPath returns the path with a trailing separator, so no join helper is
// needed here (and adding one produced double slashes in past code).
std::string prefDir() {
    char* p = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string s = p ? p : "";
    if (p) SDL_free(p);
    return s;
}

std::string g_warning;      // translation key of a problem worth telling the user about
toml::Doc g_doc;            // the preferences, as read
bool g_loaded = false;
bool g_writable = true;     // false once a write failed (read-only folder, browser sandbox)

std::string pathIn(const char* name) {
    std::string dir = prefDir();
    return dir.empty() ? std::string() : dir + name;
}

std::string configPath() { return pathIn("config.toml"); }
std::string backupPath() { return pathIn("config.toml.bak"); }
std::string legacyPath() { return pathIn("settings.json"); }
std::string legacyBackupPath() { return pathIn("settings.json.bak"); }

bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info);
}

bool readFile(const std::string& path, std::string* out) {
    if (path.empty() || !out) return false;
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return false;
    *out = std::string((const char*)data, size);
    SDL_free(data);
    return true;
}

bool writeFile(const std::string& path, const std::string& text) {
    if (path.empty()) return false;
    return SDL_SaveFile(path.c_str(), text.data(), text.size());
}

// One generation back, taken before every write: a mistyped hand edit can be undone
// by copying the backup over the file.
void backupCurrent() {
    std::string text;
    if (readFile(configPath(), &text)) writeFile(backupPath(), text);
}

void save() {
    if (!g_writable) return;
    std::string next = g_doc.dump();
    std::string current;
    if (readFile(configPath(), &current) && current == next) return;  // nothing changed on disk
    backupCurrent();
    if (!writeFile(configPath(), next)) {
        g_writable = false;
        g_warning = "settings.warn.read_only";
        SDL_Log("settings: could not write %s (%s)", configPath().c_str(), SDL_GetError());
    }
}

// Migrates an old settings.json: no config.toml yet, but the old file is there.
void migrateLegacy() {
    std::string legacy;
    if (!readFile(legacyPath(), &legacy)) return;  // fresh install
    bool ok = false;
    std::string text = settingsMigrateJson(legacy, &ok);
    if (!ok) {
        SDL_Log("settings: settings.json is damaged, ignoring it");
        return;
    }
    if (!writeFile(configPath(), text)) {
        g_writable = false;
        g_warning = "settings.warn.read_only";
        SDL_Log("settings: could not write %s (%s)", configPath().c_str(), SDL_GetError());
        return;
    }
    g_doc.parse(text, nullptr);
    // Keep exactly one copy of the old file.
    SDL_RemovePath(legacyBackupPath().c_str());
    if (!SDL_RenamePath(legacyPath().c_str(), legacyBackupPath().c_str()))
        SDL_Log("settings: could not rename settings.json (%s)", SDL_GetError());
    SDL_Log("settings: migrated settings.json to config.toml");
}

void load() {
    std::string text;
    if (readFile(configPath(), &text)) {
        std::string err;
        if (!g_doc.parse(text, &err)) {
            SDL_Log("settings: config.toml is damaged (%s)", err.c_str());
            std::string bak;
            if (readFile(backupPath(), &bak) && g_doc.parse(bak, nullptr)) {
                g_warning = "settings.warn.restored";
                writeFile(configPath(), bak);  // put the good copy back (the backup stays)
                SDL_Log("settings: restored config.toml from config.toml.bak");
            } else {
                g_doc = toml::Doc();
                g_warning = "settings.warn.reset";
            }
            return;
        }
        // config.toml wins; an old settings.json next to it is left untouched.
        if (fileExists(legacyPath())) SDL_Log("settings: config.toml and settings.json both exist, using config.toml");
        return;
    }
    migrateLegacy();
}

}  // namespace

int normalizedUIFontSize(long long value) {
    return value >= 12 && value <= 20 ? static_cast<int>(value) : 14;
}

std::string settingsPath() { return configPath(); }

void settingsInit() {
    if (g_loaded) return;
    g_loaded = true;
    if (prefDir().empty()) {
        // No writable preferences folder (browser sandbox, locked-down install):
        // run on defaults instead of failing to start.
        g_writable = false;
        g_warning = "settings.warn.read_only";
        return;
    }
    load();
}

std::string settingsWarning() { return g_warning; }

std::string settingsGet(const char* key, const std::string& def) {
    if (!key || !*key) return def;
    settingsInit();
    return g_doc.get(key, def);
}

long long settingsGetInt(const char* key, long long def) {
    if (!key || !*key) return def;
    settingsInit();
    return g_doc.getInt(key, def);
}

bool settingsBool(const char* key, bool def) {
    if (!key || !*key) return def;
    settingsInit();
    return g_doc.getBool(key, def);
}

void settingsSet(const char* key, const std::string& value) {
    if (!key || !*key) return;
    settingsInit();
    if (g_doc.v.count(key) && g_doc.get(key) == value) return;  // already stored
    g_doc.set(key, value);
    save();
}

void settingsSetInt(const char* key, long long value) {
    if (!key || !*key) return;
    settingsInit();
    if (g_doc.v.count(key) && g_doc.getInt(key) == value) return;
    g_doc.setInt(key, value);
    save();
}

void settingsSetBool(const char* key, bool value) {
    if (!key || !*key) return;
    settingsInit();
    if (g_doc.v.count(key) && g_doc.getBool(key) == value) return;
    g_doc.setBool(key, value);
    save();
}

std::string settingsMigrateJson(const std::string& jsonText, bool* ok) {
    Json j = Json::parse(jsonText);
    if (!j.isObject()) {
        if (ok) *ok = false;
        return std::string();
    }
    if (ok) *ok = true;
    char date[32] = "";
    time_t t = time(nullptr);
    strftime(date, sizeof date, "%Y-%m-%d", gmtime(&t));
    toml::Doc d;
    d.parse("", nullptr);
    for (auto& kv : j.members()) {
        if (kv.first == "version") continue;  // the JSON schema version, TOML needs none
        if (kv.second.isString()) d.set(kv.first, kv.second.str());
        else if (kv.second.isNumber()) d.setInt(kv.first, (long long)kv.second.num());
        else if (kv.second.type() == Json::Bool) d.setBool(kv.first, kv.second.boolean());
        // Nested arrays/objects have no flat equivalent here and are dropped.
    }
    return "# migrated from settings.json on " + std::string(date) + "\n" + d.dump();
}
