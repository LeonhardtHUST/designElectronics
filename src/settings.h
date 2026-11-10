// designElectronics — small persisted preferences (settings.json in the per-user
// preferences folder). Only the interface language uses it today; the file is a
// plain JSON object so more can be added without a migration.
#pragma once
#include <string>

// e.g. ".../AppData/Roaming/designElectronics/designElectronics/settings.json"
std::string settingsPath();
// Missing file/key returns `def`.
std::string settingsGet(const char* key, const std::string& def = "");
// Read-modify-write; creates the folder if needed.
void settingsSet(const char* key, const std::string& value);
