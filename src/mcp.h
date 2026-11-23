// designElectronics — MCP tools as a library: used by `designElectronics --mcp`
// (stdio server) and by the in-app AI tab (the live design).
#pragma once
#include <functional>
#include <memory>
#include <string>

#include "doc.h"

class McpSession {
public:
    // live = nullptr: the session owns its own design (standalone server).
    explicit McpSession(Document* live = nullptr);
    ~McpSession();
    // Run one tool; returns its text result and whether it failed.
    std::string callTool(const std::string& name, const Json& args, bool& isError);
    // Hooks for the app: before/after a tool that changes the design (undo, redraw),
    // and when the whole design was replaced (fit the view).
    std::function<void()> beforeEdit, afterEdit, onReplaced;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// What a plain `--mcp` server does when it wasn't told where an app listens.
enum class McpAttach {
    kAuto,        // attach to the designElectronics window the user has open, if any
    kStandalone,  // always keep a private design (--standalone)
    kRequire,     // only work on the open window, refuse to start without one (--attach)
};

// stdio MCP server. With connectPort > 0 it forwards tool calls to the running
// app (the AI tab), which applies them to the open design. Without one, kAuto
// looks for the app's published bridge (see live.h) and attaches to that design.
int runMcpServer(int connectPort = 0, const std::string& token = "", McpAttach attach = McpAttach::kAuto);
