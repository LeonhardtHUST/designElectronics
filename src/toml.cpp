// designElectronics — a small TOML subset (see toml.h).
#include "toml.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <vector>

namespace toml {
namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

bool isKeyChar(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.'; }

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        size_t nl = s.find('\n', i);
        if (nl == std::string::npos) { out.push_back(s.substr(i)); break; }
        out.push_back(s.substr(i, nl - i));
        i = nl + 1;
    }
    return out;
}

// Index just past the value that starts at `i`, i.e. where a trailing `#` comment begins.
size_t valueEnd(const std::string& line, size_t i) {
    bool dq = false, sq = false;
    for (; i < line.size(); i++) {
        char c = line[i];
        if (dq) {
            if (c == '\\') { i++; continue; }
            if (c == '"') dq = false;
            continue;
        }
        if (sq) {
            if (c == '\'') sq = false;
            continue;
        }
        if (c == '"') { dq = true; continue; }
        if (c == '\'') { sq = true; continue; }
        if (c == '#') return i;
    }
    return line.size();
}

// Is `val` one of the forms this subset accepts?
bool validValue(const std::string& val) {
    if (val.empty()) return false;
    if (val.front() == '"') return val.size() >= 2 && val.back() == '"';
    if (val.front() == '\'') return val.size() >= 2 && val.back() == '\'';
    if (val.front() == '[') return val.size() >= 2 && val.back() == ']';
    if (val == "true" || val == "false") return true;
    // A number: the whole text has to be consumed ("1.5", "-3", "1e6", "0x40").
    const char* p = val.c_str();
    char* end = nullptr;
    std::strtod(p, &end);
    return end && end != p && *end == '\0';
}

}  // namespace

std::string quote(const std::string& raw) {
    std::string out = "\"";
    for (char c : raw) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    out += "\"";
    return out;
}

std::string unquote(const std::string& raw) {
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        std::string out;
        for (size_t i = 1; i + 1 < raw.size(); i++) {
            if (raw[i] == '\\' && i + 2 < raw.size()) {
                char c = raw[++i];
                switch (c) {
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    default: out += c; break;
                }
            } else {
                out += raw[i];
            }
        }
        return out;
    }
    if (raw.size() >= 2 && raw.front() == '\'' && raw.back() == '\'') return raw.substr(1, raw.size() - 2);
    return raw;
}

bool Doc::parse(const std::string& src, std::string* err) {
    text = src;
    v.clear();
    std::string section;
    std::vector<std::string> lines = splitLines(src);
    for (size_t ln = 0; ln < lines.size(); ln++) {
        std::string line = lines[ln];
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t[0] == '[') {
            size_t close = t.find(']');
            if (close == std::string::npos) {
                if (err) *err = "line " + std::to_string(ln + 1) + ": section header is missing ']'";
                return false;
            }
            section = trim(t.substr(1, close - 1));
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) {
            if (err) *err = "line " + std::to_string(ln + 1) + ": expected key = value";
            return false;
        }
        std::string key = trim(t.substr(0, eq));
        if (key.empty()) {
            if (err) *err = "line " + std::to_string(ln + 1) + ": the key is missing";
            return false;
        }
        for (char c : key)
            if (!isKeyChar(c)) {
                if (err) *err = "line " + std::to_string(ln + 1) + ": '" + key + "' is not a valid key";
                return false;
            }
        size_t start = eq + 1;
        while (start < t.size() && (t[start] == ' ' || t[start] == '\t')) start++;
        std::string value = trim(t.substr(start, valueEnd(t, start) - start));
        if (!validValue(value)) {
            if (err) *err = "line " + std::to_string(ln + 1) + ": '" + value + "' is not a value this build understands";
            return false;
        }
        v[section.empty() ? key : section + "." + key] = value;
    }
    return true;
}

std::string Doc::get(const std::string& key, const std::string& def) const {
    auto it = v.find(key);
    return it == v.end() ? def : unquote(it->second);
}

long long Doc::getInt(const std::string& key, long long def) const {
    std::string s = get(key);
    if (s.empty()) return def;
    const char* p = s.c_str();
    char* end = nullptr;
    long long n = std::strtoll(p, &end, 0);
    return end && end != p ? n : def;
}

bool Doc::getBool(const std::string& key, bool def) const {
    std::string s = get(key);
    if (s == "true" || s == "1") return true;
    if (s == "false" || s == "0") return false;
    return def;
}

void Doc::set(const std::string& key, const std::string& value) { v[key] = quote(value); }
void Doc::setInt(const std::string& key, long long value) { v[key] = std::to_string(value); }
void Doc::setBool(const std::string& key, bool value) { v[key] = value ? "true" : "false"; }

std::string Doc::dump() const {
    std::vector<std::string> out;
    std::map<std::string, int> sectionEnd;  // section -> index just past its last line
    std::set<std::string> seen;
    std::string section;
    for (auto& raw : splitLines(text)) {
        std::string t = trim(raw);
        if (!t.empty() && t[0] == '[') {
            size_t close = t.find(']');
            section = close == std::string::npos ? std::string() : trim(t.substr(1, close - 1));
            out.push_back(raw);
            sectionEnd[section] = (int)out.size();
            continue;
        }
        if (!t.empty() && t[0] != '#') {
            size_t eq = raw.find('=');
            if (eq != std::string::npos) {
                std::string k = trim(raw.substr(0, eq));
                std::string full = section.empty() ? k : section + "." + k;
                auto it = v.find(full);
                if (it != v.end()) {
                    size_t start = eq + 1;
                    while (start < raw.size() && (raw[start] == ' ' || raw[start] == '\t')) start++;
                    // Keep the indentation, the key, the spaces before a comment and the
                    // comment itself exactly as they were.
                    size_t stop = valueEnd(raw, start);
                    size_t valueStop = stop;
                    while (valueStop > start && (raw[valueStop - 1] == ' ' || raw[valueStop - 1] == '\t')) valueStop--;
                    out.push_back(raw.substr(0, start) + it->second + raw.substr(valueStop));
                    seen.insert(full);
                    sectionEnd[section] = (int)out.size();
                    continue;
                }
            }
        }
        out.push_back(raw);
        sectionEnd[section] = (int)out.size();
    }

    std::map<int, std::vector<std::string>> inserts;               // line index -> lines
    std::map<std::string, std::vector<std::string>> newSections;  // section -> lines
    for (auto& kv : v) {
        if (seen.count(kv.first)) continue;
        size_t dot = kv.first.rfind('.');
        std::string sec = dot == std::string::npos ? std::string() : kv.first.substr(0, dot);
        std::string leaf = dot == std::string::npos ? kv.first : kv.first.substr(dot + 1);
        std::string line = leaf + " = " + kv.second;
        auto it = sectionEnd.find(sec);
        if (it != sectionEnd.end()) inserts[it->second].push_back(line);
        else if (sec.empty()) inserts[(int)out.size()].push_back(line);
        else newSections[sec].push_back(line);
    }
    for (auto it = inserts.rbegin(); it != inserts.rend(); ++it) {
        size_t at = std::min((size_t)std::max(0, it->first), out.size());
        out.insert(out.begin() + (long)at, it->second.begin(), it->second.end());
    }

    std::string result;
    for (size_t i = 0; i < out.size(); i++) {
        if (i) result += "\n";
        result += out[i];
    }
    for (auto& kv : newSections) {
        if (!result.empty() && result.back() != '\n') result += "\n";
        result += "\n[" + kv.first + "]\n";
        for (auto& line : kv.second) {
            result += line;
            result += "\n";
        }
    }
    if (!text.empty() && text.back() == '\n' && (result.empty() || result.back() != '\n')) result += "\n";
    if (!result.empty() && result.back() != '\n') result += "\n";  // a text file ends with a newline
    return result;
}

}  // namespace toml
