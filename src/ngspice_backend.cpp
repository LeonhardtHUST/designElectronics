#include "ngspice_backend.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <cctype>

#if DE_NGSPICE_DESKTOP
#if defined(_WIN32)
#include <windows.h>
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#else
#include <dlfcn.h>
#endif
#endif

namespace {
#if DE_NGSPICE_DESKTOP
struct NgComplex { double real; double imag; };
struct VecInfo { char* v_name; int v_type; short v_flags; double* v_realdata; NgComplex* v_compdata; int v_length; };
struct VecValue { char* name; double creal; double cimag; bool is_scale; bool is_complex; };
struct VecValuesAll { int veccount; int vecindex; VecValue** vecsa; };
struct Capture {
    std::vector<std::string> names;
    std::vector<std::vector<double>> real;
    std::vector<std::vector<double>> imag;
    std::vector<bool> complex;
    std::vector<bool> scale;
};
Capture* activeCapture = nullptr;
using SendCharFn = int (*)(char*, int, void*);
using SendStatFn = int (*)(char*, int, void*);
using ControlledExitFn = int (*)(int, bool, bool, int, void*);
using SendDataFn = int (*)(VecValuesAll*, int, int, void*);
using SendInitFn = int (*)(void*, int, void*);
using BgRunningFn = int (*)(bool, int, void*);
using GetVsrcFn = int (*)(double*, double, char*, int, void*);
using GetIsrcFn = int (*)(double*, double, char*, int, void*);
using GetSyncFn = int (*)(double, double*, double, int, int, int, void*);
using InitFn = int (*)(SendCharFn,SendStatFn,ControlledExitFn,SendDataFn,SendInitFn,BgRunningFn,void*);
using InitSyncFn = int (*)(GetVsrcFn,GetIsrcFn,GetSyncFn,int*,void*);
using CircFn = int (*)(char**); using CmdFn = int (*)(char*); using PlotFn = char* (*)();
using VecsFn = char** (*)(char*); using InfoFn = VecInfo* (*)(char*);
struct Api { void* lib=nullptr; InitFn init=nullptr; InitSyncFn initSync=nullptr; CircFn circ=nullptr; CmdFn cmd=nullptr; PlotFn plot=nullptr; VecsFn vecs=nullptr; InfoFn info=nullptr; };
Api api; std::mutex apiMutex; std::string callbackLog;
static int sendChar(char* s,int,void*) { if(s) callbackLog += s; return 0; }
static int sendStat(char* s,int,void*) { if(s) callbackLog += s; return 0; }
static int sendData(VecValuesAll* all, int, int, void*) {
    if (!activeCapture || !all) return 0;
    if (all->veccount <= 0 || !all->vecsa || !all->vecsa[0]) return 0;
    if (activeCapture->names.empty()) {
        activeCapture->names.reserve((size_t)all->veccount);
        activeCapture->real.resize((size_t)all->veccount);
        activeCapture->imag.resize((size_t)all->veccount);
        activeCapture->complex.resize((size_t)all->veccount, false);
        activeCapture->scale.resize((size_t)all->veccount, false);
        for (int i = 0; i < all->veccount; ++i) {
            VecValue* v = all->vecsa[i];
            activeCapture->names.push_back(v && v->name ? v->name : "vec" + std::to_string(i));
            activeCapture->complex[(size_t)i] = v && v->is_complex;
            activeCapture->scale[(size_t)i] = v && v->is_scale;
        }
    }
    for (int i = 0; i < all->veccount && i < (int)activeCapture->real.size(); ++i) {
        VecValue* v = all->vecsa[i];
        if (!v) continue;
        activeCapture->real[(size_t)i].push_back(v->creal);
        activeCapture->imag[(size_t)i].push_back(v->cimag);
    }
    return 0;
}
static int controlledExit(int, bool, bool, int, void*) { return 0; }
static int bgRunning(bool, int, void*) { return 0; }
static int getVsrc(double* value,double,char*,int,void*) { if(value) *value=0; return 0; }
static int getIsrc(double* value,double,char*,int,void*) { if(value) *value=0; return 0; }
static int getSync(double,double* delta,double oldDelta,int,int,int,void*) { if(delta && *delta<=0) *delta=oldDelta; return 0; }
static void* sym(const char* n) {
#if defined(_WIN32)
 return reinterpret_cast<void*>(GetProcAddress((HMODULE)api.lib,n));
#else
 return dlsym(api.lib,n);
#endif
}
static bool load(std::string* why) {
 if(api.lib) return true; std::vector<std::string> paths;
 if(const char* e=std::getenv("DE_NGSPICE_LIB"); e&&*e) paths.emplace_back(e);
#if defined(_WIN32)
 wchar_t buf[32768]; DWORD n=GetModuleFileNameW(nullptr,buf,32768); if(n){std::filesystem::path p(buf); paths.push_back((p.parent_path()/L"ngspice.dll").string());} paths.push_back("ngspice.dll");
#elif defined(__APPLE__)
 paths.push_back("libngspice.dylib");
#else
 paths.push_back("libngspice.so.0"); paths.push_back("libngspice.so");
#endif
 for(const auto& p:paths) {
#if defined(_WIN32)
  api.lib=(void*)LoadLibraryA(p.c_str());
#else
  api.lib=dlopen(p.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
  if(api.lib) break;
 }
 if(!api.lib){if(why)*why="libngspice not found; install ngspice and set DE_NGSPICE_LIB to its shared library";return false;}
 api.init=(InitFn)sym("ngSpice_Init"); api.initSync=(InitSyncFn)sym("ngSpice_Init_Sync"); api.circ=(CircFn)sym("ngSpice_Circ"); api.cmd=(CmdFn)sym("ngSpice_Command"); api.plot=(PlotFn)sym("ngSpice_CurPlot"); api.vecs=(VecsFn)sym("ngSpice_AllVecs"); api.info=(InfoFn)sym("ngGet_Vec_Info");
 if(!api.init||!api.circ||!api.cmd||!api.plot||!api.vecs||!api.info){if(why)*why="ngspice library lacks required API symbols";return false;} return true;
}
static std::string lower(std::string s){for(char& c:s)c=(char)std::tolower((unsigned char)c);return s;}
#endif
}

bool ngspiceAvailable(std::string* why) {
#if !DE_NGSPICE_DESKTOP
 if(why)*why="ngspice backend is unavailable on this platform"; return false;
#else
 std::lock_guard<std::mutex> lock(apiMutex); return load(why);
#endif
}

bool runSpiceAnalysis(const Document& doc,const Connectivity& conn,const SpiceOptions& options,AnalysisResult& out) {
 out=AnalysisResult{};
#if !DE_NGSPICE_DESKTOP
 out.error="ngspice backend is unavailable on this platform"; return false;
#else
 std::lock_guard<std::mutex> lock(apiMutex); std::string why; if(!load(&why)){out.error=why;return false;}
 SpiceNetlist net=buildSpice(doc,conn,options); callbackLog.clear();
 std::vector<std::string> lines; std::stringstream ss(net.text); std::string line; while(std::getline(ss,line)) if(!line.empty()) lines.push_back(line);
 std::vector<char*> ptrs; for(auto& l:lines) ptrs.push_back(l.data()); ptrs.push_back(nullptr);
 
 Capture capture; activeCapture = &capture; if(api.init(sendChar,sendStat,controlledExit,sendData,nullptr,bgRunning,nullptr)!=0){activeCapture=nullptr;out.error="ngspice initialization failed";out.log=callbackLog;return false;} api.cmd((char*)"destroy all");
 if(options.externalDigital){out.error="external digital source callbacks are not implemented; run with externalDigital=false";out.log=callbackLog;return false;}
 if(api.circ(ptrs.data())!=0 || api.cmd((char*)"run")!=0){activeCapture=nullptr;out.error="ngspice netlist/run failed";out.log=callbackLog;return false;} activeCapture=nullptr;
 out.log=callbackLog; if(char* p=api.plot()) out.plot=p; if (!capture.names.empty() && !capture.real.empty()) {
      size_t scaleIndex = 0;
      for (size_t i = 0; i < capture.names.size(); ++i) if (capture.scale[i] || lower(capture.names[i]).find("time") != std::string::npos || lower(capture.names[i]).find("frequency") != std::string::npos) { scaleIndex = i; break; }
      if (scaleIndex < capture.real.size()) out.scale = capture.real[scaleIndex];
      for (size_t i = 0; i < capture.names.size(); ++i) {
          if (i == scaleIndex || i >= capture.real.size()) continue;
          if (capture.complex[i]) {
              std::vector<double> mag, phase;
              for (size_t k = 0; k < capture.real[i].size(); ++k) { mag.push_back(std::hypot(capture.real[i][k], capture.imag[i][k])); phase.push_back(std::atan2(capture.imag[i][k], capture.real[i][k]) * 180.0 / M_PI); }
              out.vecNames.push_back(capture.names[i] + "_mag"); out.data.push_back(std::move(mag));
              out.vecNames.push_back(capture.names[i] + "_phase"); out.data.push_back(std::move(phase));
          } else { out.vecNames.push_back(capture.names[i]); out.data.push_back(capture.real[i]); }
      }
      for (auto& kv : net.nodeNet) { std::string target = "v(" + lower(kv.first) + ")", bare = lower(kv.first); for (size_t i = 0; i < out.vecNames.size(); ++i) { std::string vn = lower(out.vecNames[i]); if (vn == target || vn == bare || vn == target + "_mag" || vn == bare + "_mag") out.netV[kv.second] = out.data[i]; } }
      if (!out.scale.empty() && !out.data.empty()) { out.ok = true; return true; }
  } char** names=api.vecs(out.plot.empty()?nullptr:out.plot.data()); if(!names){out.error="ngspice returned no vectors";return false;}
 size_t n=0; while(names[n]) n++; if(!n){out.error="ngspice returned empty analysis data";return false;}
 std::vector<std::string> fullNames(n); std::vector<VecInfo*> infos(n); size_t rows=0; for(size_t i=0;i<n;i++){fullNames[i]=out.plot+"."+names[i]; infos[i]=api.info(fullNames[i].data()); if(!infos[i]) infos[i]=api.info(names[i]); if(!infos[i]||infos[i]->v_length<0){out.error="ngspice returned invalid vector info";return false;}rows=std::max(rows,(size_t)infos[i]->v_length);} if(!rows){out.error="ngspice returned empty analysis data";return false;}
 size_t scaleIndex=0; for(size_t i=0;i<n;i++){std::string nm=lower(names[i]);if(nm.find("time")!=std::string::npos||nm.find("frequency")!=std::string::npos){scaleIndex=i;break;}}
 auto append=[&](const char* name,VecInfo* vi){if(!vi)return;size_t len=(size_t)vi->v_length; if(vi->v_compdata){std::vector<double> mag(len),phase(len);for(size_t k=0;k<len;k++){double re=vi->v_compdata[k].real,im=vi->v_compdata[k].imag;mag[k]=std::hypot(re,im);phase[k]=std::atan2(im,re)*180.0/M_PI;}out.vecNames.emplace_back(std::string(name)+"_mag");out.data.push_back(std::move(mag));out.vecNames.emplace_back(std::string(name)+"_phase");out.data.push_back(std::move(phase));}else if(vi->v_realdata){out.vecNames.emplace_back(name);out.data.emplace_back(vi->v_realdata,vi->v_realdata+len);}};
 VecInfo* scale=infos[scaleIndex]; if(scale->v_realdata) out.scale.assign(scale->v_realdata,scale->v_realdata+(size_t)scale->v_length); else if(scale->v_compdata){out.scale.resize((size_t)scale->v_length);for(size_t k=0;k<out.scale.size();k++)out.scale[k]=scale->v_compdata[k].real;} 
 for(size_t i=0;i<n;i++)if(i!=scaleIndex)append(names[i],infos[i]);
 for(auto& kv:net.nodeNet){std::string target="v("+lower(kv.first)+")",bare=lower(kv.first);for(size_t i=0;i<out.vecNames.size();i++){std::string nm=lower(out.vecNames[i]);if(nm==target||nm==bare||nm==target+"_mag"||nm==bare+"_mag")out.netV[kv.second]=out.data[i];}}
 if(out.scale.empty()||out.data.empty()){out.error="ngspice returned no usable vectors";return false;} out.ok=true; return true;
#endif
}
