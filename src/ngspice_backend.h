#pragma once
#include "tools.h"
#include <functional>

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__) || defined(SDL_PLATFORM_IOS) || defined(__IPHONE_OS_VERSION_MAX_ALLOWED) || (defined(__APPLE__) && defined(TARGET_OS_IOS) && TARGET_OS_IOS) || defined(IOS)
#define DE_NGSPICE_DESKTOP 0
#else
#define DE_NGSPICE_DESKTOP 1
#endif

struct AnalysisResult {
    bool ok = false;
    std::string error;
    std::string plot;
    std::vector<std::string> vecNames;
    std::vector<std::vector<double>> data;
    std::vector<double> scale;
    std::map<int, std::vector<double>> netV;
    std::string log;
};

bool ngspiceAvailable(std::string* why = nullptr);
bool runSpiceAnalysis(const Document&, const Connectivity&, const SpiceOptions&, AnalysisResult&);
using ExternalDriver = std::function<double(const SpiceNetlist::Ext&, double)>;
