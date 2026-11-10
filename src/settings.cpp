#include "settings.h"

#include <SDL3/SDL.h>

#include "json.h"

namespace {

// SDL_GetPrefPath returns the path with a trailing separator, so no join helper
// is needed here (and adding one produced double slashes in past code).
std::string prefDir() {
    char* p = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string s = p ? p : "";
    if (p) SDL_free(p);
    return s;
}

Json readAll() {
    std::string path = settingsPath();
    size_t size = 0;
    void* data = path.empty() ? nullptr : SDL_LoadFile(path.c_str(), &size);
    if (!data) return Json::object();
    std::string text((const char*)data, size);
    SDL_free(data);
    Json j = Json::parse(text);
    return j.isObject() ? j : Json::object();
}

}  // namespace

std::string settingsPath() {
    std::string dir = prefDir();
    if (dir.empty()) return std::string();
    return dir + "settings.json";
}

std::string settingsGet(const char* key, const std::string& def) {
    Json j = readAll();
    const Json& v = j[key];
    return v.isString() ? v.str() : def;
}

void settingsSet(const char* key, const std::string& value) {
    std::string path = settingsPath();
    if (path.empty()) return;
    Json j = readAll();
    j.set("version", Json(1));
    j.set(key, Json(value));
    std::string text = j.dump(2);
    SDL_SaveFile(path.c_str(), text.data(), text.size());
}
