// designElectronics — user preferences, stored as config.toml in the per-user
// preferences folder.
//
// TOML because the file is meant to be readable and editable by hand: comments and
// keys this build does not know are kept byte for byte across writes. A file from an
// older version (settings.json) is migrated once, and a damaged file falls back to
// config.toml.bak and then to the built-in defaults, so the editor always starts.
#pragma once
#include <string>

// e.g. ".../AppData/Roaming/designElectronics/designElectronics/config.toml"
std::string settingsPath();

// Reads the file (migrating an old settings.json first if that is all there is).
// Call once, before i18n::init() — the stored language decides the interface.
void settingsInit();

// Translation key of a problem worth telling the user about, "" when all is well.
// It is a key, not a sentence, because settingsInit() runs before the language is
// chosen: the caller passes it through tr() when it shows the toast.
std::string settingsWarning();

// Missing file or key returns `def`.
std::string settingsGet(const char* key, const std::string& def = "");
long long settingsGetInt(const char* key, long long def = 0);
int normalizedUIFontSize(long long value);
bool settingsBool(const char* key, bool def = false);

// Write-modify-write; creates the folder if needed and keeps config.toml.bak.
void settingsSet(const char* key, const std::string& value);
void settingsSetInt(const char* key, long long value);
void settingsSetBool(const char* key, bool value);

// Turns an old settings.json into the text of a config.toml, prefixed with a comment
// that says where it came from. Exposed for --selftest, which must not touch the
// user's real preferences.
std::string settingsMigrateJson(const std::string& jsonText, bool* ok = nullptr);
