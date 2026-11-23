// designElectronics — see live.h: where a running app publishes its MCP bridge,
// and how `designElectronics --mcp` finds it again.
#include "live.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "json.h"
#include "net.h"

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <csignal>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if defined(DE_HAVE_NET)
#include <filesystem>
#endif

namespace live {
namespace {

std::string g_path;    // this process's record (empty: not published)
LiveInstance g_self;   // kept so focus() can rewrite the record

long long nowMs() {
    using namespace std::chrono;
    return (long long)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string slurp(const std::string& path) {
    size_t n = 0;
    void* data = SDL_LoadFile(path.c_str(), &n);
    if (!data) return {};
    std::string s((const char*)data, n);
    SDL_free(data);
    return s;
}

bool spit(const std::string& path, const std::string& data) {
    // Written aside and renamed over the old record: a client reading while the
    // app refreshes focusAt then sees either the old or the new record, never half.
    std::string tmp = path + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), data.data(), data.size())) return false;
#ifndef _WIN32
    // The token is not a secret from the user, but it is nobody else's either.
    ::chmod(tmp.c_str(), S_IRUSR | S_IWUSR);
#endif
    if (!SDL_RenamePath(tmp.c_str(), path.c_str())) {  // replaces the old file
        SDL_RemovePath(tmp.c_str());
        return false;
    }
    return true;
}

std::string recordBody(const LiveInstance& in) {
    Json j = Json::object();
    j.set("protocol", kBridgeProtocol);
    j.set("pid", in.pid);
    j.set("port", in.port);
    j.set("token", in.token);
    j.set("version", in.version);
    j.set("exe", in.exe);
    j.set("startedAt", (int64_t)in.startedAt);
    j.set("focusAt", (int64_t)in.focusAt);
    return j.dump(1) + "\n";
}

// A record we couldn't fully read is useless; one from another protocol version
// may name a socket that speaks something else.
bool fromJson(const Json& j, LiveInstance& out) {
    if (!j.isObject() || j["protocol"].i() != kBridgeProtocol) return false;
    out.pid = j["pid"].i();
    out.port = j["port"].i();
    out.token = j["token"].str();
    out.version = j["version"].str();
    out.exe = j["exe"].str();
    out.startedAt = (long long)j["startedAt"].num();
    out.focusAt = (long long)j["focusAt"].num();
    return out.pid > 0 && out.port > 0 && !out.token.empty();
}

}  // namespace

int pid() {
#ifdef _WIN32
    return (int)_getpid();
#else
    return (int)getpid();
#endif
}

std::string dir() {
    const char* env = SDL_getenv("DE_LIVE_DIR");
    if (env && *env) return std::string(env);
    char* pref = SDL_GetPrefPath("designElectronics", "designElectronics");
    std::string base = pref ? pref : "";
    SDL_free(pref);
    if (base.empty()) return {};
    return base + "agent/instances";
}

std::string publish(int port, const std::string& token, const std::string& version, const std::string& exe) {
    std::string d = dir();
    if (d.empty() || port <= 0) return {};
    // SDL_CreateDirectory makes the parents too, but only if they are missing.
    SDL_CreateDirectory(d.c_str());
    g_self.pid = pid();
    g_self.port = port;
    g_self.token = token;
    g_self.version = version;
    g_self.exe = exe;
    g_self.startedAt = g_self.focusAt = nowMs();
    g_path = d + "/" + std::to_string(g_self.pid) + ".json";
    if (!spit(g_path, recordBody(g_self))) g_path.clear();
    return g_path;
}

void focus() {
    if (g_path.empty()) return;
    g_self.focusAt = nowMs();
    spit(g_path, recordBody(g_self));
}

void unpublish() {
    if (!g_path.empty()) SDL_RemovePath(g_path.c_str());
    g_path.clear();
}

bool alive(int p) {
    if (p <= 0) return false;
    if (p == pid()) return true;
#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)p);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;  // exists, we just may not ask (an elevated app)
    DWORD code = 0;
    bool ok = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return ok;
#else
    if (::kill(p, 0) == 0) return true;
    return errno == EPERM;  // exists, owned by somebody else
#endif
}

#if defined(DE_HAVE_NET)

std::vector<LiveInstance> instances() {
    std::vector<LiveInstance> out;
    std::string d = dir();
    if (d.empty()) return out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(d, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".json") continue;
        LiveInstance in;
        if (!fromJson(Json::parse(slurp(it->path().string())), in)) continue;
        if (!alive(in.pid)) {  // crashed or was killed: its socket is gone too
            SDL_RemovePath(it->path().string().c_str());
            continue;
        }
        out.push_back(in);
    }
    std::sort(out.begin(), out.end(), [](const LiveInstance& a, const LiveInstance& b) {
        if (a.focusAt != b.focusAt) return a.focusAt > b.focusAt;
        return a.startedAt > b.startedAt;
    });
    return out;
}

void forget(int p) {
    std::string d = dir();
    if (d.empty() || p <= 0) return;
    SDL_RemovePath((d + "/" + std::to_string(p) + ".json").c_str());
}

bool hello(const LiveInstance& in) {
    if (in.port <= 0) return false;
    net::Sock s = net::connectLocal(in.port);
    if (s == net::kInvalid) return false;
    net::setTimeout(s, 2000);  // a foreign listener could accept and stay silent
    Json req = Json::object();
    req.set("method", "hello");
    req.set("protocol", kBridgeProtocol);
    req.set("token", in.token);
    std::string buf, line;
    bool sent = net::sendAll(s, req.dump(-1) + "\n") && net::readLine(s, buf, line);
    net::closeSock(s);
    if (!sent) return false;
    // Not just "something answers there": it must be the process the record names.
    // The port could have been reused by an unrelated local service since.
    Json r = Json::parse(line);
    return r["ok"].boolean() && r["protocol"].i() == kBridgeProtocol &&
           r["app"].str() == "designElectronics" && r["pid"].i() == in.pid;
}

#else

std::vector<LiveInstance> instances() { return {}; }
void forget(int) {}
bool hello(int, const std::string&) { return false; }

#endif

}  // namespace live
