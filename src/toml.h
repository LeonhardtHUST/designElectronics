// designElectronics — a small TOML subset for the preferences file (config.toml).
//
// Only what a preferences file needs: `[section]` headers, `key = value` with string,
// integer, float, boolean and array values, and whole-line or trailing `#` comments.
// The original text is kept verbatim, so comments, formatting and keys written by
// other tools survive a write: set() replaces just the value of a known key, and a
// key the file does not have yet is appended to the end of its section.
//
// Keys are flat: a key inside [window] is addressed as "window.width".
#pragma once
#include <map>
#include <string>

namespace toml {

struct Doc {
    std::string text;                      // the file as it was read
    std::map<std::string, std::string> v;  // "section.key" -> raw value text (strings keep their quotes)

    // Reads `src`. On failure `err` says which line was not understood and the
    // document keeps whatever was parsed up to that point.
    bool parse(const std::string& src, std::string* err = nullptr);

    std::string get(const std::string& key, const std::string& def = "") const;
    long long getInt(const std::string& key, long long def = 0) const;
    bool getBool(const std::string& key, bool def = false) const;

    void set(const std::string& key, const std::string& value);  // stored as a quoted, escaped string
    void setInt(const std::string& key, long long value);
    void setBool(const std::string& key, bool value);

    // The text to write back: known keys replaced in place, missing keys appended.
    std::string dump() const;
};

// "a\"b" -> "\"a\\\"b\"" and back. Exposed because the settings file is hand-edited
// and the selftest checks the escaping.
std::string quote(const std::string& raw);
std::string unquote(const std::string& raw);

}  // namespace toml
