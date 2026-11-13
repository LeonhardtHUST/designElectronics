#include "ngspice_backend.h"
#include <cstdio>
#include <cmath>
#include <sstream>
#include <cctype>

static std::string sourceName(const SpiceNetlist& net) {
    std::stringstream ss(net.text); std::string line;
    while(std::getline(ss,line)) {
        if(line.size()>1 && line[0]=='V' && std::isspace((unsigned char)line[1])==0) {
            size_t end=line.find_first_of(" \t\r");
            if(end>1) return line.substr(1,end-1);
        }
    }
    return {};
}

static bool finiteResult(const AnalysisResult& r) {
    if(!r.ok || r.scale.size()<2 || r.data.empty()) return false;
    for(double x:r.scale) if(!std::isfinite(x)) return false;
    for(const auto& col:r.data) for(double x:col) if(!std::isfinite(x)) return false;
    return true;
}

void ngspiceBackendTests(int& failures) {
    std::string why;
    if(!ngspiceAvailable(&why)) { std::printf("SPICE: SKIP (%s)\n", why.c_str()); return; }
    Document d;
    int bat=d.addPart("battery",{{"voltage","5V"}},{0,0}).id;
    int r=d.addPart("resistor",{{"resistance","1k"}},{1000,0}).id;
    int c=d.addPart("capacitor_ceramic",{{"capacitance","1uF"}},{2000,0}).id;
    int l=d.addPart("inductor",{{"inductance","1mH"}},{3000,0}).id;
    auto pin=[&](int id,const char* name){const PartInst* p=d.part(id);for(size_t i=0;i<p->def->pins.size();++i)if(p->def->pins[i].name==name||p->def->pins[i].id==name)return (int)i;return -1;};
    auto wire=[&](int a,const char* pa,int b,const char* pb){Wire w;w.id=d.newId();w.view=SCH;w.ends[0]={a,pin(a,pa)};w.ends[1]={b,pin(b,pb)};w.pts={d.part(a)->pinPos(w.ends[0].pin,SCH),d.part(b)->pinPos(w.ends[1].pin,SCH)};d.wires.push_back(w);};
    wire(bat,"plus",r,"pin0"); wire(r,"pin1",c,"pin0"); wire(c,"pin1",bat,"minus"); wire(bat,"plus",l,"pin0"); wire(l,"pin1",bat,"minus");
    Connectivity conn; conn.build(d);
    SpiceOptions tr; tr.analysis=SpiceOptions::TRAN; tr.tstep=1e-5; tr.tstop=2e-3;
    SpiceOptions rl=tr; rl.tstop=1e-3;
    SpiceOptions dc; dc.analysis=SpiceOptions::DC; dc.dcStart=0; dc.dcStop=5; dc.dcStep=1;
    SpiceOptions ac; ac.analysis=SpiceOptions::AC; ac.pointsPerDecade=10; ac.fstart=10; ac.fstop=1e5;
    SpiceNetlist probe=buildSpice(d,conn,dc); std::string src=sourceName(probe);
    if(src.empty()) { std::printf("SPICE: FAIL could not identify generated voltage source\n"); failures++; }
    dc.dcSource=src; ac.dcSource=src;
    const std::pair<const char*,SpiceOptions> cases[]={{"RC .tran",tr},{"RL .tran",rl},{"divider .dc",dc},{"RC .ac",ac}};
    for(const auto& item: cases){ AnalysisResult r;bool ok=runSpiceAnalysis(d,conn,item.second,r)&&finiteResult(r);
        if(ok && item.second.analysis==SpiceOptions::DC) ok=r.scale.size()>=5 && r.scale.front()<=0.01 && r.scale.back()>=4.99;
        if(ok && item.second.analysis==SpiceOptions::TRAN) ok=r.scale.back()>r.scale.front() && r.scale.size()>10;
        if(ok && item.second.analysis==SpiceOptions::AC) { bool hasMagnitude=false; for(const auto& n:r.vecNames) if(n.find("_mag")!=std::string::npos) hasMagnitude=true; ok=hasMagnitude && r.scale.back()>r.scale.front(); }
        std::printf("SPICE: %s %s (%zu rows, %zu vectors)\n",item.first,ok?"ok":"FAIL",r.scale.size(),r.data.size()); if(!ok) { std::printf("  error: %s\n", r.error.c_str()); failures++; }}
}
