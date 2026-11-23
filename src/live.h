// designElectronics — the live-design rendezvous.
//
// An AI client starts `designElectronics --mcp` as its own process, so the MCP
// server cannot share memory with the window the user has open. The app therefore
// publishes where its MCP bridge listens — one small record per running app under
// <pref>/agent/instances/<pid>.json — and `--mcp` reads those records to attach
// to the open design instead of starting on an empty one.
#pragma once
#include <string>
#include <vector>

// Bumped when the record or the bridge handshake changes.
constexpr int kBridgeProtocol = 1;

// One running app that offers its open design to MCP clients.
struct LiveInstance {
    int pid = 0;
    int port = 0;  // 127.0.0.1, chosen by the app
    std::string token;
    std::string version;
    std::string exe;
    long long startedAt = 0;  // unix milliseconds
    long long focusAt = 0;    // last time the window was focused
};

namespace live {

int pid();  // this process

// Where the records live: $DE_LIVE_DIR when set (tests keep their own), else
// <pref>/agent/instances.
std::string dir();

// Publish this process's bridge so MCP clients can find it. Returns the file
// written (empty when there is no writable preference folder).
std::string publish(int port, const std::string& token, const std::string& version, const std::string& exe);
// The window gained focus: rewrite the record so this is the preferred instance
// when several are open.
void focus();
// Never let a record outlive its process.
void unpublish();

// Records found on disk, most recently focused first. Records of processes that
// are gone are removed on the way.
std::vector<LiveInstance> instances();

bool alive(int pid);  // is that process still running?
void forget(int pid);
// Does the bridge named by that record still answer, and is it this app (same
// pid, same protocol) rather than something that reused the port?
bool hello(const LiveInstance& in);

}  // namespace live
