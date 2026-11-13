#include "colorcode.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "partlib.h"

namespace cc {

namespace {

const char* kKeys[COLOR_COUNT] = {"black", "brown", "red", "orange", "yellow", "green", "blue", "violet", "grey", "white", "gold", "silver", "none"};

const uint32_t kRgba[COLOR_COUNT] = {rgba(20, 20, 20),    rgba(120, 70, 30),   rgba(210, 30, 30),  rgba(240, 130, 20), rgba(240, 220, 30),
         rgba(30, 160, 60),   rgba(30, 80, 210),   rgba(140, 60, 190), rgba(130, 130, 130), rgba(245, 245, 245),
         rgba(200, 160, 40),  rgba(200, 200, 205), rgba(0, 0, 0, 0)};

struct TolEntry { int color; double pct; };
const TolEntry kTol[] = {{BROWN, 1}, {RED, 2}, {GREEN, 0.5}, {BLUE, 0.25}, {VIOLET, 0.1}, {GREY, 0.05}, {GOLD, 5}, {SILVER, 10}};

struct PpmEntry { int color; int ppm; };
const PpmEntry kPpm[] = {{BLACK, 250}, {BROWN, 100}, {RED, 50}, {ORANGE, 15}, {YELLOW, 25}, {BLUE, 10}, {VIOLET, 5}, {GREY, 1}};

int tolColor(double pct) {
    for (auto& t : kTol)
 if (std::fabs(t.pct - pct) < 1e-6) return t.color;
    return NONE;  // 20 %: no tolerance band
}

int ppmColor(int ppm) {
    for (auto& t : kPpm)
 if (t.ppm == ppm) return t.color;
    return BROWN;
}

// The multiplier exponent a colour stands for: black..white = 0..9, gold -1, silver -2.
bool multExp(int color, int& e) {
    if (color >= BLACK && color <= WHITE) { e = color; return true; }
    if (color == GOLD) { e = -1; return true; }
    if (color == SILVER) { e = -2; return true; }
    return false;
}

std::string numText(double v) {
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-9) snprintf(b, sizeof b, "%.0f", v);
    else snprintf(b, sizeof b, "%g", v);
    return b;
}

}  // namespace

uint32_t colorRgba(int c) { return c >= 0 && c < COLOR_COUNT ? kRgba[c] : 0; }
const char* colorKey(int c) { return c >= 0 && c < COLOR_COUNT ? kKeys[c] : "none"; }

void bandLayout(int n, float x[6], float& width) {
    width = 0.09f;
    const float step = 0.13f;
    int mains = n >= 5 ? 4 : 3;
    int extras = n - mains;
    for (int i = 0; i < 6; i++) x[i] = 0;
    for (int i = 0; i < mains; i++) x[i] = 0.06f + (float)i * step;
    for (int j = 0; j < extras; j++) x[mains + j] = 0.94f - width - (float)(extras - 1 - j) * step;
}

Bands encode(const Spec& s, bool* exact) {
    Bands b;
    b.n = std::max(3, std::min(6, s.bands));
    int d = b.digits();
    bool ok = true;
    int digits[3] = {0, 0, 0}, mult = BLACK;
    double ohms = s.ohms;
    if (ohms > 0 && std::isfinite(ohms)) {
        double limit = d == 3 ? 1000 : 100;
        int e = (int)std::floor(std::log10(ohms)) - (d - 1);
        double sig = std::round(ohms / std::pow(10.0, e));
        if (sig >= limit) { sig = std::round(sig / 10); e++; }
        while (e < -2) { sig = std::round(sig / 10); e++; }  // below 0.01 Ω: drop digits
        if (e > 9) { e = 9; sig = std::min(limit - 1, std::round(ohms / 1e9)); }
 if (std::fabs(sig * std::pow(10.0, e) - ohms) > ohms * 1e-6) ok = false;
        long long v = (long long)sig;
        for (int i = d - 1; i >= 0; i--) { digits[i] = (int)(v % 10); v /= 10; }
 mult = e >= 0 ? e : (e == -1 ? GOLD : SILVER);
    } else if (ohms != 0) {
        ok = false;
    }
    int i = 0;
    for (; i < d; i++) { b.color[i] = digits[i]; b.role[i] = DIGIT; }
    b.color[i] = mult;
    b.role[i++] = MULTIPLIER;
    if (b.n >= 4) {
        b.color[i] = tolColor(s.tolerance);
 b.role[i++] = TOLERANCE;
    }
    if (b.n == 6) {
        b.color[i] = ppmColor(s.ppm);
        b.role[i++] = TEMPCO;
    }
    if (exact) *exact = ok;
    return b;
}

bool decode(const Bands& b, Spec& out) {
    if (b.n < 3 || b.n > 6) return false;
    int d = b.digits();
    double sig = 0;
    for (int i = 0; i < d; i++) {
        if (b.color[i] < BLACK || b.color[i] > WHITE) return false;
        sig = sig * 10 + b.color[i];
    }
    int e = 0;
    if (!multExp(b.color[d], e)) return false;
    Spec s;
    s.bands = b.n;
    s.ohms = sig * std::pow(10.0, e);
    s.tolerance = 20;
    s.ppm = 100;
    if (b.n >= 4) {
 int c = b.color[d + 1];
        if (c != NONE) {
     bool found = false;
            for (auto& t : kTol)
  if (t.color == c) { s.tolerance = t.pct; found = true; }
     if (!found) return false;
        }
    }
    if (b.n == 6) {
        int c = b.color[d + 2];
        bool found = false;
        for (auto& t : kPpm)
     if (t.color == c) { s.ppm = t.ppm; found = true; }
 if (!found) return false;
    }
    out = s;
    return true;
}

std::vector<int> allowedColors(Role r, int) {
    std::vector<int> v;
    switch (r) {
        case DIGIT:
            for (int c = BLACK; c <= WHITE; c++) v.push_back(c);
     break;
        case MULTIPLIER:
            for (int c = BLACK; c <= WHITE; c++) v.push_back(c);
     v.push_back(GOLD);
            v.push_back(SILVER);
     break;
 case TOLERANCE:
     for (auto& t : kTol) v.push_back(t.color);
     v.push_back(NONE);
     break;
 case TEMPCO:
     for (auto& t : kPpm) v.push_back(t.color);
     break;
    }
    return v;
}

std::string meaning(Role r, int color) {
    switch (r) {
        case DIGIT: return color >= BLACK && color <= WHITE ? std::to_string(color) : "?";
        case MULTIPLIER: {
            int e = 0;
            if (!multExp(color, e)) return "?";
            if (e == 0) return "×1";
            char b[32];
            if (e > 0) snprintf(b, sizeof b, "×10^%d", e);
            else snprintf(b, sizeof b, "×0.%0*d", -e - 1, 1);
            return b;
        }
 case TOLERANCE:
     for (auto& t : kTol)
         if (t.color == color) return "±" + numText(t.pct) + "%";
     return color == NONE ? "±20%" : "?";
        case TEMPCO:
     for (auto& t : kPpm)
         if (t.color == color) return numText(t.ppm) + "ppm";
     return "?";
    }
    return "?";
}

const std::vector<double>& toleranceChoices() {
    static const std::vector<double> v = {0.05, 0.1, 0.25, 0.5, 1, 2, 5, 10, 20};
    return v;
}
const std::vector<int>& tempcoChoices() {
    static const std::vector<int> v = {250, 100, 50, 25, 15, 10, 5, 1};
    return v;
}

double snapTolerance(double pct) {
    double best = 5, bd = 1e30;
    for (double t : toleranceChoices())
        if (std::fabs(t - pct) < bd) { bd = std::fabs(t - pct); best = t; }
    return best;
}
int snapTempco(int ppm) {
    int best = 100, bd = 1 << 30;
    for (int t : tempcoChoices())
        if (std::abs(t - ppm) < bd) { bd = std::abs(t - ppm); best = t; }
    return best;
}

double parseTolerance(const std::string& s, double def) {
    const char* p = s.c_str();
    while (*p && !(std::isdigit((unsigned char)*p) || *p == '.')) p++;
    char* end = nullptr;
    double v = strtod(p, &end);
    return end == p ? def : v;
}
std::string formatTolerance(double pct) { return "±" + numText(pct) + "%"; }

int parsePpm(const std::string& s, int def) {
    int v = std::atoi(s.c_str());
    return v > 0 ? v : def;
}
std::string formatPpm(int ppm) { return std::to_string(ppm) + "ppm"; }

std::string formatOhms(double ohms) {
    struct P { double m; const char* s; } ps[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}, {1, ""}};
    double a = std::fabs(ohms);
    char b[48];
    if (a < 1e-12) return "0Ω";
    for (auto& p : ps) {
 if (a >= p.m * 0.9999995) {
            snprintf(b, sizeof b, "%.6g%s", ohms / p.m, p.s);
     return std::string(b) + "Ω";
        }
    }
    snprintf(b, sizeof b, "%.6g", ohms);
    return std::string(b) + "Ω";
}

double parseResistance(const std::string& text, double def) {
    // "4k7" / "4R7" / "R47" / "2M2": the letter is both the unit prefix and the decimal point
    std::string t;
    for (char c : text)
 if (c != ' ') t += c;
    for (size_t i = 0; i < t.size(); i++) {
        char c = t[i];
        bool letter = c == 'k' || c == 'K' || c == 'M' || c == 'G' || c == 'R' || c == 'r';
        bool digitAfter = i + 1 < t.size() && std::isdigit((unsigned char)t[i + 1]);
        bool digitBefore = i > 0 && std::isdigit((unsigned char)t[i - 1]);
        if (letter && digitAfter && (digitBefore || c == 'R' || c == 'r')) {
     double mult = (c == 'k' || c == 'K') ? 1e3 : c == 'M' ? 1e6 : c == 'G' ? 1e9 : 1;
     std::string n = t.substr(0, i) + "." + t.substr(i + 1);
     if (n[0] == '.') n = "0" + n;
     char* end = nullptr;
     double v = strtod(n.c_str(), &end);
     if (end == n.c_str()) return def;
     return v * mult;
        }
    }
    double v = parseSI(t, -1);
    return std::isfinite(v) && v >= 0 ? v : def;
}

std::string resistorMarking(double ohms, double tolerancePct) {
    if (!(ohms > 0)) return "0";
    int k = tolerancePct <= 1 ? 3 : 2;  // significant digits: precision parts print four characters
    int e = (int)std::floor(std::log10(ohms)) - (k - 1);
    double limit = k == 3 ? 1000 : 100;
    double sig = std::round(ohms / std::pow(10.0, e));
    if (sig >= limit) { sig = std::round(sig / 10); e++; }
    std::string digits = std::to_string((long long)sig);
    while ((int)digits.size() < k) digits = "0" + digits;
    if (e >= 0) return e <= 9 ? digits + std::to_string(e) : formatOhms(ohms);
    int intLen = k + e;  // digits before the R
    if (intLen <= 0) return "R" + std::string((size_t)std::max(0, -intLen - 1), '0') + digits;
    return digits.substr(0, (size_t)intLen) + "R" + digits.substr((size_t)intLen);
}

std::string capacitorMarking(double farads) {
    double pf = farads * 1e12;
    if (!(pf >= 1)) return formatSI(farads, "F");
    if (pf < 100) {
        if (std::fabs(pf - std::round(pf)) < 1e-6) return std::to_string((long long)std::llround(pf));
        char b[24];
        double w = std::floor(pf);
        snprintf(b, sizeof b, "%lldp%lld", (long long)w, (long long)std::llround((pf - w) * 10));
        return b;
    }
    int e = (int)std::floor(std::log10(pf)) - 1;
    double sig = std::round(pf / std::pow(10.0, e));
    if (sig >= 100) { sig = std::round(sig / 10); e++; }
    if (e > 9) return formatSI(farads, "F");
    return std::to_string((long long)sig) + std::to_string(e);
}

Spec specFromProps(const std::map<std::string, std::string>& props) {
    auto get = [&](const char* k, const char* def) {
        auto it = props.find(k);
        return it == props.end() ? std::string(def) : it->second;
    };
    Spec s;
    s.bands = std::max(3, std::min(6, std::atoi(get("bands", "4").c_str())));
    s.ohms = parseResistance(get("resistance", "220"), 220);
    s.tolerance = snapTolerance(parseTolerance(get("tolerance", "±5%"), 5));
    s.ppm = snapTempco(parsePpm(get("tempco", "100ppm"), 100));
    return s;
}

std::map<std::string, std::string> propsFromSpec(const Spec& s) {
    std::map<std::string, std::string> p;
    p["resistance"] = formatOhms(s.ohms);
    p["bands"] = std::to_string(s.bands);
    p["tolerance"] = formatTolerance(s.tolerance);
    p["tempco"] = formatPpm(s.ppm);
    return p;
}

bool sameSpec(const Spec& a, const Spec& b) {
    return a.bands == b.bands && std::fabs(a.ohms - b.ohms) <= std::fabs(a.ohms) * 1e-9 && std::fabs(a.tolerance - b.tolerance) < 1e-9 && a.ppm == b.ppm;
}

}  // namespace cc
