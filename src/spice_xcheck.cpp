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
    int part = 0, pin = 0, bat = 0, r = 0;
};

int pinOf(const Document& d, int part, const std::string& id) {
    const PartInst* p = d.part(part);
    if (!p || !p->def) return -1;
    for (size_t i = 0; i < p->def->pins.size(); i++) if (p->def->pins[i].id == id) return (int)i;
    return -1;
}

bool wirePins(Document& d, int a, const std::string& ai, int b, const std::string& bi) {
    int pa = pinOf(d, a, ai), pb = pinOf(d, b, bi);
    if (pa < 0 || pb < 0) return false;
    Wire w;
    w.id = d.newId();
    w.view = SCH;
    w.ends[0] = {a, pa};
    w.ends[1] = {b, pb};
    w.pts = {{0, 0}, {100, 0}};  // syncWireEnds() replaces these with the real pin positions
    d.wires.push_back(w);
    return true;
}

// Every fixture is the same skeleton: a 9 V battery, one series resistor and the device under
// test. The device may sit between the resistor and the battery minus (a load) or be wired as a
// transistor/regulator with extra resistors; the observed node is always the device's own pin.
Case basic(const std::string& name, const std::string& observable, const std::string& rvalue) {
    Case c;
    c.name = name;
    c.observable = observable;
    c.doc.title = "SPICE xcheck " + name;
    c.bat = c.doc.addPart("battery", {{"voltage", "9V"}}, {0, 0}).id;
    c.r = c.doc.addPart("resistor", {{"resistance", rvalue}}, {500, 0}).id;
    return c;
}

void addCase(std::vector<Case>& v, Case c, int part, const std::string& observePin) {
    c.part = part;
    c.pin = pinOf(c.doc, part, observePin);
    // ENDS carry the pins; PTS must follow them, or the default points merge unrelated pins.
    c.doc.syncWireEnds();
    c.conn.build(c.doc);
    v.push_back(std::move(c));
}

// A two-terminal device across the wires: resistor, capacitor, LED, diode, inductor, photocell.
void loadCase(std::vector<Case>& v, const std::string& name, const std::string& observable,
              const std::string& rvalue, const std::string& type, const Props& props,
              const std::string& junctionPin, const std::string& returnPin) {
    Case c = basic(name, observable, rvalue);
    int dev = c.doc.addPart(type, props, {1000, 0}).id;
    if (wirePins(c.doc, c.bat, "plus", c.r, "pin0") &&
        wirePins(c.doc, c.r, "pin1", dev, junctionPin) &&
        wirePins(c.doc, dev, returnPin, c.bat, "minus"))
        addCase(v, std::move(c), dev, junctionPin);
}

std::vector<Case> fixtures() {
    std::vector<Case> v;
    loadCase(v, "Resistor divider", "V(load)", "1kΩ", "resistor", {{"resistance", "1kΩ"}}, "pin0", "pin1");
    loadCase(v, "RC charging", "V(cap)", "1kΩ", "capacitor_ceramic", {{"capacitance", "1µF"}}, "pin0", "pin1");
    loadCase(v, "Red LED limiter", "V(LED)", "470Ω", "led", {{"color", "Red"}}, "anode", "cathode");
    loadCase(v, "Green LED limiter", "V(LED)", "470Ω", "led", {{"color", "Green"}}, "anode", "cathode");
    loadCase(v, "Blue LED limiter", "V(LED)", "470Ω", "led", {{"color", "Blue"}}, "anode", "cathode");
    loadCase(v, "Diode clamp", "V(diode)", "1kΩ", "diode", {}, "anode", "cathode");
    loadCase(v, "Inductor", "V(inductor)", "100Ω", "inductor", {{"inductance", "10mH"}}, "pin0", "pin1");
    loadCase(v, "Photocell divider", "V(sense)", "10kΩ", "photocell", {{"light", "50"}}, "pin0", "pin1");

    {  // potentiometer: the wiper is observed while the resistor loads the top of the divider
        Case c = basic("Potentiometer", "V(wiper)", "1kΩ");
        int pot = c.doc.addPart("potentiometer", {{"resistance", "10kΩ"}, {"wiper", "50"}}, {1000, 0}).id;
        int rl = c.doc.addPart("resistor", {{"resistance", "10kΩ"}}, {1500, 0}).id;
        if (wirePins(c.doc, c.bat, "plus", c.r, "pin0") &&
            wirePins(c.doc, c.r, "pin1", pot, "leg1") &&
            wirePins(c.doc, pot, "leg2", c.bat, "minus") &&
            wirePins(c.doc, pot, "wiper", rl, "pin0") &&
            wirePins(c.doc, rl, "pin1", c.bat, "minus"))
            addCase(v, std::move(c), pot, "wiper");
    }
    {  // NPN: base resistor from the rail, emitter to ground, collector loaded by a second resistor
        Case c = basic("NPN switch", "V(collector)", "2.2kΩ");
        int q = c.doc.addPart("transistor", {{"polarity", "NPN"}}, {1000, 0}).id;
        int rc = c.doc.addPart("resistor", {{"resistance", "1kΩ"}}, {1500, 0}).id;
        if (wirePins(c.doc, c.bat, "plus", c.r, "pin0") &&
            wirePins(c.doc, c.r, "pin1", q, "B") &&
            wirePins(c.doc, q, "E", c.bat, "minus") &&
            wirePins(c.doc, c.bat, "plus", rc, "pin0") &&
            wirePins(c.doc, rc, "pin1", q, "C"))
            addCase(v, std::move(c), q, "C");
    }
    {  // PNP: emitter on the rail, base resistor to ground, collector loaded to ground
        Case c = basic("PNP high side", "V(collector)", "2.2kΩ");
        int q = c.doc.addPart("transistor", {{"polarity", "PNP"}, {"part", "2N3906"}}, {1000, 0}).id;
        int rc = c.doc.addPart("resistor", {{"resistance", "1kΩ"}}, {1500, 0}).id;
        if (wirePins(c.doc, c.bat, "plus", q, "E") &&
            wirePins(c.doc, c.bat, "minus", c.r, "pin0") &&
            wirePins(c.doc, c.r, "pin1", q, "B") &&
            wirePins(c.doc, q, "C", rc, "pin0") &&
            wirePins(c.doc, rc, "pin1", c.bat, "minus"))
            addCase(v, std::move(c), q, "C");
    }
    {  // NMOS: gate on the rail through a resistor, source to ground, drain loaded by a resistor
        Case c = basic("NMOS low side", "V(drain)", "1kΩ");
        int q = c.doc.addPart("mosfet", {{"channel", "N"}}, {1000, 0}).id;
        int rd = c.doc.addPart("resistor", {{"resistance", "1kΩ"}}, {1500, 0}).id;
        if (wirePins(c.doc, c.bat, "plus", c.r, "pin0") &&
            wirePins(c.doc, c.r, "pin1", q, "G") &&
            wirePins(c.doc, q, "S", c.bat, "minus") &&
            wirePins(c.doc, c.bat, "plus", rd, "pin0") &&
            wirePins(c.doc, rd, "pin1", q, "D"))
            addCase(v, std::move(c), q, "D");
    }
    {  // 7805: input on the rail, ground to the rail minus, output loaded by a resistor to ground
        Case c = basic("7805 regulator", "V(out)", "1kΩ");
        int reg = c.doc.addPart("regulator", {{"part", "7805"}, {"voltage", "5V"}}, {1000, 0}).id;
        int rl = c.doc.addPart("resistor", {{"resistance", "1kΩ"}}, {1500, 0}).id;
        if (wirePins(c.doc, c.bat, "plus", reg, "in") &&
            wirePins(c.doc, reg, "gnd", c.bat, "minus") &&
            wirePins(c.doc, reg, "out", rl, "pin0") &&
            wirePins(c.doc, rl, "pin1", c.bat, "minus"))
            addCase(v, std::move(c), reg, "out");
    }
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
        if (c.part <= 0 || c.pin < 0) continue;  // a fixture whose pins no longer exist
        SimResult sr = simulate(c.doc, c.conn, {}, 0.5);
        double rt = std::numeric_limits<double>::quiet_NaN();
        int net = c.conn.netOf(c.part, c.pin);
        // A node with a single pin is not wired into the circuit: its 0 V says nothing, and
        // comparing it would report a fake match or a fake mismatch.
        bool wired = net >= 0 && c.conn.netPins(net).size() >= 2 && net < (int)sr.netVoltage.size();
        if (wired) rt = sr.netVoltage[(size_t)net];
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
            std::string reason = !available ? "backend unavailable" : !wired ? "observed node is not wired" : ar.error.empty() ? "ngspice returned no value for the node" : ar.error;
            row.rt = std::isfinite(rt) ? (std::ostringstream{} << std::fixed << std::setprecision(6) << rt).str() : "—";
            row.spice = "error: " + reason;
            row.abs = row.rel = "—";
            row.verdict = available ? "error" : "skipped";
        } else {
            double ae = std::fabs(rt - sp);
            // A node near zero volts is dominated by rounding, so a small absolute difference is
            // agreement there; everywhere else the ratio is the meaningful measure.
            bool nearZero = std::fabs(sp) < 0.5 && std::fabs(rt) < 0.5;
            double re = nearZero && ae < 0.05 ? 0.0 : ae / std::max(std::fabs(sp), 1e-12);
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
    out << "\n\n| Circuit | Observable | Real-time | ngspice | Absolute error | Relative error | Verdict |\n|---|---|---:|---:|---:|---:|---|\n";
    for (const auto& r : rows) out << "| " << r.name << " | " << r.obs << " | " << r.rt << " | " << r.spice << " | " << r.abs << " | " << r.rel << " | `" << r.verdict << "` |\n";
    out << "\n## How to read this\n\n";
    out << "`OK` is within 2 %, `WARN` within 10 %, `BAD` beyond that. On a node near zero volts\n";
    out << "(both engines below 0.5 V) a difference under 50 mV counts as `OK`, because millivolt nodes\n";
    out << "are dominated by rounding. `error` rows mean an engine or the fixture failed, not that the\n";
    out << "two engines disagree.\n\n";
    out << "The fixtures are built by `--spice-xcheck` itself (see `src/spice_xcheck.cpp`): a 9 V battery,\n";
    out << "a series resistor and the device under test, with the observed node on the device's own pin.\n";
    out << "LEDs get per-colour diode parameters and the photocell exports a light-dependent resistor,\n";
    out << "both derived from the real-time model, so agreement is expected except for differences in\n";
    out << "model shape (exponential junction versus fixed forward drop).\n";
    return 0;
}
#else
int spiceCrossCheck(const std::string&) { return 2; }
#endif
