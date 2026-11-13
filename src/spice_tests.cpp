#include "tools.h"
#include <cstdio>
#include <string>

static bool has(const std::string& text, const char* token) { return text.find(token) != std::string::npos; }

void spiceExportTests(int& failures) {
    std::printf("SPICE export\n");
    auto expect = [&](bool ok, const char* name) {
        std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", name);
        if (!ok) failures++;
    };
    Document d;
    d.title = "spice-test";
    d.addPart("resistor", {{"resistance", "1k"}}, {0, 0});
    d.addPart("capacitor_ceramic", {{"capacitance", "1uF"}}, {500, 0});
    d.addPart("inductor", {{"inductance", "2mH"}}, {1000, 0});
    d.addPart("mosfet", {{"channel", "N"}}, {1500, 0});
    d.addPart("transistor", {{"polarity", "NPN"}}, {2000, 0});
    d.addPart("regulator", {}, {2500, 0});
    d.addPart("multimeter", {}, {3000, 0});
    d.addPart("pushbutton", {{"pressed", "yes"}}, {0, 0});
    d.addPart("switch", {{"position", "1"}}, {0, 0});
    d.addPart("battery", {{"voltage", "9V"}}, {0, 0});
    Connectivity cn;
    cn.build(d);
    SpiceOptions tr;
    tr.analysis = SpiceOptions::TRAN;
    tr.tstep = 1e-6;
    tr.tstop = 1e-3;
    SpiceNetlist net = buildSpice(d, cn, tr);
    expect(has(net.text, ".tran") && has(net.text, "L") && has(net.text, "M") && has(net.text, "Q") && has(net.text, "R") && has(net.text, ".end"), "TRAN and analog device elements export");
    expect(has(net.text, "Rs1_") && has(net.text, "Rs2_"), "switch and pushbutton pin mapping export");
    expect(!has(net.text, " nc "), "no invalid NC nodes export");
    SpiceOptions ac;
    ac.analysis = SpiceOptions::AC;
    ac.dcSource = "battery";
    SpiceNetlist acNet = buildSpice(d, cn, ac);
    expect(has(acNet.text, ".ac") && acNet.warnings.empty(), "AC analysis and excitation warning");
    SpiceOptions dc;
    dc.analysis = SpiceOptions::DC;
    dc.dcSource = "battery";
    SpiceNetlist dcNet = buildSpice(d, cn, dc);
    expect(has(dcNet.text, ".dc"), "DC analysis line");
    std::string legacy = exportSpice(d, cn);
    expect(has(legacy, ".op") && has(legacy, ".end"), "legacy SPICE export compatibility");

    Document digital;
    digital.title = "digital";
    digital.addPart("arduino_uno", {}, {0, 0});
    Connectivity dn;
    dn.build(digital);
    SpiceNetlist skipped = buildSpice(digital, dn, SpiceOptions{});
    expect(!skipped.skipped.empty(), "digital parts are reported as skipped");
    SpiceOptions ext;
    ext.externalDigital = true;
    SpiceNetlist external = buildSpice(digital, dn, ext);
    expect(!external.externals.empty() && has(external.text, "external"), "external digital source registration");
    std::printf("  generated OP/TRAN/AC/DC netlists; externals=%zu skipped=%zu\n", external.externals.size(), skipped.skipped.size());
}
