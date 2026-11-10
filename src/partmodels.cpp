#include "partmodels.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

#include <SDL3/SDL.h>

#include "doc.h"
#include "partlib_internal.h"

namespace {

std::mutex g_mutex;
std::map<std::string, PartModel> g_models;
bool g_loaded = false;

std::string prefDir() {
    char* p = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string s = p ? p : "";
    if (p) SDL_free(p);
    return s;
}

std::string joinTags(const std::vector<std::string>& tags) {
    std::string out;
    for (auto& t : tags) {
        if (t.empty()) continue;
        if (!out.empty()) out += ' ';
        out += t;
    }
    return out;
}

}  // namespace

std::string partModelSlug(const std::string& s) {
    std::string out;
    bool gap = false;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out += (char)std::tolower(c);
            gap = false;
        } else if (!out.empty() && !gap) {
            out += '_';
            gap = true;
        }
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

std::string partModelDir() {
    static const std::string dir = [] {
        if (const char* env = std::getenv("DE_PARTS_DIR")) return std::string(env);  // tests / portable installs
        std::string p = prefDir();
        if (p.empty()) return std::string();
        while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
        return p + "/parts";
    }();
    return dir;
}

std::string partModelPathFor(const std::string& type) {
    std::string dir = partModelDir();
    std::string name = partModelSlug(type);
    if (name.empty()) name = "part";
    return dir.empty() ? name + ".dezpart" : dir + "/" + name + ".dezpart";
}

Json partModelToJson(const PartModel& m) {
    Json j = Json::object();
    j.set("format", "designElectronics-part");
    j.set("version", 2);
    Json meta = Json::object();
    meta.set("id", m.meta.id.empty() ? m.def.type : m.meta.id);
    meta.set("name", m.meta.name.empty() ? m.def.title : m.meta.name);
    meta.set("category", m.meta.category.empty() ? m.def.category : m.meta.category);
    if (!m.meta.datasheet.empty()) meta.set("datasheet", m.meta.datasheet);
    if (!m.meta.author.empty()) meta.set("author", m.meta.author);
    if (!m.meta.created.empty()) meta.set("created", m.meta.created);
    meta.set("source", m.meta.source.empty() ? (m.block.empty() ? "json" : "block") : m.meta.source);
    if (!m.meta.notes.empty()) meta.set("notes", m.meta.notes);
    if (!m.meta.tags.empty()) {
        Json tags = Json::array();
        for (auto& t : m.meta.tags) tags.push(t);
        meta.set("tags", tags);
    }
    if (m.meta.replaces) meta.set("replaces", true);
    j.set("meta", meta);
    j.set("part", partDefToJson(m.def));
    if (!m.block.empty()) j.set("block", m.block);
    return j;
}

bool partModelFromJson(const Json& j, PartModel& out, std::string* err) {
    out = PartModel();
    if (!j.isObject()) {
        if (err) *err = "a part model is a JSON object";
        return false;
    }
    const Json& mj = j["meta"];
    if (mj.isObject()) {
        out.meta.id = mj["id"].str();
        out.meta.name = mj["name"].str();
        out.meta.category = mj["category"].str();
        out.meta.datasheet = mj["datasheet"].str();
        out.meta.author = mj["author"].str();
        out.meta.created = mj["created"].str();
        out.meta.source = mj["source"].str();
        out.meta.notes = mj["notes"].str();
        out.meta.replaces = mj["replaces"].boolean();
        if (mj["tags"].isArray()) {
            for (auto& t : mj["tags"].items())
                if (t.isString() && !t.str().empty()) out.meta.tags.push_back(t.str());
        } else {
            std::string raw = mj["tags"].str();
            std::string cur;
            for (char c : raw) {
                if (c == ',' || c == ' ' || c == '\t') {
                    if (!cur.empty()) out.meta.tags.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()) out.meta.tags.push_back(cur);
        }
    }
    out.block = j["block"].str();

    const Json& pj = j.has("part") ? j["part"] : j;
    if (!pj["type"].str().empty()) {
        if (!partDefFromJson(pj, out.def, err)) return false;
    } else if (!out.block.empty()) {
        if (!plib::buildPartFromBlock(out.block, out.def, err)) return false;
    } else {
        if (err) *err = "the file has no part definition (\"part\") and no datasheet block (\"block\")";
        return false;
    }

    // Fill in whichever half was left out; the definition wins for the fields
    // the app actually renders.
    if (out.def.type.empty()) out.def.type = partModelSlug(out.meta.id.empty() ? out.meta.name : out.meta.id);
    out.meta.id = out.def.type;  // the definition is what the app registers under
    if (out.def.title.empty()) out.def.title = out.meta.name.empty() ? out.meta.id : out.meta.name;
    if (out.meta.name.empty()) out.meta.name = out.def.title;
    if (!out.meta.category.empty()) out.def.category = out.meta.category;
    else if (!out.def.category.empty()) out.meta.category = out.def.category;
    else out.meta.category = out.def.category = "Custom";
    if (!out.meta.datasheet.empty()) out.def.datasheet = out.meta.datasheet;
    else out.meta.datasheet = out.def.datasheet;
    if (out.def.tags.empty() && !out.meta.tags.empty()) out.def.tags = joinTags(out.meta.tags);
    if (out.meta.source.empty()) out.meta.source = out.block.empty() ? "json" : "block";
    if (out.def.silkscreen.empty()) out.def.silkscreen = out.def.title;
    return true;
}

bool readPartModelFile(const std::string& path, PartModel& out, std::string* err) {
    bool ok = false;
    std::string text = readTextFile(path, &ok);
    if (!ok) {
        if (err) *err = "could not read " + path + ": " + SDL_GetError();
        return false;
    }
    std::string perr;
    Json j = Json::parse(text, &perr);
    if (!perr.empty()) {
        if (err) *err = "not valid JSON: " + perr;
        return false;
    }
    if (!partModelFromJson(j, out, err)) return false;
    out.path = path;
    return true;
}

bool writePartModelFile(const std::string& path, const PartModel& m, std::string* err) {
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::string text = partModelToJson(m).dump(1);
    if (!writeTextFile(path, text)) {
        if (err) *err = "could not write " + path + ": " + SDL_GetError();
        return false;
    }
    return true;
}

bool checkPartModel(const PartModel& m, std::vector<std::string>& errs, std::vector<std::string>& warnings) {
    const PartDef& d = m.def;
    std::string slug = partModelSlug(d.type);
    if (d.type.empty()) errs.push_back("the model needs a type id");
    else if (slug != d.type || slug.empty())
        errs.push_back("type \"" + d.type + "\" is not a valid id: use lower case letters, digits and _ (e.g. " +
                       (slug.empty() ? std::string("mypart") : slug) + ")");
    if (d.title.empty()) errs.push_back("the model needs a title");
    if (d.pins.empty()) errs.push_back("the model has no pins");
    if (!d.views) errs.push_back("the model is not shown in any view (breadboard, schematic or pcb)");

    bool uniqueIds = !d.pins.empty();
    std::set<std::string> ids;
    for (auto& p : d.pins) {
        if (p.id.empty()) {
            errs.push_back("a pin has no id");
            uniqueIds = false;
            continue;
        }
        if (!ids.insert(p.id).second) {
            errs.push_back("duplicate pin id \"" + p.id + "\"");
            uniqueIds = false;
        }
        if (p.pad <= 0) errs.push_back("pin " + p.id + ": pad size must be positive");
        if (p.drill < 0) errs.push_back("pin " + p.id + ": drill can't be negative");
        for (int v = 0; v < NUM_VIEWS; v++)
            if (!std::isfinite(p.pos[v].x) || !std::isfinite(p.pos[v].y))
                errs.push_back("pin " + p.id + ": position in " + viewName(v) + " is not a number");
    }
    for (size_t b = 0; b < d.buses.size(); b++) {
        if (d.buses[b].size() < 2) errs.push_back("bus " + std::to_string(b + 1) + " needs at least two pins");
        for (int i : d.buses[b])
            if (i < 0 || i >= (int)d.pins.size()) {
                errs.push_back("bus " + std::to_string(b + 1) + " points at a pin that doesn't exist");
                break;
            }
    }
    for (int v = 0; v < NUM_VIEWS; v++) {
        bool bad = false;
        for (auto& g : d.gfx[v]) {
            if (!std::isfinite(g.a.x) || !std::isfinite(g.a.y) || !std::isfinite(g.b.x) || !std::isfinite(g.b.y) || !std::isfinite(g.r)) {
                errs.push_back(std::string("a drawing element in ") + viewName(v) + " has a bad coordinate");
                bad = true;
                break;
            }
            for (auto& q : g.pts)
                if (!std::isfinite(q.x) || !std::isfinite(q.y)) {
                    errs.push_back(std::string("a polygon in ") + viewName(v) + " has a bad point");
                    bad = true;
                    break;
                }
            if (bad) break;
        }
    }
    if (uniqueIds) {
        std::string bad = plib::padProblem(d);
        if (!bad.empty()) errs.push_back(bad);
    }
    if (d.sim.empty()) warnings.push_back("no simulator model: the part can be wired, routed and exported, but not simulated");
    if (d.inView(BB) && d.gfx[BB].empty()) warnings.push_back("no breadboard drawing: it will look like a plain box on the breadboard");
    if (d.inView(SCH) && d.gfx[SCH].empty()) warnings.push_back("no schematic symbol: pins will be drawn in a bare rectangle");
    return errs.empty();
}

bool installPartModel(const PartModel& m, bool overwrite, std::string* err, std::vector<std::string>* warnings) {
    std::vector<std::string> errs, warns;
    if (!checkPartModel(m, errs, warns)) {
        if (err) *err = errs.front();
        return false;
    }
    const std::string& type = m.def.type;
    if (isCustomType(type) && !overwrite) {
        if (err) *err = "a model with type \"" + type + "\" is already installed (pass overwrite = true to replace it)";
        return false;
    }
    if (!isCustomType(type) && isKnownType(type) && !m.meta.replaces) {
        if (err) *err = "\"" + type + "\" is a built-in part; set \"replaces\": true in the model to override it";
        return false;
    }
    PartModel stored = m;
    stored.path = partModelPathFor(type);
    if (stored.meta.id.empty()) stored.meta.id = type;
    if (stored.meta.name.empty()) stored.meta.name = m.def.title;
    if (!writePartModelFile(stored.path, stored, err)) return false;
    registerCustomPart(stored.def);
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_models[type] = stored;
    }
    if (warnings) *warnings = warns;
    return true;
}

bool removePartModel(const std::string& type, std::string* err) {
    std::string path;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        auto it = g_models.find(type);
        if (it == g_models.end()) {
            if (err) *err = "no user model \"" + type + "\" (built-in parts can't be deleted)";
            return false;
        }
        path = it->second.path;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
        if (err) *err = "could not delete " + path + ": " + ec.message();
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_models.erase(type);
    }
    unregisterCustomPart(type);
    return true;
}

int loadUserPartModels(std::vector<std::string>* problems) {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_loaded) return (int)g_models.size();
        g_loaded = true;
    }
    if (std::getenv("DE_NO_USER_PARTS")) return 0;
    std::string dir = partModelDir();
    if (dir.empty()) return 0;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return 0;
    std::vector<std::filesystem::path> files;
    for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".dezpart") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    int n = 0;
    for (auto& f : files) {
        PartModel m;
        std::string err;
        std::string name = f.filename().string();
        if (!readPartModelFile(f.string(), m, &err)) {
            if (problems) problems->push_back(name + ": " + err);
            continue;
        }
        std::vector<std::string> errs, warns;
        if (!checkPartModel(m, errs, warns)) {
            if (problems) problems->push_back(name + ": " + errs.front());
            continue;
        }
        m.path = f.string();
        registerCustomPart(m.def);
        {
            std::lock_guard<std::mutex> lk(g_mutex);
            g_models[m.def.type] = m;
        }
        n++;
    }
    return n;
}

void reloadUserPartModels() {
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        g_loaded = false;
        g_models.clear();
    }
    loadUserPartModels();
}

std::vector<PartModelMeta> userPartModelMetas() {
    std::vector<PartModelMeta> out;
    std::lock_guard<std::mutex> lk(g_mutex);
    for (auto& kv : g_models) out.push_back(kv.second.meta);
    std::sort(out.begin(), out.end(), [](const PartModelMeta& a, const PartModelMeta& b) {
        return std::tie(a.name, a.id) < std::tie(b.name, b.id);
    });
    return out;
}

bool userPartModel(const std::string& type, PartModel& out) {
    std::lock_guard<std::mutex> lk(g_mutex);
    auto it = g_models.find(type);
    if (it == g_models.end()) return false;
    out = it->second;
    return true;
}
