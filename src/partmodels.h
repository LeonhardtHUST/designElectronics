// designElectronics — part model files (.dezpart) and the user model library.
//
// A model is a PartDef plus metadata (name, datasheet, tags, author…). Models
// live as one JSON file per part under <pref>/parts, are registered at startup
// and show up in the parts bin under "Custom". Project files still embed the
// definitions they use, so a shared .dez opens anywhere.
#pragma once
#include <string>
#include <vector>

#include "partlib.h"

struct PartModelMeta {
    std::string id;         // type id, e.g. "74hc03"
    std::string name;       // display name; defaults to the definition's title
    std::string category;
    std::string datasheet;
    std::string author;
    std::string created;    // ISO date
    std::string source;     // "block" when built from a datasheet block, else "json"
    std::string notes;
    std::vector<std::string> tags;
    bool replaces = false;  // allowed to shadow a built-in type
};

struct PartModel {
    PartModelMeta meta;
    PartDef def;
    std::string block;  // datasheet block source, when the model came from one
    std::string path;   // file it was read from / installed as
};

std::string partModelDir();
std::string partModelPathFor(const std::string& type);
std::string partModelSlug(const std::string& s);  // "74HC03 Quad NAND" -> "74hc03_quad_nand"

// Reads the v2 wrapper {format,version,meta,part,block}, the older
// {format,version,part} and a bare PartDef. A document may carry only a
// datasheet block, which is expanded into a full definition.
bool partModelFromJson(const Json& j, PartModel& out, std::string* err);
Json partModelToJson(const PartModel& m);

bool readPartModelFile(const std::string& path, PartModel& out, std::string* err);
bool writePartModelFile(const std::string& path, const PartModel& m, std::string* err);

// Structural checks: pins, buses, coordinates, pad overlaps. Hard problems go
// to `errs`; things that still work (no simulator model, no drawing) to `warnings`.
bool checkPartModel(const PartModel& m, std::vector<std::string>& errs, std::vector<std::string>& warnings);

// Validates, writes <dir>/<type>.dezpart and registers the model. Refuses to
// shadow a built-in unless meta.replaces, and an installed model unless overwrite.
bool installPartModel(const PartModel& m, bool overwrite, std::string* err, std::vector<std::string>* warnings = nullptr);
bool removePartModel(const std::string& type, std::string* err);

// Scans the library once. Problems (unreadable files, failed checks) are reported
// and the model is skipped. Set DE_NO_USER_PARTS=1 to ignore the library.
int loadUserPartModels(std::vector<std::string>* problems = nullptr);
// Re-scan after the registry was cleared (e.g. "New design" drops embedded parts).
void reloadUserPartModels();
std::vector<PartModelMeta> userPartModelMetas();
bool userPartModel(const std::string& type, PartModel& out);
