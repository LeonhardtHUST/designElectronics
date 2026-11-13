#include "ngspice_backend.h"
#include "tools.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <vector>

#if DE_NGSPICE_DESKTOP
namespace {
struct Case {
    std::string name, observable;
    Document doc;
    Connectivity conn;
    int part = 0, pin = 0;
};

void wire(Document& d, int a, int ap, int b, int bp) {
    Wire w;
    w.id = d.newId();
    w.view = SCH;
    w.ends[0] = {a, ap};
    w.ends[1] = {b, bp};
    w.pts = {{0, 0}, {100, 0}};
    d.wires.push_back(w);
}

Case makeCase(const std::string& name, const std::string& observable,
              const std::string& value, const std::string& type, int pin = 1,
              const Props& extra = {}) {
    Case c;
    c.name = name;
    c.observable = observable;
    c.doc.title = "SPICE xcheck " + name;
    auto& bat = c.doc.addPart("battery", {{"voltage", "9V"}}, {0, 0});
    auto& r = c.doc.addPart("resistor", {{"resistance", value}}, {500, 0});
    auto& load = c.doc.addPart(type, extra, {1000, 0});
    wire(c.doc, bat.id, 1, r.id, 0);
    wire(c.doc, r.id, 1, load.id, 0);
    wire(c.doc, load.id, pin, bat.id, 0);
    c.part = load.id;
    c.pin = pin;
    c.conn.build(c.doc);
    return c;
}

std::vector<Case> fixtures() {
    std::vector<Case> v;
    v.push_back(makeCase("Resistor divider", "V(load)", "1kΩ", "resistor", 1));
    v.push_back(makeCase("RC charging", "V(cap)", "10kΩ", "capacitor_ceramic", 1, {{"capacitance", "1µF"}}));
    v.push_back(makeCase("Red LED limiter", "V(LED)", "470Ω", "led", 1, {{"color", "Red"}}));
    v.push_back(makeCase("Green LED limiter", "V(LED)", "470Ω", "led", 1, {{"color", "Green"}}));
    v.push_back(makeCase("Blue LED limiter", "V(LED)", "470Ω", "led", 1, {{"color", "Blue"}}));
    v.push_back(makeCase("Diode clamp", "V(diode)", "1kΩ", "diode", 1));
    v.push_back(makeCase("NPN switch", "V(collector)", "2.2kΩ", "transistor", 2, {{"polarity", "NPN"}}));
    v.push_back(makeCase("PNP high side", "V(emitter)", "2.2kΩ", "transistor", 0, {{"polarity", "PNP"}, {"part", "2N3906"}}));
    v.push_back(makeCase("NMOS low side", "V(drain)", "1kΩ", "mosfet", 1));
    v.push_back(makeCase("7805 regulator", "V(out)", "1kΩ", "regulator", 2, {{"part", "7805"}, {"voltage", "5V"}}));
    v.push_back(makeCase("RL inductor", "V(inductor)", "100Ω", "inductor", 1, {{"inductance", "10mH"}}));
    v.push_back(makeCase("Photocell divider", "V(sense)", "10kΩ", "photocell", 1, {{"light", "50"}}));
    v.push_back(makeCase("Potentiometer", "V(wiper)", "10kΩ", "potentiometer", 2, {{"resistance", "10kΩ"}, {"wiper", "50"}}));
    return v;
}

bool observed(const Case& c, const AnalysisResult& ar, double& value) {
    int net = c.conn.netOf(c.part, c.pin);
    auto it = ar.netV.find(net);
    if (it == ar.netV.end() || it->second.empty()) return false;
    value = it->second.back();
    return std::isfinite(value);
}

struct Row { std::string name, obs, rt, spice, abs, rel, verdict; double relValue = -1; };
}

int spiceCrossCheck(const std::string& outMarkdownPath) {
    std::ofstream out(outMarkdownPath, std::ios::binary);
    if (!out) return 2;
    std::string why;
    bool available = ngspiceAvailable(&why);
    std::vector<Row> rows;
    for (auto& c : fixtures()) {
        SimResult sr = simulate(c.doc, c.conn, {}, 0.5);
        double rt = std::numeric_limits<double>::quiet_NaN();
        int net = c.conn.netOf(c.part, c.pin);
        if (net >= 0 && net < (int)sr.netVoltage.size()) rt = sr.netVoltage[(size_t)net];
        AnalysisResult ar;
        SpiceOptions opt;
        opt.analysis = SpiceOptions::TRAN;
        opt.tstep = 1e-3;
        opt.tstop = 0.5;
        bool ok = available && runSpiceAnalysis(c.doc, c.conn, opt, ar);
        double sp = std::numeric_limits<double>::quiet_NaN();
        ok = ok && ar.ok && observed(c, ar, sp) && std::isfinite(rt);
        Row row;
        row.name = c.name; row.obs = c.observable;
        if (!ok) {
            row.rt = std::isfinite(rt) ? (std::ostringstream{} << std::fixed << std::setprecision(6) << rt).str() : "error";
            row.spice = ar.error.empty() ? "error" : "error: " + ar.error;
            row.abs = row.rel = "—"; row.verdict = available ? "error" : "skipped";
        } else {
            double ae = std::fabs(rt - sp);
            double re = ae / std::max(std::fabs(sp), 1e-12);
            row.rt = (std::ostringstream{} << std::fixed << std::setprecision(6) << rt).str();
            row.spice = (std::ostringstream{} << std::fixed << std::setprecision(6) << sp).str();
            row.abs = (std::ostringstream{} << std::fixed << std::setprecision(6) << ae).str();
            row.rel = (std::ostringstream{} << std::fixed << std::setprecision(2) << (100.0 * re) << "%").str();
            row.relValue = re;
            row.verdict = re < .02 ? "OK" : re < .10 ? "WARN" : "BAD";
        }
        rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.relValue > b.relValue; });
    out << "# SPICE Cross-check\n\nGenerated by `--spice-xcheck`; values come only from `simulate()` and `runSpiceAnalysis()`.\n\n";
    out << "Backend: `" << (available ? "available" : "error") << "`";
    if (!available && !why.empty()) out << " (" << why << ")";
    out << "\n\n| Circuit | Observable | Real-time | ngspice | Absolute error | Relative error | 判定 |\n|---|---|---:|---:|---:|---:|---|\n";
    for (const auto& r : rows) out << "| " << r.name << " | " << r.obs << " | " << r.rt << " | " << r.spice << " | " << r.abs << " | " << r.rel << " | `" << r.verdict << "` |\n";
    out << "\n## Top 5 suspected deviation sources\n\n";
    out << "1. Inductor model uses fixed 50 mΩ series resistance in `sim.cpp`; RL rows can dominate.\n";
    out << "2. LED/diode piecewise-linear fixed Vf (~0.7 V) differs from ngspice exponential junctions.\n";
    out << "3. Capacitor backward-Euler stepping at 1 ms alters RC transient/steady-state observations.\n";
    out << "4. BJT uses simplified beta/gm and saturation rather than a full SPICE Ebers–Moll model.\n";
    out << "5. MOSFET/regulator models use fixed Ron, threshold and dropout values unlike vendor SPICE models.\n";
    return 0;
}
#else
int spiceCrossCheck(const std::string&) { return 2; }
#endif
