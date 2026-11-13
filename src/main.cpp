// designElectronics — entry point. Uses SDL3's main callbacks so the same code
// runs on desktop, iOS and Android.
//
// Command line:
//   designElectronics [file.dez]                open a project
//   designElectronics --selftest                run headless model/tool tests
//   designElectronics --export-examples DIR     write the built-in examples as .dez files
//   designElectronics --export-i18n DIR         write the built-in language packs as editable JSON
//   designElectronics --export-logo DIR         write logo.svg and app icon PNGs
//   designElectronics --mcp                     run as an MCP server on stdin/stdout (for AI assistants)
//   designElectronics --screenshot OUT.png [--example N] [--view bb|sch|pcb] [--sim]
//                     [--autoroute] [--tidy] [--size WxH] [--drawer parts|inspector|layers]
//                     [--select N] [--select-anno N]
//                     [--events "frame:d|x:y, frame:u|x:y, frame:m|x:y, frame:k|keycode:mods, frame:w|0|notches"]
#define SDL_MAIN_USE_CALLBACKS 1
#include <cmath>
#include <cctype>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "app.h"
#include "colorcode.h"
#include "history.h"
#include "i18n.h"
#include "logo.h"
#include "mcp.h"
#include "ngspice_backend.h"
#include "partmodels.h"
#include "settings.h"
#include "toml.h"

#if DE_NGSPICE_DESKTOP
int spiceCrossCheck(const std::string& outMarkdownPath);
#endif
#include "video.h"

void spiceExportTests(int& failures);
void ngspiceBackendTests(int& failures);

namespace {

int check(bool ok, const char* what, int& failures) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) failures++;
    return ok;
}

// Defined below the example loop, declared here so selftest() can call it.
void annoTests(int& failures);
void hierarchyTests(int& failures);
void hitTests(int& failures);
void configTests(int& failures);
void toolTests(int& failures);
void keyTests(int& failures);
void textSelectionTests(int& failures);
void inlineAnnoTests(int& failures);
void textModeTests(int& failures);
void shapeToolTests(int& failures);
void imageTests(int& failures);
void imageAnnoTests(int& failures);


// ---- circuit-building helpers for simulation tests ----
struct TB {
    Document d;
    int count = 0;
    int add(const std::string& type, Props props = {}) {
        count++;
        std::string code;
        if (props.count("code")) { code = props["code"]; props.erase("code"); }
        PartInst& p = d.addPart(type, props, {count * 6000.f, count * 3000.f});
        p.code = code;
        return p.id;
    }
    int pin(int part, const std::string& name) {
        const PartInst* p = d.part(part);
        for (size_t i = 0; i < p->def->pins.size(); i++) if (p->def->pins[i].name == name || p->def->pins[i].id == name) return (int)i;
        printf("  !! no pin %s on %s\n", name.c_str(), p->type.c_str());
        return -1;
    }
    void wire(int a, const std::string& pa, int b, const std::string& pb) {
        Wire w;
        w.id = d.newId();
        w.view = SCH;
        w.ends[0] = {a, pin(a, pa)};
        w.ends[1] = {b, pin(b, pb)};
        w.pts = {d.part(a)->pinPos(w.ends[0].pin, SCH), d.part(b)->pinPos(w.ends[1].pin, SCH)};
        d.wires.push_back(w);
    }
    int net(Connectivity& c, int part, const std::string& p) { return c.netOf(part, pin(part, p)); }
};

// Parts library data (parts/*.txt) and surface-mount pads end to end.
void partDataTests(int& failures) {
    printf("Parts library data\n");
    auto problems = checkPartData();
    for (size_t i = 0; i < problems.size() && i < 40; i++) printf("    %s\n", problems[i].c_str());
    check(problems.empty(), "every data part fits its packages, with no overlapping pads", failures);
    {   // the lesson breadboard's holes sit on the 0.1" grid, so a part dropped on the
        // grid always lands in a hole (a half-pitch offset here breaks every IC)
        PartDefPtr bb = getPartDef("breadboard", {{"size", "HUST EIC Lesson"}});
        PartDefPtr ic = getPartDef("74hc00", {});
        int offGrid = 0, inHole = 0;
        for (auto& pin : bb->pins) {
            float gx = pin.pos[BB].x / 100.f, gy = pin.pos[BB].y / 100.f;
            if (std::fabs(gx - std::round(gx)) > 1e-3f || std::fabs(gy - std::round(gy)) > 1e-3f) offGrid++;
        }
        for (auto& pin : ic->pins) {  // DIP dropped at (300, 700) straddles the channel
            Vec2 at{300 + pin.pos[BB].x, 700 + pin.pos[BB].y};
            for (auto& hole : bb->pins)
                if (dist(hole.pos[BB], at) < 1) { inHole++; break; }
        }
        printf("  lesson breadboard: %d holes off grid, %d/%d IC legs in holes\n", offGrid, inHole, (int)ic->pins.size());
        check(offGrid == 0 && inHole == (int)ic->pins.size(), "IC legs land in the breadboard holes", failures);
    }
    {   // an SMD chip and an SMD resistor on a board: route, DRC, Gerber paste layer
        TB t;
        int board = t.add("pcb_board", {{"width", "40"}, {"height", "30"}});
        int u = t.add("lm358", {{"package", "SOIC-8"}});
        int r = t.add("lm358", {{"package", "TSSOP-8"}});
        t.wire(u, "1OUT", r, "2IN+");
        t.wire(u, "VCC", r, "VCC");
        PartInst* b = t.d.part(board);
        b->place[PCB].pos = {0, 0};
        t.d.part(u)->place[PCB].pos = {400, 500};
        t.d.part(r)->place[PCB].pos = {1100, 600};
        Connectivity c;
        c.build(t.d);
        RouteResult rr = autoroute(t.d, c, true);
        c.build(t.d);
        RoutingStatus st;
        c.ratsnest(t.d, PCB, &st);
        bool topOnly = true;
        for (auto& w : t.d.wires) if (w.view == PCB && w.autorouted && w.layer != TOP) {
            for (auto& e : w.ends) if (e.attached() && t.d.part(e.part)->type != "via") topOnly = false;
        }
        check(st.connectionsToRoute == 0 && topOnly && rr.routed >= 2, ("SMD pads are routed on the top layer (" + rr.message + ")").c_str(), failures);
        int clr = 0;
        for (auto& is : runDrc(t.d, c))
            if (is.message.rfind("Too close", 0) == 0 || is.message.find("surface-mount") != std::string::npos) { clr++; printf("    drc: %s\n", is.message.c_str()); }
        check(clr == 0, "no DRC errors around fine-pitch SMD pads", failures);
        const char* tmp = SDL_GetPrefPath("designElectronics", "selftest");
        std::string zip = std::string(tmp ? tmp : "") + "gerber_smd.zip", msg;
        check(exportGerberZip(t.d, c, zip, &msg), "Gerber export with SMD pads", failures);
        Document d2;
        std::string err;
        check(d2.deserialize(t.d.serialize(true), &err) && d2.part(u)->def->pins[0].smd(), "SMD parts save and load", failures);
    }
}

// Part models: the .dezpart document, the datasheet-block path and the checks.
void modelTests(int& failures) {
    printf("Part models\n");
    const char* block =
        "[TESTMODEL]\n"
        "title     = Test Logic\n"
        "category  = Logic\n"
        "desc      = Imported from a datasheet block.\n"
        "datasheet = https://example.com/test.pdf\n"
        "package   = DIP-8\n"
        "pins      = A B C D E F G H\n"
        "sim       = logic\n";
    {   // a pasted datasheet block becomes a complete part
        Json doc = Json::object();
        doc.set("format", "designElectronics-part");
        doc.set("version", 2);
        doc.set("block", block);
        PartModel m;
        std::string err;
        bool ok = partModelFromJson(doc, m, &err);
        printf("  block import: %s (%d pins, sim %s)\n", ok ? "ok" : err.c_str(), (int)m.def.pins.size(), m.def.sim.c_str());
        check(ok && m.def.pins.size() == 8 && m.def.type == "testmodel", "a datasheet block becomes a part model", failures);
        check(m.def.datasheet == "https://example.com/test.pdf", "the block's datasheet becomes a field", failures);
    }
    {   // metadata and geometry survive save → load
        Json doc = Json::object();
        doc.set("block", block);
        PartModel src;
        std::string err;
        partModelFromJson(doc, src, &err);
        src.meta.tags = {"nand", "demo"};
        src.meta.author = "selftest";
        PartModel back;
        bool ok = partModelFromJson(partModelToJson(src), back, &err);
        printf("  round trip: %s (%d tags)\n", ok ? "ok" : err.c_str(), (int)back.meta.tags.size());
        check(ok && back.def.type == src.def.type && back.def.pins.size() == src.def.pins.size() &&
                  back.meta.tags.size() == 2 && back.def.datasheet == src.def.datasheet && back.meta.author == "selftest",
              "a model survives a save/load round trip", failures);
    }
    {   // the checks reject what would break the editor
        PartModel bad;
        bad.def.type = "Bad Type!";
        bad.def.title = "Bad";
        PinDef p;
        p.id = "1";
        bad.def.pins.push_back(p);
        bad.def.pins.push_back(p);       // duplicate id
        bad.def.buses.push_back({0, 9});  // bus points at a pin that doesn't exist
        std::vector<std::string> errs, warns;
        bool ok = checkPartModel(bad, errs, warns);
        printf("  checks: %d problems reported\n", (int)errs.size());
        check(!ok && errs.size() >= 3, "model checks catch bad ids, duplicate pins and missing pins", failures);
    }
    {   // the v1 document (a bare part definition) still imports
        PartDef d;
        d.type = "legacy";
        d.title = "Legacy Part";
        PinDef p;
        p.id = "1";
        d.pins.push_back(p);
        PartModel m;
        std::string err;
        bool ok = partModelFromJson(partDefToJson(d), m, &err);
        check(ok && m.meta.id == "legacy" && m.meta.source == "json", "older bare definitions still import", failures);
    }
    check(partModelSlug("74HC03 Quad NAND") == "74hc03_quad_nand", "model ids are slugged from the name", failures);
}

int simTests(int& failures) {
    printf("Simulation models\n");
    {   // 555 astable blinker: f = 1.44 / ((R1 + 2 R2) C)
        TB t;
        int bat = t.add("battery", {{"voltage", "9V"}});
        int u = t.add("timer555");
        int r1 = t.add("resistor", {{"resistance", "1kΩ"}}), r2 = t.add("resistor", {{"resistance", "10kΩ"}}), r3 = t.add("resistor", {{"resistance", "470Ω"}});
        int c = t.add("capacitor_electrolytic", {{"capacitance", "10µF"}});
        int led = t.add("led");
        t.wire(bat, "plus", u, "VCC"); t.wire(bat, "plus", u, "RESET"); t.wire(bat, "minus", u, "GND");
        t.wire(bat, "plus", r1, "pin0"); t.wire(r1, "pin1", u, "DIS"); t.wire(u, "DIS", r2, "pin0"); t.wire(r2, "pin1", u, "THR");
        t.wire(u, "THR", u, "TRIG"); t.wire(u, "TRIG", c, "plus"); t.wire(c, "minus", bat, "minus");
        t.wire(u, "OUT", r3, "pin0"); t.wire(r3, "pin1", led, "anode"); t.wire(led, "cathode", bat, "minus");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        int transitions = 0; bool prev = false;
        for (int i = 0; i < 200; i++) {
            SimResult r = sim.step(t.d, cn, {}, 0.01);
            bool on = r.brightness.count(led) && r.brightness[led] > 0.5f;
            if (i > 0 && on != prev) transitions++;
            prev = on;
        }
        double f = transitions / 2.0 / 2.0;
        printf("  555 astable: %.1f Hz (expected ~6.9 Hz)\n", f);
        check(f > 4.5 && f < 9.5, "555 astable blinks an LED at the right frequency", failures);
    }
    {   // ESP32 driving 74HC08 AND gates
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO4=HIGH, GPIO5=LOW"}});
        int g = t.add("74hc08");
        int ra = t.add("resistor", {{"resistance", "220Ω"}}), rb = t.add("resistor", {{"resistance", "220Ω"}});
        int la = t.add("led"), lb = t.add("led", {{"color", "Green"}});
        t.wire(esp, "3V3", g, "VCC"); t.wire(esp, "GND", g, "GND");
        t.wire(esp, "GPIO4", g, "1A"); t.wire(esp, "GPIO4", g, "1B"); t.wire(esp, "GPIO4", g, "2A"); t.wire(esp, "GPIO5", g, "2B");
        t.wire(g, "1Y", ra, "pin0"); t.wire(ra, "pin1", la, "anode"); t.wire(la, "cathode", esp, "GND");
        t.wire(g, "2Y", rb, "pin0"); t.wire(rb, "pin1", lb, "anode"); t.wire(lb, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        float a = r.brightness.count(la) ? r.brightness[la] : 0, b = r.brightness.count(lb) ? r.brightness[lb] : 0;
        printf("  74HC08: 1&1 -> LED %.2f, 1&0 -> LED %.2f\n", a, b);
        check(a > 0.3f && b < 0.05f, "logic gates follow their truth table", failures);
    }
    {   // 74HC03 open-drain NAND: a gate that is "off" lets the pull-up do the work
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO4=HIGH"}});
        int g = t.add("74hc03");
        int pu1 = t.add("resistor", {{"resistance", "10kΩ"}}), pu2 = t.add("resistor", {{"resistance", "10kΩ"}});
        int mm1 = t.add("multimeter"), mm2 = t.add("multimeter");
        t.wire(esp, "3V3", g, "VCC"); t.wire(esp, "GND", g, "GND");
        t.wire(esp, "GPIO4", g, "1A"); t.wire(esp, "GPIO4", g, "1B");   // 1Y pulls low
        t.wire(esp, "GPIO4", g, "2A"); t.wire(esp, "GND", g, "2B");     // 2Y lets go
        t.wire(esp, "3V3", pu1, "pin0"); t.wire(pu1, "pin1", g, "1Y");
        t.wire(esp, "3V3", pu2, "pin0"); t.wire(pu2, "pin1", g, "2Y");
        t.wire(mm1, "V+", g, "1Y"); t.wire(mm1, "COM", esp, "GND");
        t.wire(mm2, "V+", g, "2Y"); t.wire(mm2, "COM", esp, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        double low = std::atof(r.meterText[mm1].c_str()), high = std::atof(r.meterText[mm2].c_str());
        printf("  74HC03: inputs 1&1 -> %s, released -> %s\n", r.meterText[mm1].c_str(), r.meterText[mm2].c_str());
        check(low < 0.6 && high > 2.5, "open-drain output only pulls low", failures);
    }
    {   // CD40192: preset 5 with ~PL held low
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "3V3=HIGH"}});
        int c = t.add("cd40192");
        int mm = t.add("multimeter");
        t.wire(esp, "3V3", c, "VDD"); t.wire(esp, "GND", c, "VSS");
        t.wire(esp, "GND", c, "~PL");
        t.wire(esp, "3V3", c, "J1"); t.wire(esp, "GND", c, "J2");
        t.wire(esp, "3V3", c, "J3"); t.wire(esp, "GND", c, "J4");   // J = 0101 -> 5
        t.wire(mm, "V+", c, "Q3"); t.wire(mm, "COM", esp, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        double v = std::atof(r.meterText[mm].c_str());
        printf("  CD40192 preset 5: Q3 = %s (expected ~3.3 V)\n", r.meterText[mm].c_str());
        check(v > 2.5, "CD40192 loads its preset inputs", failures);
    }
    {   // CD40192 carry/borrow: low at the terminal count only while that clock is low
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "3V3=HIGH"}});
        int c1 = t.add("cd40192"), c2 = t.add("cd40192"), c3 = t.add("cd40192"), c4 = t.add("cd40192");
        int m1 = t.add("multimeter"), m2 = t.add("multimeter"), m3 = t.add("multimeter"), m4 = t.add("multimeter");
        auto preset = [&](int c, const char* j1, const char* j4) {  // J1..J4 = j1 0 0 j4
            t.wire(esp, "3V3", c, "VDD"); t.wire(esp, "GND", c, "VSS");
            t.wire(esp, "GND", c, "~PL");
            t.wire(esp, j1, c, "J1"); t.wire(esp, "GND", c, "J2");
            t.wire(esp, "GND", c, "J3"); t.wire(esp, j4, c, "J4");
        };
        preset(c1, "3V3", "3V3");  // 9
        preset(c2, "3V3", "3V3");
        preset(c3, "GND", "GND");  // 0
        preset(c4, "GND", "GND");
        t.wire(esp, "3V3", c1, "UP");  // up clock parked high: ~CARRY high
        t.wire(esp, "GND", c2, "UP");  // parked low: ~CARRY pulses low
        t.wire(esp, "GND", c3, "DOWN");  // down clock parked low: ~BORROW low
        t.wire(esp, "3V3", c4, "DOWN");  // parked high: ~BORROW high
        t.wire(m1, "V+", c1, "CARRY"); t.wire(m1, "COM", esp, "GND");
        t.wire(m2, "V+", c2, "CARRY"); t.wire(m2, "COM", esp, "GND");
        t.wire(m3, "V+", c3, "BORROW"); t.wire(m3, "COM", esp, "GND");
        t.wire(m4, "V+", c4, "BORROW"); t.wire(m4, "COM", esp, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        double hi = std::atof(r.meterText[m1].c_str()), lo = std::atof(r.meterText[m2].c_str());
        double blo = std::atof(r.meterText[m3].c_str()), bhi = std::atof(r.meterText[m4].c_str());
        printf("  CD40192 at 9: ~CARRY %s/%s, at 0: ~BORROW %s/%s (clock high/low)\n", r.meterText[m1].c_str(),
               r.meterText[m2].c_str(), r.meterText[m3].c_str(), r.meterText[m4].c_str());
        check(hi > 2.5 && lo < 0.6 && blo < 0.6 && bhi > 2.5, "CD40192 carry and borrow pulse with the clock", failures);
    }
    {   // CD40192 counting: Q1 toggles at half the clock rate
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "3V3=HIGH, GPIO2=BLINK:500"}});
        int c = t.add("cd40192");
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(esp, "3V3", c, "VDD"); t.wire(esp, "GND", c, "VSS");
        t.wire(esp, "3V3", c, "~PL"); t.wire(esp, "3V3", c, "DOWN");
        t.wire(esp, "GPIO2", c, "UP");
        t.wire(c, "Q1", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        float prev = -1;
        int flips = 0;
        for (int i = 0; i < 300; i++) {
            SimResult s = sim.step(t.d, cn, {}, 0.01);
            float b = s.brightness.count(led) ? s.brightness[led] : 0;
            if (prev >= 0 && std::fabs(b - prev) > 0.05f) flips++;
            prev = b;
        }
        printf("  CD40192 counting: Q1 toggled %d times in 3 s\n", flips);
        check(flips >= 2, "CD40192 counts up on the clock", failures);
    }
    {   // two CD40192s cascaded CARRY -> UP: the tens digit ticks once per ten clocks
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "3V3=HIGH, GPIO2=BLINK:100"}});
        int ones = t.add("cd40192"), tens = t.add("cd40192");
        for (int c : {ones, tens}) {
            t.wire(esp, "3V3", c, "VDD"); t.wire(esp, "GND", c, "VSS"); t.wire(esp, "3V3", c, "~PL");
        }
        t.wire(esp, "GPIO2", ones, "UP");
        t.wire(ones, "CARRY", tens, "UP");
        int q[4], o[4];
        for (int i = 0; i < 4; i++) {
            q[i] = t.add("multimeter");
            t.wire(q[i], "V+", tens, std::string("Q") + std::to_string(i + 1));
            t.wire(q[i], "COM", esp, "GND");
            o[i] = t.add("multimeter");
            t.wire(o[i], "V+", ones, std::string("Q") + std::to_string(i + 1));
            t.wire(o[i], "COM", esp, "GND");
        }
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        auto val = [](const std::string& s) { return std::atof(s.c_str()) > 1.5 ? 1 : 0; };
        int early = -1, later = -1, onesEarly = -1, onesLater = -1;
        for (int i = 0; i < 240; i++) {
            SimResult s = sim.step(t.d, cn, {}, 0.01);
            auto n = [&](int id) { return val(s.meterText.count(id) ? s.meterText[id] : "0"); };
            auto bits = [&](int* m) { return n(m[0]) + 2 * n(m[1]) + 4 * n(m[2]) + 8 * n(m[3]); };
            if (i == 104) { early = bits(q); onesEarly = bits(o); }
            if (i == 215) { later = bits(q); onesLater = bits(o); }
        }
        // between the two samples the ones digit advances 5-6 counts, so the tens
        // digit must advance exactly once — and not on every clock
        int dOnes = (onesLater - onesEarly + 10) % 10, dTens = later - early;
        printf("  CD40192 cascade: ones %d->%d, tens %d->%d in 1.1 s\n", onesEarly, onesLater, early, later);
        check(dOnes >= 4 && dOnes <= 6 && dTens == 1, "cascaded counters tick once per ten clocks", failures);
    }
    {   // LM358 non-inverting amplifier, gain 2
        TB t;
        int bat = t.add("battery", {{"voltage", "9V"}});
        int op = t.add("lm358");
        int ra = t.add("resistor", {{"resistance", "10kΩ"}}), rb = t.add("resistor", {{"resistance", "1kΩ"}});
        int rf = t.add("resistor", {{"resistance", "10kΩ"}}), rg = t.add("resistor", {{"resistance", "10kΩ"}});
        int mm = t.add("multimeter");
        t.wire(bat, "plus", op, "VCC"); t.wire(bat, "minus", op, "GND");
        t.wire(bat, "plus", ra, "pin0"); t.wire(ra, "pin1", op, "1IN+"); t.wire(op, "1IN+", rb, "pin0"); t.wire(rb, "pin1", bat, "minus");
        t.wire(op, "1OUT", rf, "pin0"); t.wire(rf, "pin1", op, "1IN-"); t.wire(op, "1IN-", rg, "pin0"); t.wire(rg, "pin1", bat, "minus");
        t.wire(mm, "V+", op, "1OUT"); t.wire(mm, "COM", bat, "minus");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        double v = std::atof(r.meterText[mm].c_str());
        printf("  LM358 gain-2 amplifier: in 0.82 V -> out %s (expected ~1.64 V)\n", r.meterText[mm].c_str());
        check(std::fabs(v - 1.636) < 0.05, "op-amp amplifies with the right gain", failures);
    }
    {   // ESP32 blink on GPIO2
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO2=BLINK:500"}});
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(esp, "GPIO2", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        float b1 = 0, b2 = 0;
        for (int i = 0; i < 30; i++) { SimResult s = sim.step(t.d, cn, {}, 0.01); if (i == 20) b1 = s.brightness[led]; }
        for (int i = 0; i < 50; i++) { SimResult s = sim.step(t.d, cn, {}, 0.01); if (i == 40) b2 = s.brightness[led]; }
        printf("  ESP32 GPIO2 blink: t=0.2s %.2f, t=0.7s %.2f\n", b1, b2);
        check(b1 > 0.2f && b2 < 0.05f, "ESP32 pin BLINK output toggles", failures);
    }
    {   // Raspberry Pi GPIO17 and Arduino Mega D53 drive LEDs; Due/Pi pins are 3.3 V, Mega 5 V
        TB t;
        int pi = t.add("rpi4", {{"outputs", "GPIO17=HIGH, GPIO2=HIGH"}});
        int mega = t.add("arduino_mega2560", {{"outputs", "D53=HIGH"}});
        int r1 = t.add("resistor", {{"resistance", "220Ω"}}), l1 = t.add("led");
        int r2 = t.add("resistor", {{"resistance", "220Ω"}}), l2 = t.add("led");
        int m1 = t.add("multimeter"), m2 = t.add("multimeter"), m3 = t.add("multimeter");
        t.wire(pi, "GPIO17", r1, "pin0"); t.wire(r1, "pin1", l1, "anode"); t.wire(l1, "cathode", pi, "GND");
        t.wire(mega, "D53", r2, "pin0"); t.wire(r2, "pin1", l2, "anode"); t.wire(l2, "cathode", mega, "GND");
        t.wire(pi, "GND", mega, "GND");
        t.wire(m1, "V+", pi, "GPIO2/SDA"); t.wire(m1, "COM", pi, "GND");
        t.wire(m2, "V+", mega, "5V"); t.wire(m2, "COM", mega, "GND");
        t.wire(m3, "V+", pi, "3V3"); t.wire(m3, "COM", pi, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.05);
        double vSda = std::atof(r.meterText[m1].c_str()), v5 = std::atof(r.meterText[m2].c_str()), v33 = std::atof(r.meterText[m3].c_str());
        printf("  Pi GPIO17 LED %.2f, Mega D53 LED %.2f, Pi GPIO2 %s, Mega 5V %s, Pi 3V3 %s\n", r.brightness[l1], r.brightness[l2],
               r.meterText[m1].c_str(), r.meterText[m2].c_str(), r.meterText[m3].c_str());
        check(r.brightness[l1] > 0.2f && r.brightness[l2] > 0.2f, "Raspberry Pi and Arduino Mega pins drive LEDs", failures);
        check(std::fabs(vSda - 3.3) < 0.1 && std::fabs(v5 - 5) < 0.1 && std::fabs(v33 - 3.3) < 0.1, "board rails and GPIO levels are right", failures);
    }
    {   // LM393 night light: LDR divider vs. pot threshold; open-collector output sinks an LED
        float lit[2];
        for (int dark = 0; dark < 2; dark++) {
            TB t;
            int bat = t.add("battery", {{"voltage", "5V"}});
            int ldr = t.add("photocell", {{"light", dark ? "5" : "100"}});
            int rf = t.add("resistor", {{"resistance", "10kΩ"}});
            int pot = t.add("potentiometer");
            int u = t.add("lm393");
            int pu = t.add("resistor", {{"resistance", "1kΩ"}});
            int led = t.add("led", {{"color", "Yellow"}});
            t.wire(bat, "plus", ldr, "pin0"); t.wire(ldr, "pin1", rf, "pin0"); t.wire(rf, "pin1", bat, "minus");
            t.wire(bat, "plus", pot, "leg1"); t.wire(pot, "leg2", bat, "minus");
            t.wire(u, "VCC", bat, "plus"); t.wire(u, "GND", bat, "minus");
            t.wire(ldr, "pin1", u, "1IN+"); t.wire(pot, "wiper", u, "1IN-");
            t.wire(bat, "plus", pu, "pin0"); t.wire(pu, "pin1", led, "anode"); t.wire(led, "cathode", u, "1OUT");
            Connectivity cn; cn.build(t.d);
            Simulator sim;
            SimResult r;
            for (int k = 0; k < 6; k++) r = sim.step(t.d, cn, {}, 0.05);  // keeps working while the simulation runs
            lit[dark] = r.brightness[led];
        }
        printf("  LM393 night light: bright %.2f, dark %.2f\n", lit[0], lit[1]);
        check(lit[0] < 0.05f && lit[1] > 0.1f, "comparator switches an LED on in the dark", failures);
    }
    {   // programs: an ESP32 sketch blinks an LED and prints over Serial
        TB t;
        int esp = t.add("esp32_devkit", {{"code",
            "const int LED = 2;\n"
            "void setup() { pinMode(LED, OUTPUT); Serial.begin(115200); Serial.println(\"hello\"); }\n"
            "void loop() { digitalWrite(LED, HIGH); delay(200); digitalWrite(LED, LOW); delay(200); }\n"}});
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(esp, "GPIO2", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        std::string serial;
        float on = 0, off = 0;
        for (int i = 0; i < 40; i++) {
            SimResult s = sim.step(t.d, cn, {}, 0.01);
            for (auto& kv : s.serial) serial += kv.second;
            if (i == 10) on = s.brightness[led];   // ~100 ms: LED on
            if (i == 30) off = s.brightness[led];  // ~300 ms: LED off
        }
        printf("  ESP32 sketch: LED %.2f then %.2f, serial \"%s\"\n", on, off, serial.substr(0, serial.find('\n')).c_str());
        check(on > 0.2f && off < 0.05f && serial == "hello\n", "an ESP32 sketch blinks an LED and prints to Serial", failures);
    }
    {   // programs: an Uno reads a pushbutton (INPUT_PULLUP) and lights an LED while it's held
        TB t;
        int uno = t.add("arduino_uno", {{"code",
            "void setup() { pinMode(2, INPUT_PULLUP); pinMode(13, OUTPUT); }\n"
            "void loop() { if (digitalRead(2) == LOW) digitalWrite(13, HIGH); else digitalWrite(13, LOW); }\n"}});
        int btn = t.add("pushbutton");
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(uno, "D2", btn, "pin1"); t.wire(btn, "pin2", uno, "GND");
        t.wire(uno, "D13", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", uno, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        SimResult a, b;
        for (int i = 0; i < 5; i++) a = sim.step(t.d, cn, {}, 0.01);
        for (int i = 0; i < 5; i++) b = sim.step(t.d, cn, {btn}, 0.01);
        printf("  Uno button sketch: released %.2f, pressed %.2f\n", a.brightness[led], b.brightness[led]);
        check(a.brightness[led] < 0.05f && b.brightness[led] > 0.3f, "a sketch reads a button through INPUT_PULLUP", failures);
    }
    {   // programs: shiftOut drives a 74HC595 (pattern 0b10100101 → QA, QC, QF, QH lit)
        TB t;
        int uno = t.add("arduino_uno", {{"code",
            "int latch = 8, clk = 12, data = 11;\n"
            "void setup() { pinMode(latch, OUTPUT); pinMode(clk, OUTPUT); pinMode(data, OUTPUT); }\n"
            "void loop() { digitalWrite(latch, LOW); shiftOut(data, clk, MSBFIRST, 0xA5); digitalWrite(latch, HIGH); delay(500); }\n"}});
        int sr = t.add("74hc595");
        t.wire(uno, "D11", sr, "SER"); t.wire(uno, "D12", sr, "SRCLK"); t.wire(uno, "D8", sr, "RCLK");
        t.wire(uno, "5V", sr, "VCC"); t.wire(uno, "5V", sr, "~SRCLR"); t.wire(uno, "GND", sr, "GND"); t.wire(uno, "GND", sr, "~OE");
        const char* q[] = {"QA", "QB", "QC", "QD", "QE", "QF", "QG", "QH"};
        int leds[8];
        for (int i = 0; i < 8; i++) {
            int r = t.add("resistor", {{"resistance", "330Ω"}});
            leds[i] = t.add("led");
            t.wire(sr, q[i], r, "pin0"); t.wire(r, "pin1", leds[i], "anode"); t.wire(leds[i], "cathode", uno, "GND");
        }
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        SimResult rr;
        for (int i = 0; i < 20; i++) rr = sim.step(t.d, cn, {}, 0.01);
        std::string pattern;
        for (int i = 0; i < 8; i++) pattern += rr.brightness[leds[i]] > 0.2f ? '1' : '0';
        printf("  74HC595 via shiftOut(0xA5): QA..QH = %s (expected 10100101)\n", pattern.c_str());
        check(pattern == "10100101", "shiftOut from a sketch loads a 74HC595", failures);
    }
    {   // programs: read a MAX6675 by bit-banging and show it on an SSD1306 (Adafruit API)
        TB t;
        int esp = t.add("esp32_devkit", {{"code",
            "#include <Adafruit_SSD1306.h>\n"
            "Adafruit_SSD1306 display(128, 64, &Wire, -1);\n"
            "float readTC() { uint16_t v = 0; digitalWrite(5, LOW);\n"
            "  for (int i = 15; i >= 0; i--) { digitalWrite(18, HIGH); if (digitalRead(19)) v |= (1 << i); digitalWrite(18, LOW); }\n"
            "  digitalWrite(5, HIGH); return (v >> 3) * 0.25; }\n"
            "void setup() { Serial.begin(115200); pinMode(18, OUTPUT); pinMode(5, OUTPUT); pinMode(19, INPUT); digitalWrite(5, HIGH);\n"
            "  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println(\"no oled\"); }\n"
            "void loop() { float c = readTC(); Serial.println(c, 2); display.clearDisplay(); display.setCursor(0, 0);\n"
            "  display.print(c, 1); display.print(\" C\"); display.drawRect(0, 20, 50, 10, SSD1306_WHITE); display.display(); delay(200); }\n"}});
        int tc = t.add("max6675", {{"temperature", "123.25"}});
        int ol = t.add("ssd1306");
        t.wire(esp, "GPIO18", tc, "SCK"); t.wire(esp, "GPIO5", tc, "CS"); t.wire(esp, "GPIO19", tc, "SO");
        t.wire(esp, "3V3", tc, "VCC"); t.wire(esp, "GND", tc, "GND");
        t.wire(esp, "GPIO21", ol, "SDA"); t.wire(esp, "GPIO22", ol, "SCL"); t.wire(esp, "3V3", ol, "VCC"); t.wire(esp, "GND", ol, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        std::string serial;
        SimResult r;
        for (int i = 0; i < 30; i++) { r = sim.step(t.d, cn, {}, 0.01); for (auto& kv : r.serial) serial += kv.second; }
        std::string shown = r.oled.count(ol) && !r.oled[ol].text.empty() ? r.oled[ol].text[0].text : "";
        size_t lit = 0;
        if (r.oled.count(ol)) for (auto px : r.oled[ol].px) lit += px;
        printf("  MAX6675 -> OLED: serial \"%s\", OLED \"%s\", %zu px, %zu warnings\n", serial.substr(0, serial.find('\n')).c_str(), shown.c_str(), lit,
               r.warnings.size());
        check(serial.rfind("123.25\n", 0) == 0 && shown == "123.3 C" && lit == 2 * 50 + 2 * 8 && r.warnings.empty(),
              "a sketch reads a MAX6675 and draws on an SSD1306 OLED", failures);
    }
    {   // programs: compile errors are reported with their line
        TB t;
        int uno = t.add("arduino_uno", {{"code", "void setup() {\n  pinMode(13, OUTPUT)\n}\nvoid loop() {}\n"}});
        Connectivity cn; cn.build(t.d);
        SimResult r = simulate(t.d, cn, {}, 0.01);
        printf("  broken sketch: line %d: %s\n", r.code[uno].errorLine, r.code[uno].error.c_str());
        check(r.code[uno].errorLine == 3 && !r.code[uno].error.empty(), "sketch errors point at the right line", failures);
    }
    {   // CD4017 clocked by an ESP32 square wave: outputs advance
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO4=BLINK:50, GPIO5=LOW"}});
        int c = t.add("cd4017");
        t.wire(esp, "3V3", c, "VDD"); t.wire(esp, "GND", c, "VSS"); t.wire(esp, "GPIO4", c, "CLK");
        t.wire(esp, "GPIO5", c, "~CLKINH"); t.wire(esp, "GPIO5", c, "RST");
        int ld = t.add("led");
        int rr = t.add("resistor", {{"resistance", "100Ω"}});
        t.wire(c, "Q3", rr, "pin0"); t.wire(rr, "pin1", ld, "anode"); t.wire(ld, "cathode", esp, "GND");
        Connectivity cn; cn.build(t.d);
        Simulator sim;
        int on = 0;
        for (int i = 0; i < 200; i++) { SimResult s = sim.step(t.d, cn, {}, 0.01); if (s.brightness[ld] > 0.05f) on++; }
        printf("  CD4017: Q3 lit in %d of 200 samples (expected ~20)\n", on);
        check(on > 8 && on < 40, "CD4017 counter advances on clock edges", failures);
    }
    {   // Relay switching an LED
        TB t;
        int bat = t.add("battery", {{"voltage", "5V"}});
        int k = t.add("relay_module");
        int r = t.add("resistor", {{"resistance", "220Ω"}}), led = t.add("led");
        t.wire(bat, "plus", k, "COIL+"); t.wire(k, "COIL-", bat, "minus");
        t.wire(bat, "plus", k, "COM"); t.wire(k, "NO", r, "pin0"); t.wire(r, "pin1", led, "anode"); t.wire(led, "cathode", bat, "minus");
        Connectivity cn; cn.build(t.d);
        SimResult s = simulate(t.d, cn, {}, 0.05);
        printf("  relay: %s, coil+ %.2f V, coil- %.2f V, NO %.2f V, LED %.2f\n", s.message.c_str(), s.netVoltage[(size_t)t.net(cn, k, "COIL+")],
               s.netVoltage[(size_t)t.net(cn, k, "COIL-")], s.netVoltage[(size_t)t.net(cn, k, "NO")], s.brightness[led]);
        check(s.brightness[led] > 0.3f, "relay coil closes the NO contact", failures);
    }
    {   // CD4511 + 7-segment showing "2" from an ESP32
        TB t;
        int esp = t.add("esp32_devkit", {{"outputs", "GPIO12=LOW, GPIO13=HIGH, GPIO14=LOW, GPIO27=LOW, GPIO26=HIGH"}});
        int dec = t.add("cd4511"), seg = t.add("seg7_cc");
        t.wire(esp, "3V3", dec, "VDD"); t.wire(esp, "GND", dec, "VSS");
        t.wire(esp, "GPIO12", dec, "A"); t.wire(esp, "GPIO13", dec, "B"); t.wire(esp, "GPIO14", dec, "C"); t.wire(esp, "GPIO27", dec, "D");
        t.wire(esp, "GPIO26", dec, "~LT"); t.wire(esp, "GPIO26", dec, "~BL"); t.wire(esp, "GPIO27", dec, "LE");
        const char* segs[] = {"a", "b", "c", "d", "e", "f", "g"};
        for (auto* sg : segs) {
            int r = t.add("resistor", {{"resistance", "220Ω"}});
            t.wire(dec, sg, r, "pin0");
            t.wire(r, "pin1", seg, sg);
        }
        t.wire(seg, "COM", esp, "GND");
        Connectivity cn; cn.build(t.d);
        SimResult s = simulate(t.d, cn, {}, 0.05);
        std::string lit;
        for (int i = 0; i < 7; i++) if (s.segments[seg].size() > (size_t)i && s.segments[seg][(size_t)i] > 0.2f) lit += segs[i];
        printf("  CD4511 + 7-seg showing 2: lit segments \"%s\" (expected abdeg)\n", lit.c_str());
        check(lit == "abdeg", "BCD decoder drives the 7-segment display", failures);
    }
    return failures;
}

// ---- version history (local) ----
void historyTests(int& failures) {
    printf("Version history\n");
    char* base = SDL_GetPrefPath("designElectronics", "designElectronics-selftest");
    std::string root = std::string(base ? base : "./") + "history-test";
    SDL_free(base);
    SDL_RemovePath((root + "/x").c_str());
    history::setRoot(root);
    Document d;
    examples()[0].build(d);  // LED with battery
    d.syncWireEnds();
    history::ensureUid(d);
    d.uid += std::to_string(SDL_GetTicks());  // fresh history for every run
    LocalVersion v1 = history::record(d, d.serialize(true), "");
    LocalVersion same = history::record(d, d.serialize(true), "");
    PartInst* r = nullptr;
    for (auto& p : d.parts) if (p.type == "resistor") r = &p;
    std::string before = r->prop("resistance");
    r->props["resistance"] = "330Ω";
    r->refresh();
    d.addPart("led", {{"color", "Green"}}, {0, 0});
    LocalVersion v2 = history::record(d, d.serialize(true), "Brighter");
    auto list = history::list(d.uid);
    printf("  v%d: %s\n  v%d: %s (%s)\n", v1.n, v1.summary.c_str(), v2.n, v2.summary.c_str(), v2.message.c_str());
    check(v1.n == 1 && same.n == 0 && v2.n == 2, "every save is a version; unchanged saves are skipped", failures);
    check(v2.summary.find("330Ω") != std::string::npos && v2.summary.find("Added") != std::string::npos, "change summary names what changed", failures);
    check(list.size() == 2 && list[0].n == 2, "versions are listed newest first", failures);
    Document old;
    std::string err;
    bool ok = old.deserialize(history::read(d.uid, list[1]), &err);
    std::string oldR;
    for (auto& p : old.parts) if (p.type == "resistor") oldR = p.prop("resistance");
    check(ok && oldR == before && old.parts.size() + 1 == d.parts.size(), "an old version reads back exactly", failures);
    std::string bin = std::string("de\x00\xff\x10 Ω") + "end";
    check(github::base64Decode(github::base64Encode(bin)) == bin && github::base64Encode("Man") == "TWFu" && github::base64Encode("Ma") == "TWE=",
          "base64 round trip", failures);
    GitHubSettings gs;
    gs.login = "octocat";
    GitLink l = github::defaultLink(gs, d);
    printf("  default GitHub location: %s / %s\n", l.repo.c_str(), l.path.c_str());
    check(l.repo == "octocat/electronics-designs" && l.path.rfind("led-with-battery-", 0) == 0, "each design gets its own folder", failures);
    history::setRoot("");
}

// ---- video writers: valid MP4 (H.264 SPS not byte-swapped) and GIF ----
void videoTests(int& failures) {
    printf("Video export\n");
    char* base = SDL_GetPrefPath("designElectronics", "designElectronics-selftest");
    std::string dir = base ? base : "./";
    SDL_free(base);
    const int W = 270, H = 480;  // not multiples of 16, like 1080×1920
    std::vector<uint8_t> px((size_t)W * H * 4);
    std::string err;
    auto mp4 = openMp4(dir + "t.mp4", W, H, 30, &err);
    auto gif = openGif(dir + "t.gif", W, H, 15, &err);
    bool ok = mp4 && gif;
    for (int f = 0; ok && f < 20; f++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t* p = &px[((size_t)y * W + x) * 4];
                bool led = (x - W / 2) * (x - W / 2) + (y - H / 2) * (y - H / 2) < 900 && (f / 5) % 2 == 0;
                p[0] = led ? 230 : 245; p[1] = led ? 40 : 245; p[2] = led ? 40 : 240; p[3] = 255;
            }
        ok = mp4->addFrame(px.data(), W * 4) && gif->addFrame(px.data(), W * 4);
    }
    ok = ok && mp4->finish() && gif->finish();
    std::string m = readTextFile(dir + "t.mp4"), gf = readTextFile(dir + "t.gif");
    bool mp4ok = m.size() > 1000 && m.find("ftyp") != std::string::npos && m.find("avcC") != std::string::npos &&
                 m.find(std::string("\x67\x42", 2)) != std::string::npos;  // SPS: NAL type 7, Baseline profile
    bool gifok = gf.size() > 100 && gf.compare(0, 6, "GIF89a") == 0 && (unsigned char)gf.back() == 0x3B;
    printf("  MP4 %zu bytes, GIF %zu bytes\n", m.size(), gf.size());
    check(ok && mp4ok, "MP4 export is valid H.264 (header not byte-swapped)", failures);
    check(ok && gifok, "GIF export is a valid animation", failures);
    SDL_RemovePath((dir + "t.mp4").c_str());
    SDL_RemovePath((dir + "t.gif").c_str());
}

// ---- layers: hidden / locked survive save + load ----
// ---- tidy wiring: right angles, same connections, no overlaps with other nets ----
void tidyTests(int& failures) {
    printf("Tidy wiring\n");
    bool allOrtho = true, sameNets = true, endsKept = true, noOverlap = true, sameRats = true;
    int tidied = 0;
    for (size_t i = 0; i < examples().size(); i++) {
        Document d;
        examples()[i].build(d);
        d.syncWireEnds();
        // the examples leave schematic wiring to the ratsnest: draw it as straight wires first
        Connectivity c;
        c.build(d);
        for (auto& r : c.ratsnest(d, SCH)) {
            Wire w;
            w.id = d.newId();
            w.view = SCH;
            w.pts = {r.a, r.b};
            w.ends[0] = {r.pa.part, r.pa.pin};
            w.ends[1] = {r.pb.part, r.pb.pin};
            d.wires.push_back(w);
        }
        c.build(d);
        std::map<std::pair<int, int>, int> before;
        for (auto& p : d.parts)
            for (int k = 0; k < (int)p.def->pins.size(); k++) before[{p.id, k}] = c.netOf(p.id, k);
        size_t ratsBefore[2] = {c.ratsnest(d, BB).size(), c.ratsnest(d, SCH).size()};
        for (int v : {BB, SCH, PCB}) tidied += tidyWires(d, v);
        d.syncWireEnds();
        Connectivity c2;
        c2.build(d);
        // same partition of pins into nets
        std::map<int, int> map;
        for (auto& kv : before) {
            int n2 = c2.netOf(kv.first.first, kv.first.second);
            auto it = map.find(kv.second);
            if (it == map.end()) map[kv.second] = n2;
            else if (it->second != n2) sameNets = false;
        }
        if (c2.ratsnest(d, BB).size() != ratsBefore[0] || c2.ratsnest(d, SCH).size() != ratsBefore[1]) sameRats = false;
        for (auto& w : d.wires) {
            if (!w.tidy) continue;
            for (size_t k = 0; k + 1 < w.pts.size(); k++)
                if (std::fabs(w.pts[k].x - w.pts[k + 1].x) > 0.5f && std::fabs(w.pts[k].y - w.pts[k + 1].y) > 0.5f) allOrtho = false;
            for (int e = 0; e < 2; e++) {
                const PartInst* p = d.part(w.ends[e].part);
                if (!p || dist(p->pinPos(w.ends[e].pin, w.view), e ? w.pts.back() : w.pts.front()) > 0.5f) endsKept = false;
            }
        }
        // schematic: a wire must not run along, or bend on, a wire of another net
        for (auto& a : d.wires) {
            if (a.view != SCH) continue;
            for (auto& b : d.wires) {
                if (b.view != SCH || b.id <= a.id || c2.wireNet(a.id) == c2.wireNet(b.id)) continue;
                for (size_t m = 0; m + 1 < a.pts.size(); m++)
                    for (size_t n = 0; n + 1 < b.pts.size(); n++) {
                        Vec2 p0 = a.pts[m], p1 = a.pts[m + 1], q0 = b.pts[n], q1 = b.pts[n + 1];
                        bool hh = std::fabs(p0.y - p1.y) < 0.5f && std::fabs(q0.y - q1.y) < 0.5f && std::fabs(p0.y - q0.y) < 0.5f;
                        bool vv = std::fabs(p0.x - p1.x) < 0.5f && std::fabs(q0.x - q1.x) < 0.5f && std::fabs(p0.x - q0.x) < 0.5f;
                        float lo = hh ? std::max(std::min(p0.x, p1.x), std::min(q0.x, q1.x)) : std::max(std::min(p0.y, p1.y), std::min(q0.y, q1.y));
                        float hi = hh ? std::min(std::max(p0.x, p1.x), std::max(q0.x, q1.x)) : std::min(std::max(p0.y, p1.y), std::max(q0.y, q1.y));
                        if ((hh || vv) && hi - lo > 1) noOverlap = false;
                    }
            }
        }
    }
    printf("  %d wire(s) tidied across the examples\n", tidied);
    check(tidied > 0, "examples have wires to tidy", failures);
    check(allOrtho, "tidied wires are horizontal and vertical runs only", failures);
    check(endsKept, "tidied wires still end on their pins", failures);
    check(sameNets && sameRats, "tidying keeps every connection the same", failures);
    check(noOverlap, "schematic wires never run along another net's wire", failures);
    Document d;
    examples()[4].build(d);
    d.syncWireEnds();
    tidyWires(d, BB);
    Document e;
    std::string err;
    bool ok = e.deserialize(d.serialize(true), &err);
    bool flag = ok && !e.wires.empty();
    for (auto& w : e.wires) if (w.view == BB && w.ends[0].attached() && w.ends[1].attached() && !w.tidy) flag = false;
    check(flag, "tidy wires are saved and loaded as tidy", failures);
}

void layerTests(int& failures) {
    printf("Layers\n");
    Document d;
    examples()[0].build(d);
    PartInst* led = nullptr;
    for (auto& p : d.parts) if (p.type == "led") led = &p;
    led->hidden = 1u << BB;
    led->locked = true;
    Document e;
    std::string err;
    bool ok = e.deserialize(d.serialize(true), &err);
    const PartInst* l2 = nullptr;
    for (auto& p : e.parts) if (p.type == "led") l2 = &p;
    check(ok && l2 && l2->hiddenIn(BB) && !l2->hiddenIn(SCH) && l2->locked, "hidden (per view) and locked are saved and loaded", failures);
    Connectivity c1, c2;
    c1.build(d);
    examples()[0].build(e = Document());
    c2.build(e);
    SimResult r = simulate(d, c1, {});
    check(c1.numNets() == c2.numNets() && !r.brightness.empty() && r.brightness.begin()->second > 0.3f, "a hidden part still connects and simulates", failures);
}

// ---- GitHub sync against a mock API (tests/github_mock_test.py sets DE_GITHUB_API) ----
int githubSelftest() {
    int failures = 0;
    GitHubSettings s;
    s.token = "test-token";
    std::string err, login;
    check(github::whoAmI(s.token, login, err), ("who am I: " + (err.empty() ? login : err)).c_str(), failures);
    s.login = login;
    Document d;
    examples()[0].build(d);
    history::ensureUid(d);
    GitLink link = github::defaultLink(s, d);
    GitCommit c1, c2;
    check(github::commitFile(s, link, "version one", "First", c1, err), ("first commit " + err).c_str(), failures);
    check(github::commitFile(s, link, "version two Ω", "Second", c2, err), ("second commit " + err).c_str(), failures);
    std::vector<GitCommit> list;
    bool listed = github::listCommits(s, link, list, err);
    printf("  listed %zu commit(s)%s%s\n", list.size(), list.empty() ? "" : ", newest: ", list.empty() ? err.c_str() : list[0].message.c_str());
    check(listed && list.size() == 2 && list[0].message == "Second", "history lists both commits, newest first", failures);
    std::string body;
    check(github::fetchFile(s, link, c1.sha, body, err) && body == "version one", "an old version downloads exactly", failures);
    GitLink other = link;
    other.repo = "someone-else/designs";
    check(!github::commitFile(s, other, "x", "x", c1, err) && err.find("only create repositories under your own account") != std::string::npos,
          "won't create repos for other owners", failures);
    s.token = "bad";
    check(!github::whoAmI(s.token, login, err) && err.find("401") != std::string::npos, "bad token gives a clear error", failures);
    printf(failures ? "%d FAILURE(S)\n" : "GitHub sync tests passed.\n", failures);
    return failures ? 1 : 0;
}

void menuTests(int& failures) {
    printf("Menu overlays\n");
    UI ui;
    Gfx gfx;
    Input input;
    ui.g = &gfx;
    ui.in = &input;
    int fired = 0;
    std::vector<MenuItem> items;
    for (int i = 0; i < 60; i++) items.push_back({"item " + std::to_string(i), "", [&] { fired++; }, {}, true, false, false});
    input.pressSerial = 7;
    Rect anchor{100, 680, 120, 24};
    ui.openMenu(anchor, items);
    Rect bounds = ui.menuBoundsForTest();
    input.mouse = bounds.center();
    input.pressPos = bounds.center();
    input.released = true;
    ui.end();
    check(fired == 0 && ui.hasMenu(), "menu opening release cannot activate an item", failures);
    input.released = false;
    input.pressSerial++;
    input.released = true;
    ui.end();
    check(fired == 1 && !ui.hasMenu(), "new release activates menu item", failures);

    ui.openMenu(anchor, items);
    bounds = ui.menuBoundsForTest();
    check(bounds.b() <= 720 && bounds.y >= 0 && (bounds.x >= anchor.r() || bounds.r() <= anchor.x || bounds.y >= anchor.b() || bounds.b() <= anchor.y),
          "tall menu stays within window and avoids anchor", failures);
    input.released = false;
    input.mouse = bounds.center();
    input.wheelY = -1000;
    ui.end();
    check(ui.menuScrollForTest() >= 0 && ui.menuScrollForTest() <= ui.menuMaxScrollForTest(), "menu scroll is clamped", failures);
}

// ---- pictures: sniffing, limits, content keys, document storage, texture cache ----
void imageTests(int& failures) {
    printf("Pictures\n");
    // A small picture, encoded in memory: the self-test carries no asset files.
    std::vector<unsigned char> px((size_t)8 * 6 * 4);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 8; x++) {
            unsigned char* p = &px[((size_t)y * 8 + x) * 4];
            p[0] = (unsigned char)(x * 30);
            p[1] = (unsigned char)(y * 40);
            p[2] = 200;
            p[3] = ((x + y) & 1) ? 255 : 128;
        }
    std::string png;
    check(imageEncodePng(px.data(), 8, 6, 4, &png) && png.size() > 8 && (unsigned char)png[1] == 'P',
          "a PNG can be written in memory", failures);

    ImageBlob small;
    std::string err, note;
    bool ok = imageImport(png, "swatch.png", &small, &err, &note);
    check(ok && small.mime == "image/png" && small.w == 8 && small.h == 6 && !small.vector && small.bytes == png &&
              note.empty(),
          "a small PNG imports unchanged, keeping its own format and size", failures);

    {   // over the 4096 px limit: scaled down and re-encoded (no alpha here -> JPEG)
        std::vector<unsigned char> wide((size_t)5000 * 10 * 3);
        for (size_t i = 0; i < wide.size(); i += 3) {
            wide[i] = (unsigned char)((i * 7) % 251);
            wide[i + 1] = (unsigned char)((i * 3) % 97);
            wide[i + 2] = (unsigned char)((i * 11) % 31);
        }
        std::string widePng;
        check(imageEncodePng(wide.data(), 5000, 10, 3, &widePng), "a 5000x10 PNG can be written in memory", failures);
        ImageBlob scaled;
        err.clear();
        note.clear();
        ok = imageImport(widePng, "wide.png", &scaled, &err, &note);
        check(ok && scaled.w == 4096 && scaled.h == 8 && scaled.mime == "image/jpeg" && !note.empty(),
              "a bitmap wider than 4096 px is scaled down and re-encoded", failures);
        std::vector<uint8_t> pixels;
        int dw = 0, dh = 0;
        check(imageDecodeRGBA(scaled, 0, pixels, dw, dh) && dw == 4096 && dh == 8 &&
                  pixels.size() == (size_t)dw * dh * 4,
              "the scaled bitmap decodes back as 4096x8", failures);
    }

    {   // SVG stays vector: its own size on import, rasterized at the asked size
        const std::string svg =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"48\" viewBox=\"0 0 64 48\">"
            "<rect x=\"0\" y=\"0\" width=\"64\" height=\"48\" fill=\"#3366cc\"/></svg>";
        ImageBlob vec;
        err.clear();
        note.clear();
        ok = imageImport(svg, "mark.svg", &vec, &err, &note);
        check(ok && vec.vector && vec.mime == "image/svg+xml" && vec.w == 64 && vec.h == 48 && vec.bytes == svg,
              "an SVG imports as a vector with its viewBox size", failures);
        std::vector<uint8_t> raster;
        int rw = 0, rh = 0;
        check(imageDecodeRGBA(vec, 32, raster, rw, rh) && rw == 32 && rh == 24 && raster.size() == (size_t)32 * 24 * 4 &&
                  raster[((size_t)12 * 32 + 16) * 4 + 3] == 255,
              "the SVG rasterizes at the requested size, opaquely", failures);
    }

    ImageBlob junk;
    err.clear();
    check(!imageImport("not a picture, just text", "notes.bin", &junk, &err, nullptr) && !err.empty(),
          "bytes that are not a picture are refused with a reason", failures);

    ImageBlob probe;
    probe.bytes = "abc";
    check(imageKey(probe) == "a9993e364706816aba3e25717850c26c9cd0d89d", "the content key is the SHA-1 of the bytes", failures);

    // Document storage: pictures are content addressed, saved with the design, and
    // kept out of undo snapshots (which only carry the reference).
    Document d;
    Anno pic;
    pic.id = d.newId();
    pic.view = BB;
    pic.kind = AnnoKind::Image;
    pic.a = {200, 300};
    pic.b = {800, 600};
    pic.image = d.addImage(small);
    d.annos.push_back(pic);
    check(d.addImage(small) == pic.image && d.images.size() == 1, "the same picture is stored once", failures);

    const std::string snap = d.snapshot();
    const std::string fullText = d.serialize(true);
    const std::string payload = github::base64Encode(small.bytes);
    check(fullText.find("\"images\"") != std::string::npos && fullText.find(payload) != std::string::npos,
          "the saved file carries the picture bytes", failures);
    check(snap.find("\"images\"") == std::string::npos && snap.find(payload) == std::string::npos &&
              snap.size() + small.bytes.size() < fullText.size(),
          "an undo snapshot keeps the reference, not the bytes", failures);

    Document reloaded;
    std::string e2;
    bool okBack = reloaded.deserialize(fullText, &e2);
    const ImageBlob* rt = okBack ? reloaded.image(pic.image) : nullptr;
    check(okBack && rt && rt->bytes == small.bytes && rt->mime == "image/png" && rt->w == 8 && rt->h == 6,
          "a picture survives save and load byte for byte", failures);

    {   // a file is never trusted: bytes that are not a picture (whatever the mime says) are
        // skipped on load, and a real picture gets the mime its bytes say, not the file's
        Json j = Json::parse(fullText);
        Json imgs = j["images"];
        Json bogus = Json::object();
        bogus.set("mime", "image/png");
        bogus.set("w", 4);
        bogus.set("h", 4);
        bogus.set("data", github::base64Encode("<html>not a picture</html>"));
        imgs.set("deadbeef", bogus);
        Json lie = imgs[pic.image];
        lie.set("mime", "image/svg+xml");
        imgs.set(pic.image, lie);
        j.set("images", imgs);
        Document hostile;
        std::string e4;
        bool okHostile = hostile.deserialize(j.dump(-1), &e4);
        const ImageBlob* real = okHostile ? hostile.image(pic.image) : nullptr;
        check(okHostile && !hostile.image("deadbeef") && real && real->mime == "image/png" && !real->vector,
              "a loaded file keeps only real pictures, typed by their bytes", failures);
    }

    Document restored = d;  // the undo path: deserialize a snapshot into a live document
    std::string e3;
    restored.deserialize(snap, &e3);
    const ImageBlob* kept = restored.image(pic.image);
    check(kept && kept->bytes == small.bytes, "restoring an undo snapshot keeps the pictures in memory", failures);

    ImageBlob other;
    other.mime = "image/png";
    other.w = 8;
    other.h = 6;
    other.bytes = png + "different";  // a different key, referenced by nothing
    Document pruned;                  // built in memory: the annotation still points at its key
    pruned.annos.push_back(pic);
    pruned.addImage(small);
    pruned.addImage(other);
    const size_t storeBefore = pruned.images.size();
    const int dropped = pruned.pruneImages();
    check(storeBefore == 2 && dropped == 1 && pruned.images.size() == 1 && pruned.image(pic.image) != nullptr,
          "pruneImages drops the pictures nothing refers to", failures);

    Document bare;
    bare.addImage(small);
    check(bare.serialize(true).find("\"images\"") == std::string::npos,
          "a picture no annotation refers to is not written", failures);

    ImageCache cache;  // no renderer: exactly the headless self-test situation
    check(cache.get(small, 128) == nullptr && cache.usedBytes() == 0 && cache.renderer() == nullptr,
          "the texture cache is a no-op without a renderer", failures);

    // A 3x2 lossless WebP with one translucent pixel (generated once, embedded here).
    const std::string webp64 = "UklGRiIAAABXRUJQVlA4TBYAAAAvAkAAEA/wj/9BHpT/AUYabnAQ0f8Q";
    ImageBlob webp;
    err.clear();
    note.clear();
    ok = imageImport(github::base64Decode(webp64), "tiny.webp", &webp, &err, &note);
    if (!ok) printf("    webp import failed: %s\n", err.c_str());
    check(ok && webp.mime == "image/webp" && webp.w == 3 && webp.h == 2, "a WebP imports (3x2 test vector)", failures);
    std::vector<uint8_t> webpPixels;
    int ww = 0, wh = 0;
    check(imageDecodeRGBA(webp, 0, webpPixels, ww, wh) && ww == 3 && wh == 2 && webpPixels.size() == 24 &&
              webpPixels[3] == 128,
          "WebP pixels and alpha decode", failures);
}

// ---- pictures as annotations: model, geometry, clipboard, save and load ----
void imageAnnoTests(int& failures) {
    printf("Picture annotations\n");
    // A picture to annotate, built in memory so the test needs no asset files.
    std::vector<unsigned char> px((size_t)40 * 30 * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i] = (unsigned char)(i % 251);
        px[i + 1] = 120;
        px[i + 2] = 220;
        px[i + 3] = 255;
    }
    ImageBlob blob;
    {
        std::string png, err, note;
        imageEncodePng(px.data(), 40, 30, 4, &png);
        bool ok = imageImport(png, "source.png", &blob, &err, &note);
        check(ok && blob.mime == "image/png" && blob.w == 40 && blob.h == 30, "the test picture is 40x30 PNG", failures);
    }

    Anno img;
    img.id = 7;
    img.view = SCH;
    img.kind = AnnoKind::Image;
    img.a = {100, 200};
    img.b = {800, 600};
    img.image = "0f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c";  // just the reference; the bytes live in Document
    img.rotation = 90;
    img.framed = true;
    img.fill = rgba(255, 255, 255);
    img.widthStep = 2;
    img.dashed = true;
    check(std::string(annoKindKey(AnnoKind::Image)) == "image" && annoKindFromKey("image", nullptr) &&
              annoKindFromKey(annoKindKey(AnnoKind::Image), nullptr),
          "the picture kind has its own key", failures);

    {
        Json j = annoToJson(img);
        Anno back;
        bool ok = annoFromJson(j, &back);
        check(ok && back.kind == AnnoKind::Image && back.image == img.image && std::fabs(back.rotation - 90.f) < 0.01f &&
                  back.framed && back.fill == rgba(255, 255, 255) && back.widthStep == 2 && back.dashed &&
                  std::fabs(back.a.x - 100) < 0.5f && std::fabs(back.b.x - 800) < 0.5f && std::fabs(back.b.y - 600) < 0.5f,
              "an image annotation round trips through JSON (key, rotation, frame, backdrop, width)", failures);
        // A rect reading a picture's JSON must not pick the image fields up.
        Anno rect;
        rect.id = 8;
        rect.kind = AnnoKind::Rect;
        Json rj = annoToJson(rect);
        check(!rj.has("image") && !rj.has("rotation") && !rj.has("framed"),
              "shapes do not write picture fields", failures);
    }

    // Moving keeps the size: b is the box size, not a second point.
    {
        Anno moved = img;
        annoTranslate(moved, {100, 50});
        check(std::fabs(moved.a.x - 200) < 0.01f && std::fabs(moved.b.x - 800) < 0.01f && std::fabs(moved.b.y - 600) < 0.01f,
              "moving a picture keeps its size", failures);
    }

    // A quarter turn swaps the bounding box: 800x600 becomes 600 wide and 800 high.
    {
        Anno r = img;
        r.rotation = 90;
        Rect b = annoBounds(r);
        check(std::fabs(b.w - 600) < 0.6f && std::fabs(b.h - 800) < 0.6f,
              "a picture turned 90° swaps its bounding box", failures);
        // …and the hit test follows the rotation: a point that is inside the unrotated box
        // but outside the turned one must not hit, and vice versa.
        Vec2 ctr = annoImageCenter(r);
        bool insideTurned = annoHitTest(r, ctr + Vec2(0, 350), 1.f);   // 350 down: inside after the turn
        bool outsideTurned = annoHitTest(r, ctr + Vec2(350, 0), 1.f);  // 350 right: outside after the turn
        check(insideTurned && !outsideTurned && annoHitTest(r, ctr, 1.f),
              "hits follow the rotation (only what is on the picture counts)", failures);
        // The bounding box of the unrotated picture still contains that point, so this is a
        // real difference between the two hit tests and not a tautology.
        Anno r0 = img;
        r0.rotation = 0;
        check(annoHitTest(r0, ctr + Vec2(350, 0), 1.f), "the unrotated picture covers the same point", failures);
    }

    // Shift (keepRatio) holds the proportions while a corner is dragged; the opposite
    // corner stays put in the world.
    {
        Anno a = img;
        a.rotation = 0;
        a.a = {0, 0};
        a.b = {800, 400};  // 2:1
        Vec2 fixedCorner = annoImageCorner(a, 0);  // top-left, opposite the dragged one
        annoMoveHandle(a, 2, {1600, 500}, 800.f / 400.f, false);  // bottom-right, Shift
        check(std::fabs(a.b.x / a.b.y - 2.f) < 0.01f && std::fabs(a.b.x - 1600) < 0.6f && std::fabs(a.b.y - 800) < 0.6f,
              "Shift keeps a picture's proportions while scaling", failures);
        check(std::fabs(dist(annoImageCorner(a, 0), fixedCorner)) < 0.6f, "the opposite corner stays put", failures);
        // Without Shift the same drag is free: 1600x500 stays 3.2:1.
        Anno free = img;
        free.rotation = 0;
        free.a = {0, 0};
        free.b = {800, 400};
        annoMoveHandle(free, 2, {1600, 500});
        check(std::fabs(free.b.x - 1600) < 0.6f && std::fabs(free.b.y - 500) < 0.6f,
              "without Shift a picture's handles scale freely", failures);
        // …and neither drag can collapse the picture under kImageMinSide.
        Anno tiny = img;
        tiny.rotation = 0;
        tiny.a = {0, 0};
        tiny.b = {800, 400};
        annoMoveHandle(tiny, 2, {1, 1});
        check(tiny.b.x >= kImageMinSide - 0.01f && tiny.b.y >= kImageMinSide - 0.01f, "a picture cannot be dragged away to nothing", failures);
    }

    // The rotation handle: straight up means 0°, to the right 90°, and Shift steps 15°.
    {
        Anno a = img;
        a.rotation = 0;
        a.a = {0, 0};
        a.b = {800, 600};
        Vec2 c = annoImageCenter(a);
        annoMoveHandle(a, kAnnoRotateHandle, c + Vec2(0, -500));
        check(std::fabs(a.rotation) < 0.01f, "the rotation handle straight up means 0°", failures);
        annoMoveHandle(a, kAnnoRotateHandle, c + Vec2(500, 0));
        check(std::fabs(a.rotation - 90.f) < 0.01f, "dragging the rotation handle to the right turns 90°", failures);
        annoMoveHandle(a, kAnnoRotateHandle, c + Vec2(150, -500), 0.f, true);  // ~16.7° -> snapped
        check(std::fabs(a.rotation - 15.f) < 0.01f, "Shift snaps the rotation to 15° steps", failures);
        annoMoveHandle(a, kAnnoRotateHandle, c + Vec2(150, -500), 0.f, false);  // no snap: 16.7°
        check(std::fabs(a.rotation - 16.7f) < 0.2f, "without Shift the rotation stays where the pointer is", failures);
        // The handle is drawn above the top edge and follows the rotation.
        Anno up = img;
        up.rotation = 0;
        up.a = {0, 0};
        up.b = {800, 600};
        check(annoRotateHandleAt(up, 100).x > 399.9f && annoRotateHandleAt(up, 100).x < 400.1f &&
                  annoRotateHandleAt(up, 100).y < 0,
              "the rotation handle sits above the top edge", failures);
        up.rotation = 90;
        Vec2 turned = annoRotateHandleAt(up, 100);
        check(turned.x > 499.9f && std::fabs(turned.y - 300) < 0.2f, "the rotation handle follows the rotation", failures);
    }

    // The internal clipboard: copySelection puts the referenced pictures into the JSON,
    // and paste merges them into the content-addressed store (idempotently).
    {
        Document doc;
        Anno a1;
        a1.id = doc.newId();
        a1.view = SCH;
        a1.kind = AnnoKind::Image;
        a1.a = {0, 0};
        a1.b = {800, 600};
        a1.image = doc.addImage(blob);
        doc.annos.push_back(a1);
        Document clip;  // what copySelection() builds: the markup plus the pictures it uses
        clip.annos.push_back(a1);
        if (const ImageBlob* b = doc.image(a1.image)) clip.images.emplace(a1.image, *b);
        const std::string text = clip.toJson(false, true).dump(-1);
        check(text.find("\"images\"") != std::string::npos && text.find("\"library\"") == std::string::npos,
              "the clipboard carries the picture without the part library", failures);

        Document pasted;
        std::string err;
        bool okPaste = pasted.deserialize(text, &err);
        check(okPaste && pasted.annos.size() == 1 && pasted.image(a1.image) && pasted.image(a1.image)->bytes == blob.bytes,
              "the pasted clipboard has the picture bytes", failures);

        Document target;  // what App::paste does: merge the pictures, add copies of the markup
        for (auto& kv : pasted.images) target.images.emplace(kv.first, kv.second);
        for (auto& at : pasted.annos) {
            Anno n = at;
            n.id = target.newId();
            target.annos.push_back(n);
        }
        check(target.image(a1.image) != nullptr && target.annos.size() == 1 &&
                  target.annos.front().image == a1.image,
              "the pasted annotation points at a picture the document now holds", failures);

        // Undo restores a snapshot (keys only); the store must survive it, so redo shows
        // the picture again.
        const std::string snap = target.snapshot();
        check(snap.find("\"images\"") == std::string::npos && snap.find(github::base64Encode(blob.bytes)) == std::string::npos,
              "an undo snapshot carries the reference, not the picture bytes", failures);
        Document undone = target;
        undone.deserialize(snap, &err);
        const ImageBlob* afterUndo = undone.image(a1.image);
        check(afterUndo && afterUndo->bytes == blob.bytes && undone.annos.size() == 1,
              "undoing and redoing keeps the pasted picture", failures);

        // Save → load keeps mime, size and bytes.
        const std::string saved = target.serialize(true);
        Document reloaded;
        bool okLoad = reloaded.deserialize(saved, &err);
        const ImageBlob* back = okLoad ? reloaded.image(a1.image) : nullptr;
        check(okLoad && back && back->mime == "image/png" && back->w == 40 && back->h == 30 && back->bytes == blob.bytes,
              "a picture annotation saves and loads with its mime, size and bytes", failures);
    }

    // The insert path shared by the menu, a dropped file and the clipboard: it stores the
    // picture, sizes the annotation from the pixels and makes one undo step.
    {
        App app;
        app.ui.g = &app.g;
        app.ui.in = &app.in;
        app.ui.s = 1;
        app.view = SCH;
        app.cam[SCH].zoom = 1;
        app.cam[SCH].pan = {0, 0};
        app.rCanvas = {0, 0, 800, 600};
        Anno* made = app.addImageAnno(blob);
        const std::string key = imageKey(blob);
        check(made && made->kind == AnnoKind::Image && made->image == key && app.doc.image(key) &&
                  app.doc.annos.size() == 1 && app.selAnnos.size() == 1 && app.undoStack.size() == 1,
              "inserting a picture makes one annotation, one undo step, and selects it", failures);
        // 40x30 px at 12 world units per px is 480x360, which fits the 60% cap here.
        check(made && std::fabs(made->b.x - 480.f) < 0.6f && std::fabs(made->b.y - 360.f) < 0.6f &&
                  made->framed == false && made->fill == 0,
              "a new picture is sized from its pixels and has no frame or backdrop", failures);
        // The same bytes twice share one store entry.
        app.addImageAnno(blob);
        check(app.doc.images.size() == 1 && app.doc.annos.size() == 2, "the same picture is stored once", failures);
        // Garbage bytes are refused by the same path (and toast instead of throwing).
        check(!app.insertImageBytes("not a picture at all", "junk.bin"), "the insert path refuses bytes that are not a picture", failures);
        check(app.doc.annos.size() == 2 && app.doc.images.size() == 1, "a failed import adds nothing", failures);
    }
}

// ---- colour code calculator, component markings, monospaced ink ----
void colorCodeTests(int& failures) {
    printf("Colour code and markings\n");
    i18n::initEmbedded();
    i18n::setLang("en");
    using namespace cc;
    auto bandsOf = [](double ohms, int n, double tol = 5, int ppm = 100) {
 cc::Spec s; s.ohms = ohms; s.bands = n; s.tolerance = tol; s.ppm = ppm;
        return cc::encode(s);
    };
    auto same = [](const cc::Bands& b, std::initializer_list<int> want) {
 size_t i = 0;
        for (int c : want) if (i >= (size_t)b.n || b.color[i++] != c) return false;
        return (size_t)b.n == want.size();
    };
    check(same(bandsOf(4700, 4), {YELLOW, VIOLET, RED, GOLD}), "4.7k 4-band is yellow violet red gold", failures);
    check(same(bandsOf(1000, 3), {BROWN, BLACK, RED}), "1k 3-band has no tolerance band", failures);
    check(same(bandsOf(4700, 5, 1), {YELLOW, VIOLET, BLACK, BROWN, BROWN}), "4.7k 1% 5-band is yellow violet black brown brown", failures);
    check(same(bandsOf(0.47, 4), {YELLOW, VIOLET, SILVER, GOLD}), "0.47 ohm uses the silver multiplier", failures);
    check(same(bandsOf(4.7, 4), {YELLOW, VIOLET, GOLD, GOLD}), "4.7 ohm uses the gold multiplier", failures);
    check(same(bandsOf(10e3, 6, 0.1, 5), {BROWN, BLACK, BLACK, RED, VIOLET, VIOLET}), "10k 0.1% 5ppm 6-band", failures);
    {
        bool exact = true;
        cc::Spec s; s.ohms = 4750; s.bands = 4;
        Bands b = encode(s, &exact);
        check(!exact && b.n == 4, "4.75k does not fit 4 bands and is flagged", failures);
        s.bands = 5; encode(s, &exact);
        check(exact, "4.75k fits 5 bands", failures);
    }
    bool roundTrip = true;
    for (int n = 3; n <= 6; n++)
        for (double v : {0.1, 1.0, 4.7, 10.0, 220.0, 330.0, 1500.0, 4700.0, 47000.0, 1e6, 2.2e6, 1e8}) {
     cc::Spec s; s.bands = n; s.ohms = v; s.tolerance = n == 3 ? 20 : 2; s.ppm = 50;
     cc::Spec back;
     bool ok = decode(encode(s), back) && back.bands == n && std::fabs(back.ohms - v) < v * 1e-9 && back.tolerance == s.tolerance;
     if (n == 6) ok = ok && back.ppm == 50;
     if (!ok) { roundTrip = false; printf("    round trip failed: %d bands %g\n", n, v); }
        }
    check(roundTrip, "value -> bands -> value for 3, 4, 5 and 6 bands", failures);
    {
        Bands b; b.n = 4;
        b.color[0] = BROWN; b.color[1] = BLACK; b.color[2] = ORANGE; b.color[3] = GOLD;
 cc::Spec s;
 check(decode(b, s) && s.ohms == 10000 && s.tolerance == 5, "brown black orange gold reads 10k +-5%", failures);
        b.color[3] = ORANGE;
 check(!decode(b, s), "orange is not a tolerance colour", failures);
    }
    check(parseResistance("4k7") == 4700 && parseResistance("4R7") == 4.7 && parseResistance("R47") == 0.47 &&
   parseResistance("2M2") == 2.2e6 && parseResistance("4.7kΩ") == 4700 && parseResistance("100") == 100 && parseResistance("abc") < 0,
   "resistance text: 4k7, 4R7, R47, 2M2, 4.7kΩ, 100", failures);
    check(formatOhms(4700) == "4.7kΩ" && formatOhms(100) == "100Ω" && formatOhms(2.2e6) == "2.2MΩ" && formatOhms(0.47) == "0.47Ω", "resistance formatting", failures);
    check(resistorMarking(10000, 5) == "103" && resistorMarking(10000, 1) == "1002" && resistorMarking(4.7, 5) == "4R7" &&
          resistorMarking(220, 5) == "221" && resistorMarking(0.47, 5) == "R47", "SMD resistor markings (103, 1002, 4R7, 221)", failures);
    check(capacitorMarking(100e-9) == "104" && capacitorMarking(10e-9) == "103" && capacitorMarking(22e-12) == "22" &&
          capacitorMarking(4.7e-12) == "4p7" && capacitorMarking(1e-6) == "105", "capacitor markings (104, 103, 22, 4p7, 105)", failures);

    // the resistor part follows its properties, and the Inspector writes them back
    for (int n = 3; n <= 6; n++) {
        PartDefPtr d = getPartDef("resistor", {{"resistance", "4.7kΩ"}, {"bands", std::to_string(n)}});
 int bandRects = 0;
 for (auto& pr : d->gfx[BB]) if (pr.kind == Prim::RECT && pr.fill != 0 && pr.b.x - pr.a.x < 40 && pr.b.y - pr.a.y > 70) bandRects++;
 // 3 bands: two digits and a multiplier; the others add a tolerance band (and a tempco band)
        check(d && bandRects == n, (std::to_string(n) + "-band resistor draws that many bands").c_str(), failures);
    }
    check(getPartDef("resistor", {{"bands", "6"}})->propDefs.size() > getPartDef("resistor", {{"bands", "3"}})->propDefs.size(),
   "a 6-band resistor offers tolerance and temperature coefficient", failures);
    {
        App app;
        int id = app.doc.addPart("resistor", {{"resistance", "1kΩ"}}, {0, 0}).id;
 cc::Spec want; want.ohms = 4700; want.bands = 5; want.tolerance = 1;
        app.setPartProps(id, cc::propsFromSpec(want));
        const PartInst* p = app.doc.part(id);
 cc::Spec got = cc::specFromProps(mergedProps(p->type, p->props));
        check(got.ohms == 4700 && got.bands == 5 && got.tolerance == 1 && app.undoStack.size() == 1, "the calculator writes value, bands and tolerance in one undo step", failures);
 app.setPartProp(id, "resistance", "4k7");
        check(app.doc.part(id)->prop("resistance") == "4.7kΩ", "a typed 4k7 is stored as 4.7kΩ", failures);
    }

    // markings follow the value
    {
        Document d;
        auto inkOf = [&](int id) {
     std::string all;
     for (int v = 0; v < NUM_VIEWS; v++)
  if (v != SCH) for (auto& pr : d.part(id)->def->gfx[v]) if (pr.kind == Prim::TEXT) all += pr.text + "|";
     return all;
 };
        int c = d.addPart("capacitor_ceramic", {{"capacitance", "100nF"}}, {0, 0}).id;
 check(inkOf(c).find("104") != std::string::npos, "a 100nF ceramic is printed 104", failures);
        d.part(c)->props["capacitance"] = "10nF";
        d.part(c)->refresh();
        check(inkOf(c).find("103") != std::string::npos && inkOf(c).find("104") == std::string::npos, "changing the value reprints the capacitor (103)", failures);
 int e = d.addPart("capacitor_electrolytic", {{"capacitance", "470µF"}}, {0, 0}).id;
        check(inkOf(e).find("470µF") != std::string::npos, "an electrolytic shows its value", failures);
 int r = d.addPart("resistor", {{"resistance", "10kΩ"}, {"package", "0603 [SMD]"}}, {0, 0}).id;
        check(inkOf(r).find("103") != std::string::npos, "an SMD 10k resistor is printed 103", failures);
 int dp = d.addPart("diode", {{"part", "1N4148"}}, {0, 0}).id;
 check(inkOf(dp).find("1N4148") != std::string::npos, "a diode body shows its part number", failures);
        int t = d.addPart("timer555", {{"part", "LM555"}}, {0, 0}).id;
        check(inkOf(t).find("LM555") != std::string::npos, "a 555 shows the chosen part number", failures);
        d.part(r)->props["resistance"] = "10kΩ";
        check(d.part(r)->silkLabel() == d.part(r)->label + "  10kΩ", "the PCB silkscreen label carries the value", failures);
        d.part(r)->props["resistance"] = "22kΩ";
 check(d.part(r)->silkLabel() == d.part(r)->label + "  22kΩ", "and follows it when the value changes", failures);
    }
    {
 App app;
        check(!App::toolList().empty() && app.toolsMenu().size() == App::toolList().size(), "the tools menu lists every tool", failures);
        check(i18n::exists("tool.colorcode.name") && i18n::exists("menu.title.tools") && i18n::exists("welcome.tools"), "tool strings are translated", failures);
 i18n::setLang("zh-CN");
 check(tr("tool.colorcode.name") != "tool.colorcode.name" && tr("cc.color.red") == "红", "tool strings exist in Chinese", failures);
        i18n::setLang("en");
    }
}
static double spiceSI(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    double mul = 1.0;
    bool utf8Micro = s.size() >= 2 && (unsigned char)s[s.size() - 2] == 0xC2 && (unsigned char)s.back() == 0xB5;
    if (utf8Micro) { mul = 1e-6; s.resize(s.size() - 2); }
    else if (!s.empty()) {
        char c = s.back();
        if (c == 'p') mul = 1e-12;
        else if (c == 'n') mul = 1e-9;
        else if (c == 'u') mul = 1e-6;
        else if (c == 'm') mul = 1e-3;
        else if (c == 'k' || c == 'K') mul = 1e3;
        else if (c == 'M') mul = 1e6;
        else if (c == 'G') mul = 1e9;
        if (mul != 1.0) s.pop_back();
    }
    return std::atof(s.c_str()) * mul;
}
static int spiceCli(int argc, char** argv, int at) {
    if(at+1>=argc) { std::fprintf(stderr,"--spice-run requires a .dez file\\n"); return 2; }
    Document d; std::string err; if(!d.deserialize(readTextFile(argv[at+1]),&err)){ std::fprintf(stderr,"cannot read %s: %s\\n",argv[at+1],err.c_str()); return 2; }
    Connectivity c; c.build(d); SpiceOptions o; std::string outPath;
    for(int i=at+2;i<argc;i++){ std::string a=argv[i]; if(a=="--tran"&&i+2<argc){o.analysis=SpiceOptions::TRAN;o.tstep=spiceSI(argv[++i]);o.tstop=spiceSI(argv[++i]);} else if(a=="--ac"&&i+3<argc){o.analysis=SpiceOptions::AC;o.fstart=spiceSI(argv[++i]);o.fstop=spiceSI(argv[++i]);o.pointsPerDecade=std::atoi(argv[++i]); if(o.pointsPerDecade<1){std::fprintf(stderr,"--ac pointsPerDecade must be positive\n");return 2;}} else if(a=="--source"&&i+1<argc)o.dcSource=argv[++i]; else if(a=="--dc"&&i+4<argc){o.analysis=SpiceOptions::DC;o.dcSource=argv[++i];o.dcStart=spiceSI(argv[++i]);o.dcStop=spiceSI(argv[++i]);o.dcStep=spiceSI(argv[++i]);} else if(a=="--out"&&i+1<argc) outPath=argv[++i]; else {std::fprintf(stderr,"unknown or incomplete SPICE option: %s\n",a.c_str());return 2;} }
    std::string why; if(!ngspiceAvailable(&why)){ std::fprintf(stderr,"SPICE unavailable: %s\\nInstall ngspice or set DE_NGSPICE_LIB.\\n",why.c_str()); return 3; }
    AnalysisResult r; if(!runSpiceAnalysis(d,c,o,r)){ std::fprintf(stderr,"SPICE failed: %s\\n%s",r.error.c_str(),r.log.c_str()); return 4; }
    FILE* f=outPath.empty()?stdout:std::fopen(outPath.c_str(),"w"); if(!f){std::fprintf(stderr,"cannot write %s\\n",outPath.c_str());return 5;}
    std::fprintf(f,"scale"); for(auto& n:r.vecNames) std::fprintf(f,",%s",n.c_str()); std::fprintf(f,"\n"); for(size_t row=0;row<r.scale.size();row++){std::fprintf(f,"%.12g",r.scale[row]); for(auto& col:r.data) std::fprintf(f,",%.12g",row<col.size()?col[row]:0.0); std::fprintf(f,"\n");} if(f!=stdout)std::fclose(f); return 0;
}
int selftest() {
    int failures = 0;
    // The assertions below match the English source strings, so pin the language.
    i18n::initEmbedded();
    i18n::setLang("en");
    printf("designElectronics %s self-test\n", kAppVersion);
    printf("Part library: %zu entries\n", partLibrary().size());
    for (auto& e : partLibrary()) {
        PartDefPtr d = getPartDef(e.type, e.props);
        if (!d) { printf("  [FAIL] part %s\n", e.type.c_str()); failures++; continue; }
        // every pin id must be unique
        std::set<std::string> ids;
        for (auto& p : d->pins) ids.insert(p.id);
        if (ids.size() != d->pins.size()) { printf("  [FAIL] duplicate pin ids in %s\n", e.type.c_str()); failures++; }
        // JSON round trip of the definition
        PartDef back;
        std::string err;
        if (!partDefFromJson(partDefToJson(*d), back, &err) || back.pins.size() != d->pins.size() || back.gfx[BB].size() != d->gfx[BB].size()) {
            printf("  [FAIL] part JSON round trip %s: %s\n", e.type.c_str(), err.c_str());
            failures++;
        }
    }
    check(parseSI("4.7kΩ") == 4700, "parseSI 4.7kΩ", failures);
    check(std::fabs(parseSI("100nF") - 1e-7) < 1e-12, "parseSI 100nF", failures);
    check(std::fabs(parseSI("100µF") - 1e-4) < 1e-9, "parseSI 100µF", failures);

    for (size_t i = 0; i < examples().size(); i++) {
        Document d;
        examples()[i].build(d);
        d.syncWireEnds();
        printf("Example %zu: %s (%zu parts, %zu wires)\n", i, examples()[i].title.c_str(), d.parts.size(), d.wires.size());
        Connectivity c;
        c.build(d);
        RoutingStatus st[3];
        for (int v = 0; v < 3; v++) c.ratsnest(d, v, &st[v]);
        printf("  nets=%d  BB: %d/%d routed  SCH: %d/%d  PCB: %d/%d\n", c.numNets(), st[0].routedNets, st[0].nets, st[1].routedNets, st[1].nets, st[2].routedNets, st[2].nets);
        check(st[BB].connectionsToRoute == 0, "breadboard view fully wired", failures);
        // file round trip
        std::string text = d.serialize(true);
        Document d2;
        std::string err;
        bool ok = d2.deserialize(text, &err);
        check(ok && d2.parts.size() == d.parts.size() && d2.wires.size() == d.wires.size(), "save/load round trip", failures);
        if (ok) {
            Connectivity c2;
            c2.build(d2);
            check(c2.numNets() == c.numNets(), "nets identical after reload", failures);
        }
        // simulation
        SimResult sr = simulate(d, c, {});
        printf("  sim: %s\n", sr.message.c_str());
        for (auto& kv : sr.brightness) printf("    LED part %d brightness %.2f\n", kv.first, kv.second);
        for (auto& kv : sr.meterText) printf("    meter part %d reads %s\n", kv.first, kv.second.c_str());
        for (auto& w : sr.warnings) printf("    warning: %s\n", w.c_str());
        if (i == 0 || i == 1) check(!sr.brightness.empty() && sr.brightness.begin()->second > 0.3f, "LED lights up in simulation", failures);
        if (i == 4 || i == 5 || i == 6) {
            Simulator s2;
            int changes = 0;
            float prev = -1;
            for (int k = 0; k < 150; k++) {
                SimResult r2 = s2.step(d, c, {}, 0.01);
                float b = r2.brightness.empty() ? 0 : r2.brightness.begin()->second;
                bool on = b > 0.1f;
                if (prev >= 0 && on != (prev > 0.1f)) changes++;
                prev = b;
            }
            printf("  LED changed state %d times in 1.5 s\n", changes);
            check(changes >= 2, "example LED blinks in simulation", failures);
        }
        if (i == 2) {
            bool off = sr.brightness.empty() || sr.brightness.begin()->second < 0.05f;
            check(off, "LED off with button released", failures);
            std::set<int> pressed;
            for (auto& p : d.parts) if (p.type == "pushbutton") pressed.insert(p.id);
            SimResult on = simulate(d, c, pressed);
            check(!on.brightness.empty() && on.brightness.begin()->second > 0.3f, "LED on with button pressed", failures);
        }
        if (i == 3) {
            bool good = !sr.meterText.empty() && sr.meterText.begin()->second.rfind("4.", 0) == 0;
            check(good, "multimeter reads ~4.5V across the divider", failures);
        }
        // autoroute + DRC
        RouteResult rr = autoroute(d, c, true);
        c.build(d);
        c.ratsnest(d, PCB, &st[2]);
        printf("  autoroute: %s\n", rr.message.c_str());
        check(st[2].connectionsToRoute == 0, "PCB fully routed", failures);
        auto issues = runDrc(d, c);
        int copperIssues = 0;
        for (auto& is : issues) if (is.message.rfind("Too close", 0) == 0) { copperIssues++; if (copperIssues <= 3) printf("    drc: %s\n", is.message.c_str()); }
        for (auto& is : issues) if (is.message.rfind("Too close", 0) != 0) printf("    drc: %s\n", is.message.c_str());
        printf("  DRC: %zu issue(s), %d clearance\n", issues.size(), copperIssues);
        check(copperIssues == 0, "no clearance violations after autoroute", failures);
        // exports
        std::string svg = exportSvg(d, c, BB);
        check(svg.find("<svg") != std::string::npos, "SVG export", failures);
        check(exportBomCsv(d).find("Quantity") == 0, "BOM export", failures);
        check(exportNetlistXml(d, c).find("<net ") != std::string::npos, "netlist export", failures);
        check(exportSpice(d, c).find(".end") != std::string::npos, "SPICE export", failures);
        const char* tmp = SDL_GetPrefPath("designElectronics", "selftest");
        std::string zip = std::string(tmp ? tmp : "") + "gerber_test.zip";
        std::string msg;
        check(exportGerberZip(d, c, zip, &msg), "Gerber zip export", failures);
    }
    partDataTests(failures);
    modelTests(failures);
    simTests(failures);
    spiceExportTests(failures);
    ngspiceBackendTests(failures);
    colorCodeTests(failures);
    historyTests(failures);
    layerTests(failures);
    annoTests(failures);
    hierarchyTests(failures);
    hitTests(failures);
    textSelectionTests(failures);
    inlineAnnoTests(failures);
    textModeTests(failures);
    menuTests(failures);
    configTests(failures);
    toolTests(failures);
    shapeToolTests(failures);
    imageTests(failures);
    imageAnnoTests(failures);
    keyTests(failures);
    tidyTests(failures);
    videoTests(failures);
    printf(failures ? "\n%d FAILURE(S)\n" : "\nAll tests passed.\n", failures);
    return failures ? 1 : 0;
}

// ---- annotations: markup that never becomes copper ----
void annoTests(int& failures) {
    printf("Annotations\n");
    Document d;
    examples()[0].build(d);
    d.syncWireEnds();
    Connectivity c0;
    c0.build(d);
    const int nets0 = c0.numNets();
    const size_t rats0 = c0.ratsnest(d, BB).size();

    Anno rect;
    rect.id = d.newId(); rect.view = BB; rect.kind = AnnoKind::Rect; rect.a = {100, 200}; rect.b = {1600, 900};
    Anno line;
    line.id = d.newId(); line.view = SCH; line.kind = AnnoKind::Line; line.a = {0, 0}; line.b = {800, 600};
    line.dashed = false; line.stroke = rgba(60, 60, 64);
    Anno circ;
    circ.id = d.newId(); circ.view = PCB; circ.kind = AnnoKind::Circle; circ.a = {2000, 1500}; circ.b = {2600, 1500};
    Anno text;
    text.id = d.newId(); text.view = SCH; text.kind = AnnoKind::Text; text.a = {100, 100};
    annoSetText(text, "hello\nworld");
    text.hidden = true;
    text.font = FONT_MONO;
    d.annos = {rect, line, circ, text};

    std::string full = d.serialize(true), compact = d.compactJson(), err;
    Document back, share;
    bool okFull = back.deserialize(full, &err);
    bool okShare = share.deserialize(compact, &err);
    const Anno* r2 = okFull ? back.anno(rect.id) : nullptr;
    const Anno* l2 = okFull ? back.anno(line.id) : nullptr;
    const Anno* c2 = okFull ? back.anno(circ.id) : nullptr;
    const Anno* t2 = okFull ? back.anno(text.id) : nullptr;
    printf("  round trip: %s%zu annotations, compact form %s%zu\n", okFull ? "" : "FAILED ", back.annos.size(),
           okShare ? "" : "FAILED ", share.annos.size());
    check(okFull && okShare && back.annos.size() == 4 && share.annos.size() == 4,
          "annotations are saved in both the full and the compact form", failures);
    check(r2 && r2->kind == AnnoKind::Rect && r2->view == BB && std::fabs(r2->a.x - 100) < 0.6f && std::fabs(r2->b.y - 900) < 0.6f &&
              l2 && l2->view == SCH && !l2->dashed && l2->stroke == rgba(60, 60, 64) &&
              c2 && c2->view == PCB && std::fabs(dist(c2->a, c2->b) - 600) < 0.6f &&
              t2 && t2->hidden && t2->text == "hello\nworld" && t2->b.y > 0 && t2->font == FONT_MONO && r2->font == FONT_UI,
          "each kind keeps its geometry, view, style, text and font", failures);

    {   // an annotation kind from a newer build is skipped, the rest still load
        Json j = Json::parse(full);
        Json arr = j["annos"];
        Json bad = Json::object();
        bad.set("id", 9999);
        bad.set("kind", "arrow");
        bad.set("view", "breadboard");
        arr.push(bad);
        j.set("annos", arr);
        Document mixed;
        std::string e2;
        bool ok = mixed.deserialize(j.dump(-1), &e2);
        check(ok && mixed.annos.size() == 4, "an unknown annotation kind is skipped, the rest load", failures);
    }
    {   // a version 1 file has no "annos" at all: it must open, with no markup
        Document plain;
        examples()[0].build(plain);
        plain.syncWireEnds();
        Json j = Json::parse(plain.serialize(true));
        j.set("version", 1);
        std::string v1 = j.dump(-1);
        size_t at = v1.find("\"annos\":[");
        if (at != std::string::npos) v1.erase(at, v1.find(']', at) + 2 - at);
        Document old;
        std::string e2;
        bool ok = old.deserialize(v1, &e2);
        printf("  version 1 file: %s (%zu parts, %zu annotations)\n", ok ? "opens" : e2.c_str(), old.parts.size(), old.annos.size());
        check(ok && old.annos.empty() && old.parts.size() == plain.parts.size(),
              "a version 1 file opens with no annotations", failures);
    }
    {   // markup is never copper: no nets, no ratsnest, nothing in the exports
        Connectivity c;
        c.build(d);
        std::string svg = exportSvg(d, c, SCH);
        std::string net = exportNetlistXml(d, c);
        bool netSame = c.numNets() == nets0 && c.ratsnest(d, BB).size() == rats0;
        bool clean = svg.find("hello") == std::string::npos && net.find("hello") == std::string::npos;
        printf("  nets %d (was %d), ratsnest %zu (was %zu)\n", c.numNets(), nets0, c.ratsnest(d, BB).size(), rats0);
        check(netSame, "annotations over pins create no nets", failures);
        check(clean, "annotations stay out of the SVG and netlist exports", failures);
    }
    {   // hit testing: the shape, not its bounding box
        Anno box = rect;
        check(annoHitTest(box, {100, 550}, 20.f) && !annoHitTest(box, {800, 550}, 20.f),
              "a rectangle is hit on its frame, not through its middle", failures);
        Anno l = line;
        check(annoHitTest(l, {400, 300}, 5.f) && !annoHitTest(l, {400, 340}, 5.f), "a line is hit along its stroke", failures);
        check(annoHitTest(circ, {2600, 1500}, 5.f) && !annoHitTest(circ, {2400, 1500}, 5.f), "a circle is hit on its rim", failures);
        int hi = -1;
        bool rimHandle = annoHitHandle(circ, {2600, 1510}, 20.f, &hi) && hi == 1;
        bool centreHandle = annoHitHandle(circ, {2000, 1500}, 20.f, &hi) && hi == 0;
        check(rimHandle && centreHandle, "a circle offers a centre and a radius handle", failures);
        Anno box2 = rect;  // four corners, and dragging one keeps the opposite corner
        bool fourCorners = annoHandleCount(box2) == 4;
        Vec2 topRight = annoHandleAt(box2, 1);
        annoMoveHandle(box2, 1, {topRight.x + 300, topRight.y - 200});
        check(fourCorners && std::fabs(box2.a.x - rect.a.x) < 0.5f && std::fabs(box2.b.y - rect.b.y) < 0.5f &&
                  std::fabs(box2.b.x - (rect.b.x + 300)) < 0.5f,
              "a rectangle has four corner handles and the opposite corner stays", failures);
        check(annoBounds(circ).w >= 1199.f && std::fabs(annoBounds(circ).center().x - 2000) < 1.f, "bounds follow the shape", failures);
    }
    {   // text keeps its size when moved; its resize handle sits on the bottom-right corner
        Anno t = text;
        t.a = {500, 700};
        t.b = {1800, 160};
        Anno moved = t;
        annoTranslate(moved, {-900, 400});
        Anno r = rect;
        annoTranslate(r, {50, -50});
        Vec2 corner = annoHandleAt(t, 1);
        check(std::fabs(moved.a.x - (-400)) < 0.5f && std::fabs(moved.a.y - 1100) < 0.5f && std::fabs(moved.b.x - 1800) < 0.5f &&
                  std::fabs(moved.b.y - 160) < 0.5f && std::fabs(r.b.x - (rect.b.x + 50)) < 0.5f,
              "moving a text annotation keeps its box size (other shapes still move both points)", failures);
        check(std::fabs(corner.x - 2300) < 0.5f && std::fabs(corner.y - 860) < 0.5f,
              "a text annotation's resize handle is on its bottom-right corner", failures);
    }
    {   // moving and resizing an annotation is undoable like any other edit
        check(annoKindFromKey(annoKindKey(AnnoKind::Text), nullptr) && !annoKindFromKey("arrow", nullptr),
              "annotation kinds round trip through their key", failures);
        Anno t = text;
        Vec2 boxBefore = t.b;
        annoBumpTextSize(t, 40);
        check(t.textSize > kNoteTextSize && t.b.x == boxBefore.x && t.b.y == boxBefore.y,
              "a text box keeps its frame when its text size grows", failures);
        // A point text is the opposite: it has no frame, so bigger glyphs make a bigger box.
        Gfx gfx;
        Anno pt = text;
        pt.pointText = true;
        annoFitText(pt, gfx);
        Vec2 pointBefore = pt.b;
        annoBumpTextSize(pt, 40);
        annoFitText(pt, gfx);
        check(pt.b.x > pointBefore.x && pt.b.y > pointBefore.y, "a point text grows with its text size", failures);
    }
}

// ---- hierarchy: groups are levels, and groups never nest ----
void hierarchyTests(int& failures) {
    printf("Hierarchy\n");
    Document d;
    examples()[0].build(d);
    std::vector<int> ids;
    for (auto& p : d.parts) if (!p.def->isBoard && !p.def->isBreadboard) ids.push_back(p.id);
    check(ids.size() >= 3, "the example has parts to stack", failures);
    if (ids.size() < 3) return;

    std::vector<int> three = {ids[0], ids[1], ids[2]};
    bool grouped = d.groupParts(three);
    int gid = d.part(three[0]) ? d.part(three[0])->group : 0;
    int first = -1, last = -1, count = 0;
    for (size_t i = 0; i < d.parts.size(); i++)
        if (d.parts[i].group == gid && gid > 0) {
            if (first < 0) first = (int)i;
            last = (int)i;
            count++;
        }
    printf("  grouped %d parts as group %d at [%d..%d]\n", count, gid, first, last);
    check(grouped && gid > 0 && count == 3 && last - first + 1 == 3,
          "grouping puts the parts on one level", failures);

    std::string err;
    Document back;
    bool reread = back.deserialize(d.serialize(true), &err);
    bool sameGroup = reread && back.part(three[0]) && back.part(three[1]) && back.part(three[2]) &&
                     back.part(three[0])->group == gid && back.part(three[1])->group == gid && back.part(three[2])->group == gid;
    check(sameGroup && back.nextGroup > gid, "group ids survive save and load", failures);

    int idx0 = d.partIndex(three[1]);
    bool down = d.moveLevel(three[1], -1);
    int idxDown = d.partIndex(three[1]);
    bool up = d.moveLevel(three[1], +1);
    int idxUp = d.partIndex(three[1]);
    printf("  level move: idx %d -> %d -> %d (down %d, up %d)\n", idx0, idxDown, idxUp, (int)down, (int)up);
    check(down && idxDown < idx0 && up && idxUp == idx0, "a level moves down and back up as one block", failures);
    bool stillTogether = d.partIndex(three[0]) >= 0 && d.partIndex(three[2]) >= 0 &&
                         std::abs(d.partIndex(three[0]) - d.partIndex(three[2])) <= 2;
    check(stillTogether, "a moved level keeps its parts together", failures);

    int outsider = -1;
    for (auto& p : d.parts) if (p.group == 0) { outsider = p.id; break; }
    check(outsider > 0 && !d.groupParts({three[0], outsider}), "a part already in a group cannot join another", failures);

    d.ungroup(gid);
    check(d.part(three[0])->group == 0 && d.part(three[1])->group == 0 && d.part(three[2])->group == 0,
          "ungrouping returns every part to its own level", failures);

    {   // a group that loses a member is no longer a group
        Document e;
        examples()[0].build(e);
        std::vector<int> two;
        for (auto& p : e.parts) if (!p.def->isBoard && !p.def->isBreadboard) two.push_back(p.id);
        e.groupParts({two[0], two[1]});
        int g = e.part(two[0])->group;
        e.removePart(two[1]);
        check(g > 0 && e.part(two[0])->group == 0, "removing a member dissolves a two-part group", failures);
    }
    {   // a hand-edited file (or a removal) can leave members scattered: tidy them
        Document e;
        examples()[0].build(e);
        std::vector<int> four;
        for (auto& p : e.parts) if (!p.def->isBoard && !p.def->isBreadboard) four.push_back(p.id);
        if (four.size() < 3) return;
        e.part(four[0])->group = 7;
        e.part(four[2])->group = 7;
        e.nextGroup = 8;
        e.normalizeGroups();
        int s = -1, t = -1, n = 0;
        for (size_t i = 0; i < e.parts.size(); i++)
            if (e.parts[i].group == 7) { if (s < 0) s = (int)i; t = (int)i; n++; }
        printf("  scattered group: %d member(s) at [%d..%d]\n", n, s, t);
        check(n == 2 && t - s + 1 == 2, "scattered group members are pulled into one run", failures);
    }
}

// ---- text field selection: focus, keyboard, mouse, all on UTF-8 code-point boundaries ----
void textSelectionTests(int& failures) {
    printf("Text field selection\n");
    UI ui;
    Gfx gfx;
    Input input;
    ui.g = &gfx;
    ui.in = &input;
    std::string value = u8"a中b";
    Rect box{0, 0, 300, 80};
    ui.focusField("selection", true);
    ui.textField("selection", box, value, "", true);
    check(ui.fieldFocused("selection"), "focusField focuses requested text field", failures);
    input.text = "replacement";
    ui.textField("selection", box, value, "", true);
    check(value == "replacement", "typing replaces programmatically selected text", failures);
    input.endFrame();
    input.mods = SDL_KMOD_CTRL;
    input.keys = {SDLK_A};
    ui.textField("selection", box, value, "", true);
    input.endFrame();
    input.mods = 0;
    input.keys = {SDLK_BACKSPACE};
    ui.textField("selection", box, value, "", true);
    check(value.empty(), "Backspace deletes full selected range", failures);
    input.endFrame();
    value = u8"a中b";
    ui.focusField("area", false);
    ui.textArea("area", box, value);
    input.mods = SDL_KMOD_SHIFT;
    input.keys = {SDLK_LEFT, SDLK_LEFT};
    ui.textArea("area", box, value);
    input.endFrame();
    input.mods = 0;
    input.keys = {SDLK_DELETE};
    ui.textArea("area", box, value);
    check(value == "a", "Shift arrows and Delete preserve UTF-8 boundaries", failures);
    ui.clearFocus();
    check(!ui.fieldFocused("area"), "clearFocus clears field focus", failures);
    value = u8"a中b";
    input.mouse = {1, 35}; input.pressed = true; input.down = true;
    ui.textField("click", box, value, "", true);
    input.endFrame(); input.pressed = false; input.down = false;
    input.mouse = {20, 35}; input.pressed = true; input.doubleClick = true; input.down = true;
    ui.textField("click", box, value, "", true);
    input.endFrame(); input.pressed = false; input.down = false;
    input.keys = {SDLK_BACKSPACE};
    ui.textField("click", box, value, "", true);
    // "a中b" has no word break, so the double-click selects all of it (on code-point boundaries)
    check(value.empty(), "mouse click and double-click selection stay at UTF-8 boundaries", failures);
    ui.clearFocus(); input.endFrame();
    // Click positions from the real glyph widths: inside the field the text starts at x = 8 (inset).
    const float px = 14.f, x0 = 8.f, wa = gfx.textWidth("a", px), wz = gfx.textWidth(u8"中", px);
    printf("  glyph widths: a %.1f, 中 %.1f\n", wa, wz);
    auto clickThenBackspace = [&](const char* id, float x) {
        std::string v = u8"a中b";
        input.endFrame();
        input.mouse = {x, 35}; input.pressPos = input.mouse; input.pressed = true; input.down = true;
        ui.textField(id, box, v, "", true);
        input.endFrame(); input.down = false; input.released = true;
        ui.textField(id, box, v, "", true);
        input.endFrame();
        input.keys = {SDLK_BACKSPACE};
        ui.textField(id, box, v, "", true);
        input.endFrame();
        ui.clearFocus();
        return v;
    };
    if (wa > 0 && wz > 0) {
        std::string left = clickThenBackspace("left-half", x0 + wa + wz * 0.25f);
        std::string right = clickThenBackspace("right-half", x0 + wa + wz * 0.75f);
        printf("  backspace after a click on the left half of 中 -> \"%s\", right half -> \"%s\"\n", left.c_str(), right.c_str());
        check(left == u8"中b", "clicking left half positions at a UTF-8 boundary", failures);
        check(right == u8"ab", "clicking right half positions at a UTF-8 boundary", failures);
    } else {
        printf("  (no font metrics: click positioning not checked)\n");
    }
    {   // a click far past the end of a long, scrolled text puts the caret at its end
        std::string v(1200, 'x');
        input.endFrame();
        input.mouse = {290, 35}; input.pressPos = input.mouse; input.pressed = true; input.down = true;
        ui.textField("long-tail", box, v, "", true);
        input.endFrame(); input.down = false;
        input.keys = {SDLK_BACKSPACE};
        ui.textField("long-tail", box, v, "", true);
        bool focused = ui.fieldFocused("long-tail");
        input.endFrame();
        ui.clearFocus();
        check(focused && v.size() <= 1200 && v.find_first_not_of('x') == std::string::npos,
              "clicking near long-text tail keeps focus without overflow", failures);
    }
}

void inlineAnnoTests(int& failures) {
    printf("Inline annotation editing\n");
    App app;
    app.ui.g = &app.g;
    app.ui.in = &app.in;
    app.ui.s = 1;
    app.view = SCH;
    app.cam[SCH].zoom = 1;
    app.cam[SCH].pan = {0, 0};
    app.rCanvas = {0, 0, 800, 600};
    Anno anno;
    anno.id = app.doc.newId();
    anno.view = SCH;
    anno.kind = AnnoKind::Text;
    anno.a = {50, 50};
    annoSetText(anno, "placeholder");
    app.doc.annos.push_back(anno);
    app.pressHit.kind = Hit::ANNO;
    app.pressHit.anno = anno.id;
    app.drag = DRAG_PENDING;
    app.in.doubleClick = true;
    app.finishPointer();
    check(app.inlineAnnoEdit == anno.id, "double-click text annotation starts inline editing", failures);
    app.in.endFrame();
    app.drawInlineAnnoEditor();
    check(app.ui.fieldFocused("annoinline" + std::to_string(anno.id)), "inline editor receives field focus", failures);
    app.in.text = "first";
    app.drawInlineAnnoEditor();
    check(app.doc.anno(anno.id)->text == "first", "inline typing replaces fully selected placeholder", failures);
    app.in.endFrame();
    app.in.text = " second";
    app.drawInlineAnnoEditor();
    check(app.doc.anno(anno.id)->text == "first second" && app.undoStack.size() == 1, "live inline edits share one undo checkpoint", failures);
    app.in.endFrame();
    app.in.keys = {SDLK_RETURN};
    app.drawInlineAnnoEditor();
    check(app.inlineAnnoEdit < 0 && app.doc.anno(anno.id)->text == "first second", "Enter commits and closes inline editing", failures);
    app.in.endFrame();
    app.undo();
    check(app.doc.anno(anno.id)->text == "placeholder", "single undo restores text before inline session", failures);
    auto undoBefore = app.undoStack;
    auto redoBefore = app.redoStack;
    app.beginInlineAnnoEdit(anno.id);
    app.drawInlineAnnoEditor();
    app.in.text = "cancelled";
    app.drawInlineAnnoEditor();
    app.in.endFrame();
    app.in.keys = {SDLK_ESCAPE};
    app.drawInlineAnnoEditor();
    check(app.inlineAnnoEdit < 0 && app.doc.anno(anno.id)->text == "placeholder" && app.undoStack == undoBefore && app.redoStack == redoBefore, "Escape restores original annotation and undo history", failures);
    app.in.endFrame();
    app.readOnly = true;
    app.beginInlineAnnoEdit(anno.id);
    check(app.inlineAnnoEdit < 0, "read-only canvas cannot start inline editing", failures);
    app.readOnly = false;
    app.beginInlineAnnoEdit(anno.id);
    app.drawInlineAnnoEditor();
    app.in.pressed = true;
    app.in.mouse = {790, 590};
    app.drawInlineAnnoEditor();
    check(app.inlineAnnoEdit < 0, "clicking outside commits and closes inline editing", failures);
    app.in.endFrame();
    app.beginInlineAnnoEdit(anno.id);
    app.view = PCB;
    app.drawInlineAnnoEditor();
    check(app.inlineAnnoEdit < 0, "view change closes inline editing", failures);
    app.view = SCH;
    app.in.mouse = {80, 80};
    app.in.pressPos = app.in.mouse;
    app.in.down = app.in.pressed = app.in.doubleClick = true;
    app.canvasInput();
    check(app.inlineAnnoEdit == anno.id && !app.canvasOwnsPointer, "double-click down frame enters editor without a canvas drag", failures);
    app.drawInlineAnnoEditor();
    app.in.endFrame();
    app.in.text = "down-frame replacement";
    app.drawInlineAnnoEditor();
    check(app.doc.anno(anno.id)->text == "down-frame replacement", "double-click down frame preserves full initial selection", failures);
    app.in.endFrame();
    app.in.down = false;
    app.in.pressed = true;
    app.in.mouse = {80, 80};
    app.canvasInput();
    check(app.drag == DRAG_NONE && !app.canvasOwnsPointer, "inline editor pointer input cannot start canvas dragging", failures);
    app.in.endFrame();
    app.undo();
    app.drawInlineAnnoEditor();
    check(app.inlineAnnoEdit < 0 && app.doc.anno(anno.id)->text == "placeholder", "external undo closes stale inline session without rewriting text", failures);
    app.beginInlineAnnoEdit(anno.id);
    app.doc.removeAnno(anno.id);
    app.drawInlineAnnoEditor();
    check(app.inlineAnnoEdit < 0, "deleted annotation closes inline editor", failures);
}

// ---- point text vs text box: bounds that follow the glyphs, and a frame the user owns ----
void textModeTests(int& failures) {
    printf("Point text and text boxes\n");
    Gfx gfx;  // no fonts loaded: textWidth falls back to a deterministic estimate

    Anno point;
    point.id = 1;
    point.view = SCH;
    point.kind = AnnoKind::Text;
    point.a = {100, 200};
    point.pointText = true;
    annoSetText(point, "hello\nworlds");
    annoFitText(point, gfx);
    const float pad = kPointTextPad * point.textSize;
    const float longest = std::max(gfx.textWidth("hello", point.textSize, point.font),
                                   gfx.textWidth("worlds", point.textSize, point.font));
    check(std::fabs(point.b.x - (longest + 2 * pad)) < 0.01f, "point text width is its longest line plus the padding", failures);
    check(std::fabs(point.b.y - (2 * 1.35f * point.textSize + 2 * pad)) < 0.01f, "point text height is its line count plus the padding", failures);

    // No wrapping: a point text keeps the lines exactly as typed, however long they are.
    Anno longLine = point;
    annoSetText(longLine, "one line far wider than any box a user would draw around this note");
    annoFitText(longLine, gfx);
    check(std::fabs(longLine.b.y - (1.35f * longLine.textSize + 2 * kPointTextPad * longLine.textSize)) < 0.01f,
          "point text never wraps, so its height stays one line", failures);

    // Editing the text and changing the size both move the bounds.
    const Vec2 beforeEdit = point.b;
    annoSetText(point, "hello\nworlds\nthird line");
    annoFitText(point, gfx);
    check(point.b.y > beforeEdit.y + 1 && point.b.x >= beforeEdit.x, "editing a point text updates its bounds", failures);
    const Vec2 beforeSize = point.b;
    point.textSize = 54.f;
    annoFitText(point, gfx);
    check(point.b.y < beforeSize.y * 0.6f && point.b.x < beforeSize.x, "a smaller point text gets a smaller box", failures);
    point.textSize = kNoteTextSize;
    annoFitText(point, gfx);

    // A text box owns its size: neither the text nor the size moves its frame.
    Anno box;
    box.id = 2;
    box.view = SCH;
    box.kind = AnnoKind::Text;
    box.a = {0, 0};
    box.b = {1800, 400};
    annoSetText(box, "one");
    const Vec2 frame = box.b;
    annoSetText(box, "one\ntwo\nthree\nfour");
    check(box.b.x == frame.x && box.b.y == frame.y, "a text box keeps its size when the text changes", failures);
    annoBumpTextSize(box, 8);
    check(box.b.x == frame.x && box.b.y == frame.y && box.textSize > kNoteTextSize, "a text box keeps its frame when its text grows", failures);

    // JSON: "point" is written only for point text; a file without it reads as a text box.
    Json pointJson = annoToJson(point);
    check(pointJson["point"].boolean(false), "point text saves its point flag", failures);
    Anno pointBack;
    check(annoFromJson(pointJson, &pointBack) && pointBack.pointText &&
              std::fabs(pointBack.b.x - point.b.x) < 0.01f && std::fabs(pointBack.b.y - point.b.y) < 0.01f,
          "point text round-trips through JSON", failures);
    Json boxJson = annoToJson(box);
    check(!boxJson["point"].boolean(false), "a text box writes no point flag", failures);
    Anno boxBack;
    check(annoFromJson(boxJson, &boxBack) && !boxBack.pointText, "a text box round-trips as a text box", failures);
    Json legacy = Json::object();
    legacy.set("id", 3);
    legacy.set("view", viewKey(SCH));
    legacy.set("kind", "text");
    Json legacyA = Json::array(), legacyB = Json::array();
    legacyA.push(Json(10.0)); legacyA.push(Json(20.0));
    legacyB.push(Json(1500.0)); legacyB.push(Json(300.0));
    legacy.set("a", legacyA);
    legacy.set("b", legacyB);
    legacy.set("text", "older note");
    Anno legacyBack;
    check(annoFromJson(legacy, &legacyBack) && !legacyBack.pointText &&
              std::fabs(legacyBack.b.x - 1500.f) < 0.01f && std::fabs(legacyBack.b.y - 300.f) < 0.01f,
          "older text annotations load as text boxes", failures);

    // ---- the text tool: click makes point text, drag makes a text box ----
    App app;
    app.ui.g = &app.g;
    app.ui.in = &app.in;
    app.ui.s = 1;
    app.view = SCH;
    app.cam[SCH].zoom = 1;
    app.cam[SCH].pan = {0, 0};
    app.cam[SCH].valid = true;
    app.rCanvas = {0, 0, 800, 600};

    app.setTool(TOOL_TEXT);
    app.in.mouse = {100, 100};
    app.in.pressed = true;
    app.in.down = true;
    app.canvasInput();
    check(app.annoDrawing && app.canvasOwnsPointer, "text tool press starts sizing a text", failures);
    app.in.endFrame();
    app.in.mouse = {102, 101};
    app.in.down = false;
    app.in.released = true;
    app.canvasInput();
    app.in.endFrame();
    const int pointId = app.doc.annos.empty() ? -1 : app.doc.annos.back().id;
    const Anno* madePoint = app.doc.anno(pointId);
    check(madePoint && madePoint->kind == AnnoKind::Text && madePoint->pointText, "a click with the text tool makes point text", failures);
    check(madePoint && std::fabs(madePoint->a.x - 100) < 0.01f && std::fabs(madePoint->a.y - 100) < 0.01f, "the point text lands where it was clicked", failures);
    check(app.inlineAnnoEdit == pointId && app.tool == TOOL_SELECT, "a new point text opens for typing straight away", failures);
    // The in-place editor grows with what is typed.
    app.in.text = "a";
    app.drawInlineAnnoEditor();
    app.in.endFrame();
    const float narrow = app.inlineAnnoRect().w;
    app.in.text = " and then a good deal more text";
    app.drawInlineAnnoEditor();
    app.in.endFrame();
    check(app.inlineAnnoRect().w > narrow + 20, "the point text editor grows with its content", failures);
    app.finishInlineAnnoEdit();

    app.setTool(TOOL_TEXT);
    app.in.mouse = {200, 150};
    app.in.pressed = true;
    app.in.down = true;
    app.canvasInput();
    app.in.endFrame();
    app.in.mouse = {700, 400};
    app.canvasInput();  // dragging: the dashed preview follows the pointer
    app.in.endFrame();
    app.in.released = true;
    app.in.down = false;
    app.canvasInput();
    app.in.endFrame();
    const Anno* madeBox = app.doc.annos.empty() ? nullptr : &app.doc.annos.back();
    check(madeBox && madeBox->kind == AnnoKind::Text && !madeBox->pointText, "dragging with the text tool makes a text box", failures);
    check(madeBox && std::fabs(madeBox->b.x - 500) < 0.01f && std::fabs(madeBox->b.y - 250) < 0.01f, "the text box gets the dragged size", failures);
    check(app.inlineAnnoEdit == (madeBox ? madeBox->id : -1), "a new text box opens for typing straight away", failures);
    check(madeBox && std::fabs(app.inlineAnnoRect().w - 500) < 1.f, "the text box editor uses the frame size", failures);
    app.finishInlineAnnoEdit();

    // ---- point text has no frame: its corner handle scales the glyphs proportionally ----
    Anno scaled;
    scaled.id = 9;
    scaled.view = SCH;
    scaled.kind = AnnoKind::Text;
    scaled.a = {0, 0};
    scaled.pointText = true;
    annoSetText(scaled, "scale me");
    annoFitText(scaled, gfx);
    const float sizeBefore = scaled.textSize;
    const Vec2 corner = annoHandleAt(scaled, 1);
    check(std::fabs(corner.x - scaled.b.x) < 0.01f && std::fabs(corner.y - scaled.b.y) < 0.01f, "the point text corner handle sits on the fitted corner", failures);
    annoMoveHandle(scaled, 1, Vec2(corner.x * 1.5f, corner.y * 1.5f));
    check(std::fabs(scaled.textSize - sizeBefore * 1.5f) < 0.5f, "dragging a point text corner scales its text size proportionally", failures);
    annoFitText(scaled, gfx);
    check(std::fabs(scaled.b.x - corner.x * 1.5f) < 2.f && std::fabs(scaled.b.y - corner.y * 1.5f) < 2.f,
          "the point text box follows the scaled text", failures);
    check(annoHitTest(scaled, {scaled.a.x + scaled.b.x - 1, scaled.a.y + scaled.b.y / 2}, 0), "the fitted point text box is its hit box", failures);
    // A text box corner still resizes the frame, never below the minimum.
    Anno smallBox;
    smallBox.kind = AnnoKind::Text;
    smallBox.a = {0, 0};
    smallBox.b = {1800, 400};
    annoMoveHandle(smallBox, 1, Vec2(40, 30));
    check(std::fabs(smallBox.b.x - kTextBoxMinW) < 0.01f && std::fabs(smallBox.b.y - kTextBoxMinH) < 0.01f,
          "a text box never shrinks below its minimum size", failures);
}

// ---- pointer hit priority, left/middle button semantics, the red "unconnected" dot ----
void hitTests(int& failures) {
    printf("Pointer hit priority\n");
    App app;
    app.ui.s = 1.f;
    app.view = SCH;
    app.cam[SCH].zoom = 1.f;
    app.cam[SCH].pan = Vec2(200, 200);
    app.cam[SCH].valid = true;
    const int rid = app.doc.addPart("74hc00", {}, {1000, 1000}).id;  // a body big enough to miss the wire in
    const Rect body = app.doc.part(rid)->worldBounds(SCH);
    const Vec2 mid = body.center();
    {
        Wire wr;
        wr.id = app.doc.newId();
        wr.view = SCH;
        wr.pts = {Vec2(body.x - 500, mid.y), Vec2(body.r() + 500, mid.y)};
        app.doc.wires.push_back(wr);
    }
    auto hitAtPx = [&](float dxPx, float dyPx, float zoom) {
        app.cam[SCH].zoom = zoom;
        Vec2 world = mid + Vec2(dxPx, dyPx) / zoom;
        return app.hitTest(app.toScreen(world));
    };
    bool wireWins = true, partWins = true;
    for (float z : {0.25f, 1.f, 4.f}) {
        Hit on = hitAtPx(0, 4, z), off = hitAtPx(0, 12, z);
        printf("  zoom %.2fx: 4 px off the wire -> kind %d, 12 px off -> kind %d\n", z, (int)on.kind, (int)off.kind);
        if (on.kind != Hit::WIRE_SEG) wireWins = false;
        if (off.kind != Hit::PART && off.kind != Hit::PIN) partWins = false;
    }
    check(wireWins, "a wire crossing a part is picked within 6 px of its centre line, at any zoom", failures);
    check(partWins, "off the wire the part under the pointer is picked, at any zoom", failures);
    check(app.hitTest(app.toScreen(mid + Vec2(6000, 6000))).kind == Hit::NONE, "empty canvas hits nothing", failures);

    {   // a left drag on a segment moves the whole wire and adds no bend point
        size_t ptsBefore = app.doc.wires[0].pts.size();
        app.pressHit = Hit();
        app.pressHit.kind = Hit::WIRE_SEG;
        app.pressHit.wire = app.doc.wires[0].id;
        app.pressHit.index = 0;
        app.pressWorld = mid;
        app.in.mouse = app.toScreen(mid);
        app.in.mods = 0;
        app.startDrag();
        printf("  left drag: %zu -> %zu point(s), mode %d\n", ptsBefore, app.doc.wires[0].pts.size(), (int)app.drag);
        check(app.doc.wires[0].pts.size() == ptsBefore && app.drag == DRAG_MOVE,
              "a left drag on a segment moves the wire instead of adding a bend", failures);
        app.drag = DRAG_NONE;
        app.endEdit();
    }
    {   // a selected annotation's handle is grabbable, and the shape is not
        Anno a;
        a.id = app.doc.newId();
        a.view = SCH;
        a.kind = AnnoKind::Rect;
        a.a = mid + Vec2(-2500, 2500);
        a.b = mid + Vec2(2500, 4000);
        app.doc.annos.push_back(a);
        app.cam[SCH].zoom = 1.f;
        app.cam[SCH].pan = Vec2(0, 0);
        Hit plain = app.hitTest(app.toScreen(a.b));
        app.selAnnos = {a.id};
        Hit handle = app.hitTest(app.toScreen(a.b));
        printf("  annotation handle: unselected -> %d, selected -> %d\n", (int)plain.kind, (int)handle.kind);
        check(handle.kind == Hit::ANNO_HANDLE && plain.kind != Hit::ANNO_HANDLE,
              "the resize handle is grabbable only once the annotation is selected", failures);
        app.selAnnos.clear();
    }
    {   // a text label on a breadboard is clicked, not the breadboard (nor its holes) under it
        App b;
        b.ui.s = 1.f;
        b.view = BB;
        b.cam[BB].zoom = 1.f;
        b.cam[BB].pan = Vec2(0, 0);
        b.cam[BB].valid = true;
        const int bbId = b.doc.addPart("breadboard", {{"size", "half+"}}, {0, 0}).id;
        const Vec2 hole = b.doc.part(bbId)->pinPos(0, BB);
        Anno t;
        t.id = b.doc.newId();
        t.view = BB;
        t.kind = AnnoKind::Text;
        t.text = "label";
        t.a = hole - Vec2(200, 60);
        t.b = Vec2(800, 120);
        b.doc.annos.push_back(t);
        Hit onHole = b.hitTest(b.toScreen(hole));
        Hit onBody = b.hitTest(b.toScreen(t.a + Vec2(20, 20)));
        b.doc.annos.clear();
        Hit holeAlone = b.hitTest(b.toScreen(hole));
        printf("  text on breadboard: on a hole -> %d, off the holes -> %d, text removed -> %d\n", (int)onHole.kind,
              (int)onBody.kind, (int)holeAlone.kind);
        check(onHole.kind == Hit::ANNO && onHole.anno == t.id && onBody.kind == Hit::ANNO && onBody.anno == t.id,
              "a text label on a breadboard is hit instead of the breadboard and its holes", failures);
        check(holeAlone.kind == Hit::PIN && holeAlone.part == bbId, "without markup the breadboard hole is hit as before",
              failures);
    }

    {   // middle button: bend in on a segment, bend out on a bend, pan elsewhere
        App a;
        a.ui.s = 1.f;
        a.view = SCH;
        a.cam[SCH].zoom = 1.f;
        a.cam[SCH].pan = Vec2(0, 0);
        a.cam[SCH].valid = true;
        a.snapToGrid = false;  // exact positions, so the new bend point is predictable
        const int pid = a.doc.addPart("74hc00", {}, {1000, 1000}).id;
        const Rect b = a.doc.part(pid)->worldBounds(SCH);
        Wire w;
        w.id = a.doc.newId();
        w.view = SCH;
        w.pts = {Vec2(b.x - 500, b.center().y), Vec2(b.r() + 500, b.center().y)};
        a.doc.wires.push_back(w);
        const int wid = w.id;
        const size_t pts0 = a.doc.wire(wid)->pts.size();
        Vec2 onSeg = b.center() + Vec2(300, 0);
        a.in.mouse = a.toScreen(onSeg);  // the middle button works where the pointer is
        bool panOnSeg = a.middleButtonPress(a.toScreen(onSeg));
        size_t pts1 = a.doc.wire(wid)->pts.size();
        Vec2 bend = a.doc.wire(wid)->pts[1];
        a.in.mouse = a.toScreen(bend);
        bool panOnBend = a.middleButtonPress(a.toScreen(bend));
        size_t pts2 = a.doc.wire(wid)->pts.size();
        bool panOnBlank = a.middleButtonPress(a.toScreen(b.center() + Vec2(0, 4000)));
        size_t pts3 = a.doc.wire(wid)->pts.size();
        printf("  middle button: segment %zu pts (pan %d), bend %zu pts (pan %d), blank %zu pts (pan %d)\n", pts1, (int)panOnSeg, pts2,
               (int)panOnBend, pts3, (int)panOnBlank);
        check(!panOnSeg && pts1 == pts0 + 1 && std::fabs(bend.y - onSeg.y) < 0.5f,
              "the middle button adds a bend point on a wire segment", failures);
        check(!panOnBend && pts2 == pts0, "the middle button removes a bend point", failures);
        check(panOnBlank && pts3 == pts0, "the middle button on empty canvas pans and adds no bend", failures);
        a.drag = DRAG_NONE;
    }

    {   // the proximity grid answers exactly like the old full scan, at 200 wires
        App g;
        g.ui.s = 1.f;
        g.view = SCH;
        g.cam[SCH].zoom = 1.f;
        g.cam[SCH].valid = true;
        const int wires = 200;
        for (int i = 0; i < wires; i++) {
            Wire w;
            w.id = g.doc.newId();
            w.view = SCH;
            // every third wire shares an end with the previous one, the rest stand alone
            float x = (float)((i / 3) * 600);
            float y = (float)((i % 3) * 400);
            w.pts = {Vec2(x, y), Vec2(x + 400, y)};
            if (i % 3 == 2) w.pts.back() = Vec2(x + 400, y - 400);  // a bend, so segments matter
            g.doc.wires.push_back(w);
        }
        // and one wire whose free end sits on another wire's middle
        Wire cross;
        cross.id = g.doc.newId();
        cross.view = SCH;
        cross.pts = {Vec2(200, 1500), Vec2(200, 2500)};
        g.doc.wires.push_back(cross);

        int mismatches = 0;
        for (auto& w : g.doc.wires) {
            for (int e = 0; e < 2; e++) {
                Vec2 p = e == 0 ? w.pts.front() : w.pts.back();
                float tol = g.wireJoinTol(w.view);
                bool brute = false;
                for (auto& o : g.doc.wires) {
                    if (o.id == w.id) continue;
                    if (dist(o.pts.front(), p) <= tol || dist(o.pts.back(), p) <= tol) brute = true;
                    for (size_t k = 0; k + 1 < o.pts.size(); k++)
                        if (distPointSeg(p, o.pts[k], o.pts[k + 1]) <= tol) brute = true;
                }
                if (brute != g.wireEndConnected(w, e)) mismatches++;
            }
        }
        printf("  %d wires, %d disagreement(s) with the full scan\n", (int)g.doc.wires.size(), mismatches);
        check(mismatches == 0, "the cached wire proximity answers exactly like a full scan", failures);
    }

    printf("Unconnected wire ends\n");
    {
        App a;
        a.ui.s = 1.f;
        a.view = SCH;
        a.cam[SCH].zoom = 1.f;
        a.cam[SCH].valid = true;
        const int p1 = a.doc.addPart("resistor", {}, {0, 0}).id;
        const int p2 = a.doc.addPart("resistor", {}, {3000, 0}).id;
        Wire w;
        w.id = a.doc.newId();
        w.view = SCH;
        w.ends[0] = {p1, 0};
        w.ends[1] = {p2, 0};
        w.pts = {a.doc.part(p1)->pinPos(0, SCH), a.doc.part(p2)->pinPos(0, SCH)};
        a.doc.wires.push_back(w);
        const int wid = w.id;
        bool both = a.wireEndConnected(*a.doc.wire(wid), 0) && a.wireEndConnected(*a.doc.wire(wid), 1);
        // a part moved away leaves ends[] attached but the point in mid-air
        a.doc.wire(wid)->pts.back() = a.doc.wire(wid)->pts.back() + Vec2(0, 900);
        bool staleRed = !a.wireEndConnected(*a.doc.wire(wid), 1);
        bool otherGreen = a.wireEndConnected(*a.doc.wire(wid), 0);
        // the frame the end snaps onto a pin, before finishPointer() writes ends[]
        a.drag = DRAG_WIRE_END;
        a.dragWire = wid;
        a.dragWireEnd = 1;
        a.wireTargetPart = p2;
        bool snapping = a.wireEndConnected(*a.doc.wire(wid), 1);
        a.drag = DRAG_NONE;
        a.wireTargetPart = -1;
        // a free end touching another wire's segment is connected too
        Vec2 tip = a.doc.wire(wid)->pts.back();
        Wire o;
        o.id = a.doc.newId();
        o.view = SCH;
        o.pts = {tip + Vec2(-300, 0), tip + Vec2(300, 0)};
        a.doc.wires.push_back(o);
        bool joined = a.wireEndConnected(*a.doc.wire(wid), 1);
        // deleting the part brings the dot back at once
        a.doc.removePart(p1);
        bool redAfterDelete = !a.wireEndConnected(*a.doc.wire(wid), 0);
        printf("  both ends %d, stale end red %d (other end %d), snapping %d, joined %d, after delete %d\n", (int)both, (int)staleRed,
               (int)otherGreen, (int)snapping, (int)joined, (int)redAfterDelete);
        check(both, "a wire snapped onto pins at both ends has no red dot", failures);
        check(staleRed && otherGreen, "only the end left behind by a moved part turns red", failures);
        check(snapping, "the red dot disappears on the frame the end snaps", failures);
        check(joined, "a free end touching another wire counts as connected", failures);
        check(redAfterDelete, "deleting the part turns its wire ends red again", failures);
    }
}

// ---- config.toml: the preferences file, its migration and its fallbacks ----
void configTests(int& failures) {
    printf("Preferences (config.toml)\n");
    const std::string src =
        "# interface language, kept from the last run\n"
        "language = \"en\"\n"
        "unknown_setting = 7   # written by hand, not by this build\n"
        "\n"
        "[window]\n"
        "width = 1024   # pixels\n"
        "palette = [\"top\", \"bottom\"]\n";
    toml::Doc d;
    std::string err;
    bool parsed = d.parse(src, &err);
    printf("  parse: %s, %zu keys\n", parsed ? "ok" : err.c_str(), d.v.size());
    check(parsed && d.get("language") == "en" && d.getInt("window.width") == 1024 && d.get("window.palette") == "[\"top\", \"bottom\"]",
          "toml: values are read back by their section.key name", failures);

    d.set("language", "zh-CN");
    d.set("note", "he said \"hi\"");
    d.setInt("window.height", 768);
    d.setBool("window.maximized", true);
    std::string out = d.dump();
    bool keptComments = out.find("# interface language, kept from the last run") != std::string::npos &&
                        out.find("width = 1024   # pixels") != std::string::npos &&
                        out.find("unknown_setting = 7   # written by hand, not by this build") != std::string::npos &&
                        out.find("palette = [\"top\", \"bottom\"]") != std::string::npos;
    bool wroteValues = out.find("language = \"zh-CN\"") != std::string::npos &&
                       out.find("note = \"he said \\\"hi\\\"\"") != std::string::npos &&
                       out.find("height = 768") != std::string::npos && out.find("maximized = true") != std::string::npos;
    toml::Doc back;
    bool reparsed = back.parse(out, &err);
    printf("  written file: %s\n", reparsed ? "reads back" : err.c_str());
    check(keptComments, "toml: comments, unknown keys and formatting survive a write", failures);
    check(wroteValues, "toml: changed keys are replaced in place and new ones appended to their section", failures);
    check(reparsed && back.get("language") == "zh-CN" && back.get("note") == "he said \"hi\"" && back.getInt("window.height") == 768 &&
              back.getBool("window.maximized"),
          "toml: the written file reads back with the new values", failures);
    check(out.rfind("# interface language", 0) == 0 && out.find("\nheight = 768") < out.find("maximized = true"),
          "toml: the header comment stays first and new keys are grouped", failures);

    {   // a damaged file is reported with a line, it does not take the editor down
        toml::Doc bad;
        std::string e2;
        bool ok = bad.parse("this is not a toml line\n", &e2);
        toml::Doc bad2;
        std::string e3;
        bool ok2 = bad2.parse("[window\nwidth = 1\n", &e3);
        printf("  damaged: %s / %s\n", e2.c_str(), e3.c_str());
        check(!ok && !e2.empty() && !ok2 && !e3.empty(), "toml: a damaged file is reported, not silently half-read", failures);
    }

    {   // settings.json (the old format) becomes a config.toml, language included
        bool ok = false;
        std::string migrated = settingsMigrateJson("{\"version\": 1, \"language\": \"en\", \"maximized\": true, \"zoom\": 2}", &ok);
        toml::Doc m;
        std::string e2;
        bool mp = m.parse(migrated, &e2);
        printf("  migrated: %s\n", migrated.substr(0, migrated.find('\n')).c_str());
        check(ok && mp && migrated.rfind("# migrated from settings.json on ", 0) == 0,
              "an old settings.json turns into config.toml with a note about where it came from", failures);
        check(m.get("language") == "en" && m.getBool("maximized") && m.getInt("zoom") == 2 && m.v.count("version") == 0,
              "the migration keeps every setting and drops the JSON schema version", failures);
        bool ok2 = true;
        settingsMigrateJson("not json at all", &ok2);
        check(!ok2, "a damaged settings.json is refused instead of migrated", failures);
    }

    {   // the settings API keeps its old shape: same names, same defaults
        std::string path = settingsPath();
        printf("  path: %s\n", path.empty() ? "(none)" : path.c_str());
        check(path.size() >= 11 && path.compare(path.size() - 11, 11, "config.toml") == 0,
              "preferences live in config.toml", failures);
    }
}

// Keeps the real preferences file untouched while a test writes to it: the settings
// API is the only entry point, so the test has to go through it.
struct PrefsGuard {
    std::string path, bak, text, bakText;
    bool had = false, hadBak = false;
    PrefsGuard() {
        path = settingsPath();
        bak = path + ".bak";
        text = readTextFile(path, &had);
        bakText = readTextFile(bak, &hadBak);
    }
    ~PrefsGuard() {
        if (had) writeTextFile(path, text);
        else if (!path.empty()) SDL_RemovePath(path.c_str());
        if (hadBak) writeTextFile(bak, bakText);
        else if (!bak.empty()) SDL_RemovePath(bak.c_str());
    }
};

// ---- markup tools and the wire colour new wires inherit ----
void toolTests(int& failures) {
    printf("Markup tools\n");
    App a;
    a.ui.s = 1.f;
    a.view = BB;
    a.cam[BB].zoom = 1.f;
    a.cam[BB].pan = Vec2(0, 0);
    a.cam[BB].valid = true;
    a.snapToGrid = false;
    a.createAnnoFromDrag(TOOL_RECT, {100, 100}, {900, 700});
    a.createAnnoFromDrag(TOOL_CIRCLE, {2000, 500}, {2400, 500});
    a.createAnnoFromDrag(TOOL_LINE, {3000, 500}, {3000, 500});
    const Anno& circ = a.doc.annos[1];
    const Anno& clickLine = a.doc.annos[2];
    printf("  %zu annotations, circle radius %.0f, click-line length %.0f, undo steps %zu\n", a.doc.annos.size(),
           (double)dist(circ.a, circ.b), (double)dist(clickLine.a, clickLine.b), a.undoStack.size());
    check(a.doc.annos.size() == 3 && a.doc.annos[0].kind == AnnoKind::Rect && circ.kind == AnnoKind::Circle &&
              clickLine.kind == AnnoKind::Line,
          "the tools make a rectangle, a circle and a line", failures);
    check(std::fabs(dist(circ.a, circ.b) - 400) < 0.5f, "a circle drag sets the radius", failures);
    check(dist(clickLine.a, clickLine.b) > 0, "a click without a drag still makes a usable shape", failures);
    check(a.undoStack.size() == 3, "each drawn shape is exactly one undo step", failures);
    check(a.selAnnos.size() == 1 && a.selAnnos.count(clickLine.id) > 0, "the new shape is selected", failures);
    a.undo();
    check(a.doc.annos.size() == 2, "one undo removes the last shape", failures);
    a.setTool(TOOL_TEXT);
    bool toolOn = a.tool == TOOL_TEXT && a.selAnnos.empty();
    a.cancelAnnoTool();
    check(toolOn && a.tool == TOOL_SELECT, "a tool can be picked and cancelled", failures);

    printf("Remembered wire colour\n");
    PrefsGuard guard;  // put the user's config.toml back when we are done
    {
        App b;
        b.ui.s = 1.f;
        b.view = BB;
        b.cam[BB].zoom = 1.f;
        b.cam[BB].valid = true;
        Wire w;
        w.id = b.doc.newId();
        w.view = BB;
        w.pts = {{0, 0}, {500, 0}};
        b.doc.wires.push_back(w);
        const int wid = w.id;
        const uint32_t red = rgba(220, 50, 50);
        b.setWireColor(BB, wid, red);
        bool wireRed = b.doc.wire(wid)->color == red;
        bool memRed = b.wireColorMem[BB] == red;
        bool stored = parseColor(settingsGet("wire.color.bb", ""), 0) == red;
        printf("  bb memory %s, stored %s\n", colorToHex(b.wireColorMem[BB]).c_str(), settingsGet("wire.color.bb", "(none)").c_str());
        check(wireRed && memRed, "picking a colour repaints the wire and remembers it", failures);
        check(stored, "the remembered colour is written to config.toml", failures);
        // per view: the PCB choice must not change the breadboard colour
        uint32_t bbMem = b.wireColorMem[BB];
        b.wireColorMem[PCB] = rgba(40, 170, 80);
        b.saveWirePrefs(PCB);
        check(b.wireColorMem[BB] == bbMem, "each view remembers its own colour", failures);
        // a fresh app reads it back (what the next start does)
        App c2;
        c2.loadWirePrefs();
        check(c2.wireColorMem[BB] == red && c2.wireColorMem[PCB] == rgba(40, 170, 80),
              "the remembered colours are read back on the next start", failures);
        // the eyedropper: take a wire's colour without repainting anything
        b.setWireColor(BB, wid, rgba(65, 140, 225));
        check(b.wireColorMem[BB] == rgba(65, 140, 225), "the eyedropper colour becomes the new-wire colour", failures);
    }
    {
        App c;
        c.ui.s = 1.f;
        const int p1 = c.doc.addPart("resistor", {}, {0, 0}).id;
        const int p2 = c.doc.addPart("resistor", {}, {1500, 0}).id;
        Wire w;
        w.id = c.doc.newId();
        w.view = BB;
        w.ends[0] = {p1, 0};
        w.ends[1] = {p2, 0};
        w.pts = {c.doc.part(p1)->pinPos(0, BB), c.doc.part(p2)->pinPos(0, BB)};
        c.doc.wires.push_back(w);
        Connectivity conn;
        conn.build(c.doc);
        uint32_t memBefore = c.wireColorMem[PCB];
        RouteResult r = autoroute(c.doc, conn, true);
        printf("  autoroute: %d routed, %d failed, memory unchanged %d\n", r.routed, r.failed, (int)(c.wireColorMem[PCB] == memBefore));
        check(c.wireColorMem[PCB] == memBefore, "the autorouter never touches the remembered colour", failures);
    }
}

// ---- shape stroke widths, and grabbing existing markup with a drawing tool ----
void shapeToolTests(int& failures) {
    printf("Shape widths and grabbing\n");
    {   // every width step survives the file, and out-of-range values are clamped
        bool roundTrip = true;
        for (int k = 0; k <= 3; k++) {
            Anno a;
            a.id = 7;
            a.kind = AnnoKind::Rect;
            a.widthStep = k;
            Anno back;
            if (!annoFromJson(annoToJson(a), &back) || back.widthStep != k) roundTrip = false;
        }
        Anno a;
        a.id = 7;
        a.kind = AnnoKind::Line;
        Json j = annoToJson(a);
        Anno high, low;
        j.set("widthStep", 9);
        bool clamped = annoFromJson(j, &high) && high.widthStep == 3;
        j.set("widthStep", -4);
        clamped = annoFromJson(j, &low) && low.widthStep == 0 && clamped;
        printf("  widthStep: round trip %s, clamped %s\n", roundTrip ? "ok" : "FAILED", clamped ? "ok" : "FAILED");
        check(roundTrip, "all four stroke-width steps survive save and load", failures);
        check(clamped, "an out-of-range width step is clamped to 0..3", failures);
    }
    {   // the stroke grows with the step, and the thin step stays the old 2 px
        Anno a;
        bool grows = true;
        float prev = 0;
        for (int k = 0; k <= 3; k++) {
            a.widthStep = k;
            float px = annoStrokePx(a, 1.f);
            if (px <= prev) grows = false;
            prev = px;
        }
        a.widthStep = 0;
        bool thin = std::fabs(annoStrokePx(a, 1.f) - 2.f) < 1e-4f && std::fabs(annoStrokePx(a, 2.f) - 4.f) < 1e-4f;
        a.widthStep = 3;
        float heavy = annoStrokePx(a, 1.f);
        printf("  stroke px: thin 2, extra thick %.1f, grows %s\n", (double)heavy, grows ? "yes" : "no");
        check(grows && heavy > 4.f, "a heavier step draws a wider stroke", failures);
        check(thin, "the thin step is the old 2 px stroke, scaled by the interface", failures);
    }
    auto setup = [](App& app) {
        app.ui.s = 1.f;
        app.view = SCH;
        app.cam[SCH].zoom = 1.f;
        app.cam[SCH].pan = Vec2(0, 0);
        app.cam[SCH].valid = true;
        app.rCanvas = Rect(0, 0, 800, 600);  // the canvas covers the whole test window
        app.snapToGrid = false;
    };
    {   // 8 px outside a rectangle's stroke: the press grabs that shape, it draws nothing
        App app;
        setup(app);
        Anno r;
        r.id = app.doc.newId();
        r.view = SCH;
        r.kind = AnnoKind::Rect;
        r.a = {100, 100};
        r.b = {500, 300};
        app.doc.annos.push_back(r);
        app.setTool(TOOL_RECT);
        Vec2 press = app.toScreen(Vec2(300, 92));  // 8 px above the top edge
        app.in.mouse = press;
        app.in.pressPos = press;
        app.in.pressed = true;
        app.in.down = true;
        app.canvasInput();
        app.in.endFrame();
        bool grabbed = app.drag == DRAG_ANNO && app.dragAnno == r.id && app.selAnnos.count(r.id) > 0;
        Vec2 move = app.toScreen(Vec2(400, 92));  // drag the shape 100 units to the right
        app.in.mouse = move;
        app.in.down = true;
        app.canvasInput();
        app.in.endFrame();
        app.in.mouse = move;
        app.in.down = false;
        app.in.released = true;
        app.canvasInput();
        app.in.endFrame();
        float ax = app.doc.annos.empty() ? 0.f : app.doc.annos[0].a.x;
        printf("  press 8 px off the stroke: grabbed %s, moved to x %.0f, %zu shape(s)\n", grabbed ? "yes" : "no", (double)ax,
               app.doc.annos.size());
        check(grabbed, "a press just outside an existing stroke grabs that shape instead of starting a new one", failures);
        check(app.doc.annos.size() == 1 && std::fabs(ax - 200.f) < 0.5f,
              "dragging the grabbed shape moves it and draws no second shape", failures);
        check(app.tool == TOOL_RECT, "the drawing tool stays active after grabbing a shape", failures);
    }
    {   // the same grab works over a part body: a drawing tool only cares about markup
        App app;
        setup(app);
        // The part sits wherever its symbol puts it; a canvas big enough for the whole
        // test window keeps the press inside it.
        app.rCanvas = Rect(-4000, -4000, 12000, 12000);
        const int pid = app.doc.addPart("74hc00", {}, {300, 250}).id;
        const Rect body = app.doc.part(pid)->worldBounds(SCH);
        Anno r;
        r.id = app.doc.newId();
        r.view = SCH;
        r.kind = AnnoKind::Rect;
        r.a = {body.x + body.w * 0.25f, body.y + body.h * 0.5f};
        r.b = {body.r() - body.w * 0.25f, body.b() - body.h * 0.25f};
        app.doc.annos.push_back(r);
        // 8 px off the top edge of the frame, inside the body of the part it surrounds
        Vec2 pressWorld = {r.a.x + (r.b.x - r.a.x) * 0.5f, r.a.y - 8.f};
        Hit plain = app.hitTest(app.toScreen(pressWorld));
        app.setTool(TOOL_RECT);
        Vec2 press = app.toScreen(pressWorld);
        app.in.mouse = press;
        app.in.pressPos = press;
        app.in.pressed = true;
        app.in.down = true;
        app.canvasInput();
        app.in.endFrame();
        bool grabbed = app.drag == DRAG_ANNO && app.dragAnno == r.id && app.doc.annos.size() == 1;
        app.in.mouse = press;
        app.in.down = false;
        app.in.released = true;
        app.canvasInput();
        app.in.endFrame();
        printf("  shape over a part body: plain hit kind %d, grabbed %s, %zu shape(s)\n", (int)plain.kind,
               grabbed ? "yes" : "no", app.doc.annos.size());
        check(plain.kind == Hit::PART || plain.kind == Hit::PIN,
              "a point on a frame around a part belongs to that part for the selection tool", failures);
        check(grabbed && app.doc.annos.size() == 1 && app.tool == TOOL_RECT,
              "a shape tool grabs a frame drawn around a part instead of drawing a new shape", failures);
    }
    {   // a drawing tool draws over a picture: only shape strokes are grabbed, not image bodies
        App app;
        setup(app);
        Anno pic;
        pic.id = app.doc.newId();
        pic.view = SCH;
        pic.kind = AnnoKind::Image;
        pic.a = {100, 100};
        pic.b = {500, 400};
        app.doc.annos.push_back(pic);
        app.setTool(TOOL_RECT);
        Vec2 p0 = app.toScreen(Vec2(200, 200)), p1 = app.toScreen(Vec2(400, 350));
        app.in.mouse = p0; app.in.pressPos = p0; app.in.pressed = true; app.in.down = true;
        app.canvasInput();
        app.in.endFrame();
        bool grabbedPicture = app.drag == DRAG_ANNO;
        app.in.mouse = p1; app.in.down = true;
        app.canvasInput();
        app.in.endFrame();
        app.in.mouse = p1; app.in.down = false; app.in.released = true;
        app.canvasInput();
        app.in.endFrame();
        const Anno& moved = app.doc.annos[0];
        check(!grabbedPicture && app.doc.annos.size() == 2 && app.doc.annos[1].kind == AnnoKind::Rect &&
                  std::fabs(moved.a.x - 100.f) < 0.5f,
              "a rectangle drawn over a picture is a new shape, the picture stays put", failures);
    }
    {   // a press on empty canvas still draws exactly one new shape
        App app;
        setup(app);
        app.setTool(TOOL_RECT);
        Vec2 press = app.toScreen(Vec2(600, 400));
        app.in.mouse = press;
        app.in.pressPos = press;
        app.in.pressed = true;
        app.in.down = true;
        app.canvasInput();
        app.in.endFrame();
        bool drawing = app.annoDrawing && app.doc.annos.empty();
        Vec2 move = app.toScreen(Vec2(750, 520));
        app.in.mouse = move;
        app.in.down = true;
        app.canvasInput();
        app.in.endFrame();
        app.in.mouse = move;
        app.in.down = false;
        app.in.released = true;
        app.canvasInput();
        app.in.endFrame();
        printf("  press on empty canvas: started drawing %s, %zu shape(s) after the release\n", drawing ? "yes" : "no",
               app.doc.annos.size());
        check(drawing && app.doc.annos.size() == 1 && !app.annoDrawing,
              "a press on empty canvas still draws exactly one shape", failures);
    }
    {   // the wider stroke tolerance never takes a click away from a wire
        App app;
        setup(app);
        Anno r;
        r.id = app.doc.newId();
        r.view = SCH;
        r.kind = AnnoKind::Rect;
        r.a = {100, 100};
        r.b = {500, 300};
        app.doc.annos.push_back(r);
        Wire wr;
        wr.id = app.doc.newId();
        wr.view = SCH;
        wr.pts = {Vec2(120, 100), Vec2(480, 100)};  // lies along the rectangle's top edge
        app.doc.wires.push_back(wr);
        Hit h = app.hitTest(app.toScreen(Vec2(300, 104)));
        printf("  wire over a shape: hit kind %d (wire segment is %d)\n", (int)h.kind, (int)Hit::WIRE_SEG);
        check(h.kind == Hit::WIRE_SEG, "a wire on top of a shape keeps the click", failures);
    }
}

// ---- rebindable keys and mouse roles ----
void keyTests(int& failures) {
    printf("Keyboard and mouse mapping\n");
    Bindings d = defaultBindings();
    check(d.chord[(int)Action::Undo] == KeyChord{SDLK_Z, true, false, false} && d.chord[(int)Action::Delete].key == SDLK_DELETE &&
              !d.chord[(int)Action::AddBend].bound() && d.mouse[SLOT_MIDDLE] == MouseRole::Auto,
          "the factory bindings are the documented ones", failures);

    bool roundTrip = true;
    for (int i = 0; i < kActionCount; i++) {
        KeyChord back;
        if (!chordFromText(chordToToken(d.chord[i]), &back) || back != d.chord[i]) roundTrip = false;
        KeyChord shown;  // and the text the dialog shows parses back to the same chord
        if (d.chord[i].bound() && (!chordFromText(chordToText(d.chord[i]), &shown) || shown != d.chord[i])) roundTrip = false;
    }
    printf("  undo %s, redo %s, unbound %s\n", chordToToken(d.chord[(int)Action::Undo]).c_str(),
           chordToText(d.chord[(int)Action::Redo]).c_str(), chordToText(KeyChord()).c_str());
    check(roundTrip, "every binding survives a round trip through its config.toml text", failures);
    check(chordToText(d.chord[(int)Action::Redo]) == "Ctrl+Shift+Z" && chordToText(KeyChord()) == tr("keys.unbound"),
          "a binding is shown as Ctrl+Shift+Z, and an empty one as \"not bound\"", failures);

    KeyChord c;
    check(chordFromText("alt+left", &c) && c.key == SDLK_LEFT && c.alt && !c.ctrl && !c.shift,
          "a chord is parsed from alt+left", failures);
    check(chordFromText("none", &c) && !c.bound(), "\"none\" unbinds", failures);
    check(!chordFromText("ctrl+nosuchkey", &c) && !chordFromText("hyper+z", &c), "nonsense is rejected, not guessed", failures);

    check(!chordAllowed(Action::Delete, KeyChord{SDLK_X, false, false, false}) &&
              chordAllowed(Action::Delete, KeyChord{SDLK_X, true, false, false}) &&
              chordAllowed(Action::Delete, KeyChord{SDLK_DELETE, false, false, false}) &&
              chordAllowed(Action::Copy, KeyChord{SDLK_X, false, false, false}),
          "a bare key can't be a delete, a modifier or Delete itself can", failures);

    PrefsGuard guard;  // the binding tests write config.toml; put the user's file back
    {
        App k;
        k.binds = defaultBindings();
        // Alt+Left for AddBend: live, and the middle button goes back to panning
        KeyChord altLeft{SDLK_LEFT, false, false, true};
        bool middleWasAuto = k.mouseRoleFor(SLOT_MIDDLE) == MouseRole::Auto;
        k.applyBinding(Action::AddBend, altLeft);
        Input in;
        in.mods = SDL_KMOD_ALT;
        in.keys = {SDLK_LEFT};
        bool fires = actionFor(k.binds, in) == (int)Action::AddBend;
        bool middlePans = k.mouseRoleFor(SLOT_MIDDLE) == MouseRole::Pan;
        printf("  AddBend on %s: fires %d, middle pans %d\n", chordToText(altLeft).c_str(), (int)fires, (int)middlePans);
        check(middleWasAuto && fires && middlePans,
              "binding AddBend to a key works at once and hands the middle button back to pan", failures);

        // a conflict replaces the loser, which is left unbound
        KeyChord copyChord = k.binds.chord[(int)Action::Copy];
        int loser = findChord(k.binds, copyChord, (int)Action::Paste);
        k.applyBinding(Action::Paste, copyChord, loser);
        bool moved = k.binds.chord[(int)Action::Paste] == copyChord && loser == (int)Action::Copy &&
                     !k.binds.chord[(int)Action::Copy].bound();
        printf("  Copy's chord moved to Paste: loser %d (%s), now %s\n", loser, actionId((Action)loser),
               chordToText(k.binds.chord[(int)Action::Copy]).c_str());
        check(moved, "replacing a shortcut unbinds the action that had it", failures);

        // the sheet's "restore all" is exactly defaultBindings()
        k.binds = defaultBindings();
        bool factory = true;
        Bindings ref = defaultBindings();
        for (int i = 0; i < kActionCount; i++)
            if (k.binds.chord[i] != ref.chord[i]) factory = false;
        for (int i = 0; i < kMouseSlots; i++)
            if (k.binds.mouse[i] != ref.mouse[i]) factory = false;
        check(factory, "restoring the defaults equals defaultBindings()", failures);

        // persistence: what the sheet saves is what the next start loads
        k.binds.chord[(int)Action::AddBend] = altLeft;
        k.binds.mouse[SLOT_MIDDLE] = MouseRole::Pan;
        k.saveBindings();
        App fresh;
        fresh.loadBindings();
        std::string stored = settingsGet("shortcut.add_bend", "(missing)");
        printf("  stored shortcut.add_bend = %s, mouse.middle = %s\n", stored.c_str(), settingsGet("mouse.middle", "?").c_str());
        check(fresh.binds.chord[(int)Action::AddBend] == altLeft && fresh.binds.mouse[SLOT_MIDDLE] == MouseRole::Pan,
              "custom bindings are saved to config.toml and read back on the next start", failures);
        check(fresh.binds.chord[(int)Action::Undo] == ref.chord[(int)Action::Undo],
              "bindings that were never touched keep their default", failures);
    }
    {   // the shortcuts dialog is generated from the table, not from hard-coded text
        App k;
        k.binds = defaultBindings();
        std::vector<std::string> lines = k.shortcutLines();
        bool undoLine = !lines.empty() && lines[0].find("Ctrl+Z") != std::string::npos;
        bool bendLine = false;
        for (auto& l : lines)
            if (l.find(tr("shortcut.14")) != std::string::npos) bendLine = true;
        printf("  shortcuts dialog: %zu lines, undo line %s\n", lines.size(), lines.empty() ? "-" : lines[0].c_str());
        check(lines.size() == (size_t)kActionCount + 7 && undoLine && bendLine,
              "the shortcuts dialog lists every action from the table, including the bend-point tip", failures);
        k.binds.chord[(int)Action::AddBend] = KeyChord{SDLK_LEFT, false, false, true};
        std::vector<std::string> after = k.shortcutLines();
        bool shown = false;
        for (auto& l : after)
            if (l.find("Alt+Left") != std::string::npos || l.find("Alt+\u2190") != std::string::npos) shown = true;
        check(shown, "a rebound action shows its new keys in the dialog", failures);
    }
    {
        // a damaged value in the file falls back to the default instead of breaking the app
        settingsSet("shortcut.undo", "not-a-key");
        App broken;
        broken.loadBindings();
        check(broken.binds.chord[(int)Action::Undo] == defaultBindings().chord[(int)Action::Undo],
              "a nonsense binding in config.toml falls back to the default", failures);
    }
}

int exportExamples(const std::string& dir) {
    for (size_t i = 0; i < examples().size(); i++) {
        Document d;
        examples()[i].build(d);
        d.syncWireEnds();
        std::string name = examples()[i].title;
        for (auto& c : name) if (c == ' ' || c == '+' || c == '/') c = '_';
        std::string path = dir + "/" + name + kFileExt;
        if (!writeTextFile(path, d.serialize(true))) { printf("could not write %s\n", path.c_str()); return 1; }
        printf("wrote %s\n", path.c_str());
    }
    return 0;
}

// Writes the built-in language packs as editable JSON. Copy the ones you change
// into an "i18n" folder next to the executable (or into the preferences folder)
// to override the compiled-in text without rebuilding.
int exportI18n(const std::string& dir) {
    i18n::initEmbedded();
    // The part library and the examples build their keys from data, so a static
    // scan cannot find them: enumerate the live library instead.
    std::map<std::string, std::string> harvested;
    collectPartI18nKeys(harvested);
    for (auto& e : examples()) harvested["ex." + slugify(e.title) + ".title"] = e.title;

    SDL_CreateDirectory(dir.c_str());
    for (auto& code : i18n::packCodes()) {
        std::string text = i18n::dumpPack(code);
        if (code == "en") {  // harvested text is English by definition
            std::string err;
            Json j = Json::parse(text, &err);
            if (!j.isObject()) { printf("could not read the English pack: %s\n", err.c_str()); return 1; }
            for (auto& kv : harvested) j.set(kv.first, Json(kv.second));
            text = j.dump(2);
        }
        std::string path = dir + "/" + code + ".json";
        if (!writeTextFile(path, text)) { printf("could not write %s\n", path.c_str()); return 1; }
        printf("wrote %s\n", path.c_str());
    }
    return 0;
}

// Writes the logo as SVG plus PNGs at the sizes a macOS .iconset needs.
int exportLogo(const std::string& dir) {
    if (!writeTextFile(dir + "/logo.svg", logoSvg())) { printf("could not write %s/logo.svg\n", dir.c_str()); return 1; }
    for (int size : {16, 32, 64, 128, 256, 512, 1024}) {
        SDL_Surface* s = makeLogoSurface(size);
        std::string path = dir + "/icon_" + std::to_string(size) + ".png";
        bool ok = s && SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
        if (!ok) { printf("could not write %s\n", path.c_str()); return 1; }
    }
    printf("wrote logo.svg and icon_*.png to %s\n", dir.c_str());
    return 0;
}

struct Screenshot {
    std::string out;
    int frames = 12;
    int example = -1;
    int view = -1;
    bool sim = false, route = false, tidy = false;
    int w = 0, h = 0;
    std::string drawer;
    int select = -1;
    int selectAnno = -1;  // index into the annotations of the loaded design
    std::string menu;
    std::string sheet;  // history | commit | account
    bool openCode = false;  // open the program panel of the first board
    bool welcome = false;   // --welcome: capture the first screen
    std::string ai;         // --ai "prompt": run the AI tab with this message, capture when it finishes
    bool aiStarted = false;
    // scripted pointer events: frame -> (type, x, y) in window points
    struct Ev { int frame; char type; float x, y; };
    std::vector<Ev> events;
};

// --record-ui out.mp4: records the app window itself (with --ai: the agent building the circuit,
// time-lapsed by --speed, then the finished circuit simulating at normal speed).
struct UiRecorder {
    std::string path;
    double speed = 4;          // time-lapse while the agent works
    double curSpeed = 1;
    int fps = 30, outW = 0, outH = 0;
    std::unique_ptr<VideoWriter> vw;
    std::vector<uint8_t> buf;
    std::string err;
    double nextT = 0;
    long frames = 0;
    void frame(SDL_Surface* s) {
        double now = (double)SDL_GetTicksNS() / 1e9;
        if (vw && now < nextT) return;
        SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        if (!c) return;
        if (!vw) {
            outW = std::min(1920, c->w) & ~1;
            outH = (int)std::lround((double)outW * c->h / c->w) & ~1;
            vw = openMp4(path, outW, outH, fps, &err);
            if (!vw) { SDL_DestroySurface(c); return; }
            nextT = now;
        }
        // box-filtered downscale to the output size
        buf.assign((size_t)outW * outH * 4, 0);
        double rx = (double)c->w / outW, ry = (double)c->h / outH;
        const uint8_t* src = (const uint8_t*)c->pixels;
        for (int y = 0; y < outH; y++) {
            int y0 = (int)(y * ry), y1 = std::max(y0 + 1, std::min(c->h, (int)((y + 1) * ry)));
            for (int x = 0; x < outW; x++) {
                int x0 = (int)(x * rx), x1 = std::max(x0 + 1, std::min(c->w, (int)((x + 1) * rx)));
                unsigned acc[4] = {0, 0, 0, 0}, n = 0;
                for (int yy = y0; yy < y1; yy++)
                    for (int xx = x0; xx < x1; xx++, n++) {
                        const uint8_t* p = src + (size_t)yy * c->pitch + (size_t)xx * 4;
                        for (int k = 0; k < 4; k++) acc[k] += p[k];
                    }
                uint8_t* o = &buf[((size_t)y * outW + x) * 4];
                for (int k = 0; k < 4; k++) o[k] = (uint8_t)(acc[k] / n);
                o[3] = 255;
            }
        }
        SDL_DestroySurface(c);
        // keep the video's clock: repeat the frame for slots we fell behind on (at most a few)
        int copies = 0;
        do {
            vw->addFrame(buf.data(), outW * 4);
            frames++;
            nextT += curSpeed / fps;
        } while (nextT <= now && ++copies < 4);
        if (nextT < now) nextT = now;
    }
};
UiRecorder g_ui;
std::string g_agentCmd;
std::string g_animate, g_zoomTo;
// --animate "max6675.temperature=24:85": sets that property on every part of the type, eased over `seconds`
std::function<void(double)> makeAnimation(App* app, const std::string& spec, double seconds) {
    size_t dot = spec.find('.'), eq = spec.find('='), col = spec.find(':');
    if (dot == std::string::npos || eq == std::string::npos || col == std::string::npos) return nullptr;
    std::string type = spec.substr(0, dot), prop = spec.substr(dot + 1, eq - dot - 1);
    double a = std::atof(spec.substr(eq + 1, col - eq - 1).c_str()), b = std::atof(spec.substr(col + 1).c_str());
    return [=](double t) {
        double u = std::clamp(t / std::max(0.1, seconds * 0.8), 0.0, 1.0);
        u = u * (2 - u);  // ease out
        char v[32];
        snprintf(v, sizeof v, "%.2f", a + (b - a) * u);
        for (auto& p : app->doc.parts) if (p.type == type) p.props[prop] = v;
    };
}
struct UiScript {
    int phase = 0;
    double t0 = 0;
    size_t typed = 0;
} g_uis;

// Ease the canvas camera toward framing `target` (world units), or the whole design when target is empty.
void glideCamera(App* app, Rect target, float rate) {
    Camera& c = app->cam[app->view];
    Camera saved = c;
    if (target.w <= 0) {
        app->fitView(app->view, app->rCanvas);
    } else {
        Rect vp = app->rCanvas;
        c.zoom = std::min(vp.w / target.w, vp.h / target.h);
        c.pan = vp.center() - target.center() * c.zoom;
    }
    Camera goal = c;
    c = saved;
    if (!saved.valid) { c = goal; return; }
    // interpolate in log-zoom so the motion feels even
    Vec2 ctrNow = (app->rCanvas.center() - c.pan) / c.zoom, ctrGoal = (app->rCanvas.center() - goal.pan) / goal.zoom;
    float z = std::exp(std::log(c.zoom) + (std::log(goal.zoom) - std::log(c.zoom)) * rate);
    Vec2 ctr = ctrNow + (ctrGoal - ctrNow) * rate;
    c.zoom = z;
    c.pan = app->rCanvas.center() - ctr * z;
}

App* g_app = nullptr;
bool g_embed = false;
// --record OUT.mp4|OUT.gif [--preset 0-3] [--seconds N] [--record-view 0-3]: export a video and quit
std::string g_record;
int g_recordPreset = 0, g_recordSeconds = 3, g_recordView = -1;
bool g_recordStarted = false;
std::string g_editorUrl;
int g_loadExample = -1;
std::string g_forceLang;  // --lang: overrides the stored/system language for this run
Screenshot g_shot;
int g_frame = 0;

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    std::vector<char*> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--share-fragment") {  // print the "#d=…" link fragment for a .dez (or an example index)
            std::string arg = next();
            App tmp;
            if (!arg.empty() && std::isdigit((unsigned char)arg[0])) examples()[(size_t)std::atoi(arg.c_str()) % examples().size()].build(tmp.doc);
            else if (!tmp.doc.deserialize(readTextFile(arg), nullptr)) return SDL_APP_FAILURE;
            if (i + 1 < argc && std::string(argv[i + 1]) == "--json") printf("%s\n", tmp.doc.compactJson().c_str());
            else printf("%s\n", tmp.shareFragment().c_str());
            return SDL_APP_SUCCESS;
        }
        if (a == "--export-logo") return exportLogo(next()) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--github-selftest") return githubSelftest() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--mcp") {
            int port = 0;
            std::string token;
            for (int k = i + 1; k + 1 < argc; k++) {
                if (std::string(argv[k]) == "--connect") port = std::atoi(argv[k + 1]);
                if (std::string(argv[k]) == "--token") token = argv[k + 1];
            }
            return runMcpServer(port, token) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
        if (a == "--selftest") return selftest() == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--spice-run") return spiceCli(argc, argv, i) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
#if DE_NGSPICE_DESKTOP
        if (a == "--spice-xcheck") {
            if (i + 1 >= argc) return SDL_APP_FAILURE;
            return spiceCrossCheck(argv[++i]) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
#endif
        if (a == "--export-examples") return exportExamples(next()) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--export-i18n") return exportI18n(next()) == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        if (a == "--screenshot") g_shot.out = next();
        else if (a == "--example") g_shot.example = std::atoi(next().c_str());
        else if (a == "--view") { std::string v = next(); g_shot.view = (v == "sch" || v == "schematic") ? SCH : v == "pcb" ? PCB : BB; }
        else if (a == "--sim") g_shot.sim = true;
        else if (a == "--autoroute") g_shot.route = true;
        else if (a == "--tidy") g_shot.tidy = true;
        else if (a == "--frames") g_shot.frames = std::atoi(next().c_str());
        else if (a == "--drawer") g_shot.drawer = next();
        else if (a == "--select") g_shot.select = std::atoi(next().c_str());
        else if (a == "--select-anno") g_shot.selectAnno = std::atoi(next().c_str());
        else if (a == "--menu") g_shot.menu = next();
        else if (a == "--sheet") g_shot.sheet = next();
        else if (a == "--open-code") g_shot.openCode = true;
        else if (a == "--ai") g_shot.ai = next();
        else if (a == "--events") {
            // "frame:d:x:y,frame:m:x:y,frame:u:x:y,frame:k:keycode:0"
            std::string spec = next() + ",";
            std::string cur;
            for (char ch : spec) {
                if (ch != ',') { cur += ch; continue; }
                Screenshot::Ev e{};
                char t = 0;
                if (sscanf(cur.c_str(), "%d:%c:%f:%f", &e.frame, &t, &e.x, &e.y) == 4) { e.type = t; g_shot.events.push_back(e); }
                cur.clear();
            }
        }
        else if (a == "--embed") g_embed = true;
        else if (a == "--welcome") g_shot.welcome = true;
        else if (a == "--record") g_record = next();
        else if (a == "--record-ui") g_ui.path = next();
        else if (a == "--animate") g_animate = next();
        else if (a == "--zoom-to") g_zoomTo = next();  // "max6675,ssd1306": move in on these parts halfway through  // "type.prop=from:to" eased over the recording
        else if (a == "--speed") g_ui.speed = std::atof(next().c_str());
        else if (a == "--agent-cmd") g_agentCmd = next();  // testing: a custom command instead of Claude Code
        else if (a == "--preset") g_recordPreset = std::atoi(next().c_str());
        else if (a == "--seconds") g_recordSeconds = std::atoi(next().c_str());
        else if (a == "--record-view") g_recordView = std::atoi(next().c_str());
        else if (a == "--editor-url") g_editorUrl = next();
        else if (a == "--load-example") g_loadExample = std::atoi(next().c_str());
        else if (a == "--size") { std::string s = next(); sscanf(s.c_str(), "%dx%d", &g_shot.w, &g_shot.h); }
        else if (a == "--lang") g_forceLang = next();  // interface language for this run ("en", "zh-CN", …)
        else if (a.rfind("-psn", 0) == 0) continue;  // macOS Finder launch argument
        else rest.push_back(argv[i]);
    }
    g_app = new App();
    g_app->startupLanguage = g_forceLang;
    *appstate = g_app;
    g_app->embedMode = g_app->readOnly = g_embed;
    g_app->editorUrl = g_editorUrl;
    g_app->scratchSession = !g_shot.out.empty() || !g_record.empty();
    if (!g_app->init((int)rest.size(), rest.data())) return SDL_APP_FAILURE;
    if (g_shot.welcome) g_app->startScreen = true;  // --welcome: photograph the first screen
    if (g_loadExample >= 0 && g_loadExample < (int)examples().size()) {
        g_app->doc.clear();
        examples()[(size_t)g_loadExample].build(g_app->doc);
        g_app->doc.syncWireEnds();
        g_app->doc.touch();
        g_app->savedRevision = g_app->doc.revision;
    }
#ifdef __EMSCRIPTEN__
    // a design opened from a link isn't a file on this device: Save downloads it under its title
    g_app->filePath.clear();
    g_app->savedRevision = g_app->doc.revision;
#endif
    // start view / simulation (web links: #view=pcb&sim=1)
    if (g_shot.out.empty()) {
        if (g_shot.view >= 0) g_app->view = g_shot.view;
        if (g_shot.sim) g_app->simOn = true;
    }
    if (!g_shot.out.empty()) {
        if (g_shot.w > 0) SDL_SetWindowSize(g_app->win, g_shot.w, g_shot.h);
        if (g_shot.example >= 0) {
            g_app->doc.clear();
            examples()[(size_t)g_shot.example].build(g_app->doc);
            g_app->doc.syncWireEnds();
            g_app->doc.touch();
            g_app->savedRevision = g_app->doc.revision;
            for (auto& c : g_app->cam) c.valid = false;
        }
        if (g_shot.view >= 0) g_app->view = g_shot.view;
        if (g_shot.route) { g_app->ensureConn(); autoroute(g_app->doc, g_app->conn, true); g_app->doc.touch(); }
        if (g_shot.tidy) { for (int v = 0; v < NUM_VIEWS; v++) tidyWires(g_app->doc, v); g_app->doc.syncWireEnds(); g_app->doc.touch(); }
        if (g_shot.sim) g_app->simOn = true;
        if (g_shot.select >= 0 && g_shot.select < (int)g_app->doc.parts.size()) g_app->selParts = {g_app->doc.parts[(size_t)g_shot.select].id};
        if (g_shot.selectAnno >= 0 && g_shot.selectAnno < (int)g_app->doc.annos.size())
            g_app->selAnnos = {g_app->doc.annos[(size_t)g_shot.selectAnno].id};
        // screenshots are for sharing: no local file path or "Opened…" toast in them
        g_app->filePath.clear();
        g_app->ui.toast("", 0);
        if (g_shot.openCode)
            for (auto& p : g_app->doc.parts)
                if (p.def->sim == "board" || p.def->sim == "mcu") { g_app->openCode(p.id); g_app->ui.toast("", 0); break; }
        if (g_shot.sheet == "history") g_app->openHistory();
        else if (g_shot.sheet == "commit") g_app->openCommitSheet();
        else if (g_shot.sheet == "account") g_app->openGitHubAccount();
        else if (g_shot.sheet == "embed") g_app->openEmbedSheet();
        else if (g_shot.sheet == "video") g_app->openVideoSheet();
        else if (g_shot.sheet == "colorcode") g_app->openColorCodeTool();
        else if (g_shot.sheet == "upload")
            for (auto& p : g_app->doc.parts)
                if (p.def->sim == "board") { g_app->openUploadSheet(p.id); break; }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return ((App*)appstate)->event(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App* app = (App*)appstate;
    if (!g_record.empty()) {
        if (!g_recordStarted) {
            g_recordStarted = true;
            app->videoPreset = g_recordPreset;
            app->videoSeconds = g_recordSeconds;
            app->videoView = g_recordView >= 0 ? g_recordView : app->view;
            app->videoGif = g_record.size() > 4 && g_record.substr(g_record.size() - 4) == ".gif";
            app->videoTitle = app->doc.title;
            if (!g_animate.empty()) app->videoAnimate = makeAnimation(app, g_animate, g_recordSeconds);
            if (!g_zoomTo.empty()) {
                std::string types = "," + g_zoomTo + ",";
                double secs = g_recordSeconds;
                app->videoZoom = [app, types, secs](double t, Rect& focus) -> float {
                    bool any = false;
                    for (auto& p : app->doc.parts)
                        if (types.find("," + p.type + ",") != std::string::npos) {
                            Rect r = p.worldBounds(app->videoView < 3 ? app->videoView : BB);
                            focus = any ? focus.united(r) : r;
                            any = true;
                        }
                    if (!any) return 0;
                    focus = Rect(focus.x - 150, focus.y - 150, focus.w + 300, focus.h + 300);
                    double u = std::clamp((t - secs * 0.3) / 1.6, 0.0, 1.0);  // glide in from 30% of the clip
                    return (float)(u * u * (3 - 2 * u));
                };
            }
            app->startVideo(g_record);
            if (!app->videoJob) { printf("could not start recording\n"); return SDL_APP_FAILURE; }
        } else if (!app->videoJob) {
            if (!app->lastVideoError.empty()) { printf("recording failed: %s\n", app->lastVideoError.c_str()); return SDL_APP_FAILURE; }
            printf("recorded %s\n", g_record.c_str());
            return SDL_APP_SUCCESS;
        }
    }
    if (!g_shot.out.empty()) {
        g_frame++;
        if (g_frame == 3) {
            if (g_shot.drawer == "parts") app->drawerParts = true;
            if (g_shot.drawer == "inspector") app->drawerInspector = true;
            if (g_shot.drawer == "layers") app->drawerLayers = true;
            if (g_shot.drawer == "layers-tab") app->rightTab = 1;
            if (g_shot.drawer == "ai-tab") app->rightTab = 2;
            if (!g_shot.menu.empty()) {
                std::vector<MenuItem> m = g_shot.menu == "file" ? app->fileMenu() : g_shot.menu == "view" ? app->viewMenu() : app->allMenus();
                app->ui.openMenu(Rect(60 * app->ui.s, 8 * app->ui.s, 40 * app->ui.s, 36 * app->ui.s), m);
            }
        }
        for (auto& ev : g_shot.events) {
            if (ev.frame != g_frame) continue;
            SDL_Event e{};
            SDL_WindowID wid = SDL_GetWindowID(app->win);
            if (ev.type == 'd' || ev.type == 'u') {
                e.type = ev.type == 'd' ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                e.button.windowID = wid; e.button.button = SDL_BUTTON_LEFT; e.button.down = ev.type == 'd';
                e.button.clicks = 1; e.button.x = ev.x; e.button.y = ev.y;
            } else if (ev.type == 'm') {
                e.type = SDL_EVENT_MOUSE_MOTION;
                e.motion.windowID = wid; e.motion.x = ev.x; e.motion.y = ev.y;
                e.motion.state = SDL_BUTTON_LMASK;
            } else if (ev.type == 'k') {
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.windowID = wid; e.key.key = (SDL_Keycode)ev.x; e.key.mod = (SDL_Keymod)ev.y; e.key.down = true;
            } else if (ev.type == 'w') {
                // Wheel notches in y, at wherever the pointer last was.
                e.type = SDL_EVENT_MOUSE_WHEEL;
                e.wheel.windowID = wid; e.wheel.x = 0; e.wheel.y = ev.y != 0 ? ev.y : -1;
                e.wheel.mouse_x = app->in.mouse.x; e.wheel.mouse_y = app->in.mouse.y;
            }
            app->event(e);
        }
        bool waitAI = false;
        if (!g_ui.path.empty()) {
            // scripted recording: hold, type the prompt, time-lapse the agent, then run the result
            double now = (double)SDL_GetTicksNS() / 1e9;
            auto& S = g_uis;
            if (!app->frameTap) {
                app->frameTap = [](SDL_Surface* s) { g_ui.frame(s); };
                app->rightTab = 2;
                app->detectAgents();
                if (!g_agentCmd.empty()) { app->agentKind = "custom"; app->agentCustomCommand = g_agentCmd; }
                S.t0 = now;
            }
            bool running = false, detected = false;
            std::map<std::string, std::string> paths;
            app->agentMessages(running, detected, paths);
            double t = now - S.t0;
            static int lastPhase = -1;
            static bool lastRunning = false;
            if (S.phase != lastPhase || running != lastRunning) {
                printf("[rec %.1fs] phase %d running %d\n", (double)SDL_GetTicks() / 1000.0, S.phase, (int)running);
                fflush(stdout);
                lastPhase = S.phase;
                lastRunning = running;
            }
            if (S.phase == 0 && t > 1.5 && detected) { S.phase = 1; S.t0 = now; }
            else if (S.phase == 1) {  // type the prompt
                size_t want = std::min(g_shot.ai.size(), (size_t)(t / 0.03));
                while (S.typed < want) {  // whole UTF-8 characters
                    S.typed++;
                    while (S.typed < g_shot.ai.size() && ((unsigned char)g_shot.ai[S.typed] & 0xC0) == 0x80) S.typed++;
                }
                app->agentInput = g_shot.ai.substr(0, S.typed);
                if (S.typed >= g_shot.ai.size() && t > g_shot.ai.size() * 0.03 + 0.8) {
                    app->agentInput.clear();
                    app->startAgent(g_shot.ai);
                    g_ui.curSpeed = g_ui.speed;
                    S.phase = 2;
                    S.t0 = now;
                }
            } else if (S.phase == 2 && !running && t > 2) {
                g_ui.curSpeed = 1;
                app->selParts.clear();  // no selection outline in the finished shot
                app->selWires.clear();
                S.phase = 3;
                S.t0 = now;
            } else if (S.phase == 3 && t > 2.5) {  // run it
                app->simOn = true;
                S.phase = 4;
                S.t0 = now;
            } else if (S.phase == 4) {
                // warm the thermocouple up so the graph moves
                for (auto& p : app->doc.parts)
                    if (p.type == "max6675") {
                        char b[16];
                        snprintf(b, sizeof b, "%.2f", 24 + 60 * std::min(1.0, t / 13) * (2 - std::min(1.0, t / 13)));
                        p.props["temperature"] = b;
                    }
                // then move in on the modules so the OLED reads on a phone
                if (t > 3) {
                    Rect focus;
                    bool any = false;
                    for (auto& p : app->doc.parts)
                        if (p.type == "max6675" || p.type == "ssd1306" || p.type == "sh1106") {
                            Rect r = p.worldBounds(app->view);
                            focus = any ? focus.united(r) : r;
                            any = true;
                        }
                    if (any) glideCamera(app, Rect(focus.x - 250, focus.y - 250, focus.w + 500, focus.h + 500), 0.05f);
                }
                if (t > 16) S.phase = 5;
            }
            if (S.phase == 2) glideCamera(app, Rect(), 0.08f);  // keep the growing design in frame
            waitAI = S.phase < 5 && g_frame < 60 * 600;
            if (!waitAI && g_ui.vw) {
                app->frameTap = nullptr;
                bool ok = g_ui.vw->finish();
                printf("%s %s (%ld frames, %dx%d)\n", ok ? "recorded" : "FAILED", g_ui.path.c_str(), g_ui.frames, g_ui.outW, g_ui.outH);
                bool r2 = false, d2 = false;
                for (auto& m : app->agentMessages(r2, d2, paths)) if (m.role != App::ChatMsg::R_USER) printf("  [%d] %s\n", (int)m.role, m.text.substr(0, 200).c_str());
                g_ui.vw.reset();
                std::string dez = g_ui.path.substr(0, g_ui.path.find_last_of('.')) + ".dez";
                writeTextFile(dez, app->doc.toJson(false).dump(1));
                printf("design saved to %s\n", dez.c_str());
            }
        } else if (!g_shot.ai.empty()) {
            app->rightTab = 2;
            bool running = false, detected = false;
            std::map<std::string, std::string> paths;
            auto msgs = app->agentMessages(running, detected, paths);
            if (!g_shot.aiStarted) {
                if (!app->agent) app->detectAgents();
                if (detected) {
                    g_shot.aiStarted = true;
                    app->startAgent(g_shot.ai);
                }
                waitAI = true;
            } else {
                waitAI = running;
                if (!running) for (auto& m : msgs) if (m.role != App::ChatMsg::R_USER) printf("  [%d] %s\n", (int)m.role, m.text.substr(0, 160).c_str());
            }
            if (g_frame > 60 * 240) waitAI = false;  // give up after ~4 minutes
        }
        if (g_frame >= g_shot.frames && !waitAI) {
            // Render one more frame and capture it before presenting.
            app->capturePath = g_shot.out;
            app->iterate();
            bool ok = app->captureOk;
            printf("%s %s\n", ok ? "saved" : "FAILED", g_shot.out.c_str());
            app->ensureConn();
            RoutingStatus st;
            app->conn.ratsnest(app->doc, app->view, &st);
            printf("doc: %zu parts, %zu wires, %zu selected, undo=%zu, view nets %d/%d routed\n", app->doc.parts.size(), app->doc.wires.size(),
                   app->selParts.size() + app->selWires.size(), app->undoStack.size(), st.routedNets, st.nets);
            for (auto& w : app->doc.wires) printf("  wire %d view %d ends %d:%d -> %d:%d\n", w.id, w.view, w.ends[0].part, w.ends[0].pin, w.ends[1].part, w.ends[1].pin);
            app->savedRevision = app->doc.revision;  // don't prompt
            return SDL_APP_SUCCESS;
        }
    }
    return app->iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    App* app = (App*)appstate;
    if (app) {
        if (g_shot.out.empty()) app->shutdown();
        delete app;
    }
    SDL_Quit();
}
