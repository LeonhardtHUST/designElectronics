#!/usr/bin/env python3
"""Checks that `designElectronics --mcp` attaches to the app the user has open:
a record written by the app (see src/live.h) must make the server forward tool
calls to that app instead of using its own empty design.

    python3 tests/mcp_attach_test.py path/to/designElectronics

Uses a fake app listening on 127.0.0.1, so no real window is needed. The record
folder is pointed at a temp dir with DE_LIVE_DIR, which is also what keeps this
test out of the user's own MCP sessions.
"""
import json, os, socket, subprocess, sys, tempfile, threading
import time

exe = sys.argv[1]
tmp = tempfile.mkdtemp(prefix="de-attach-")
instances = os.path.join(tmp, "instances")
os.makedirs(instances, exist_ok=True)

seen = []
srv = socket.socket()
srv.bind(("127.0.0.1", 0))
srv.listen(4)
port = srv.getsockname()[1]

PID = os.getpid()  # a process that really is running while the record is checked


def fake_app():
    while True:
        try:
            c, _ = srv.accept()
        except OSError:
            return
        f = c.makefile("rwb")
        try:
            for line in f:
                req = json.loads(line)
                seen.append(req)
                if req.get("method") == "hello":
                    ok = req.get("protocol") == 1 and req.get("token") == "tok-attach"
                    resp = {"ok": ok, "protocol": 1, "app": "designElectronics", "version": "test", "pid": PID}
                else:
                    ok = req.get("token") == "tok-attach"
                    resp = {"isError": not ok, "text": json.dumps({"fromApp": req["name"], "title": "Live Design"})}
                f.write((json.dumps(resp) + "\n").encode())
                f.flush()
        except Exception:
            pass
        finally:
            try:
                c.close()
            except OSError:
                pass


threading.Thread(target=fake_app, daemon=True).start()


def record(pid, port, token, focus):
    path = os.path.join(instances, "%d.json" % pid)
    with open(path, "w") as f:
        json.dump({"protocol": 1, "pid": pid, "port": port, "token": token, "version": "test",
                   "exe": exe, "startedAt": focus, "focusAt": focus}, f)
    return path


def run(args, env_extra=None):
    env = dict(os.environ, DE_LIVE_DIR=instances)
    env.update(env_extra or {})
    p = subprocess.Popen([exe, "--mcp"] + args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, env=env)
    n = 0

    def rpc(method, params):
        nonlocal n
        n += 1
        p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": n, "method": method, "params": params}) + "\n")
        p.stdin.flush()
        return json.loads(p.stdout.readline())

    init = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}})
    call = rpc("tools/call", {"name": "get_design", "arguments": {}})
    p.stdin.close()
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
    text = call["result"]["content"][0]["text"]
    try:
        body = json.loads(text)
    except json.JSONDecodeError:
        body = {"raw": text}
    return init, body, p.stderr.read()


# 1. a live-looking record: the server must talk to the fake app
record(PID, port, "tok-attach", int(time.time() * 1000))
init, body, err = run([])
attached = body.get("fromApp") == "get_design" and body.get("title") == "Live Design"
said_live = "live on their screen" in init["result"].get("instructions", "")
print("attach:    get_design came from the app:", attached, "| instructions mention the open app:", said_live)
print("           app saw:", seen[0] if seen else None)

# 2. --standalone must ignore the record
before = len(seen)
init2, body2, _ = run(["--standalone"])
standalone_ok = body2.get("standalone") is True and len(seen) == before
print("standalone: own design:", body2.get("standalone"), "| app not contacted:", len(seen) == before)

# 3. a stale record (dead pid, closed port) must not stop the server
os.remove(os.path.join(instances, "%d.json" % PID))
record(999999999, 1, "dead", 0)
init3, body3, _ = run([])
stale_ok = body3.get("standalone") is True

# 4. --attach refuses to run without an app
p = subprocess.Popen([exe, "--mcp", "--attach"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, text=True, env=dict(os.environ, DE_LIVE_DIR=instances))
out, err = p.communicate(timeout=10)
refused = p.returncode != 0


# 5. a private design that was already edited must not be swapped for the app's:
#    attaching later would silently throw away what the assistant built.
def lock_test():
    for f in os.listdir(instances):  # start with no window at all
        os.remove(os.path.join(instances, f))
    p = subprocess.Popen([exe, "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True, env=dict(os.environ, DE_LIVE_DIR=instances))

    def rpc(i, method, params):
        p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": i, "method": method, "params": params}) + "\n")
        p.stdin.flush()
        return json.loads(p.stdout.readline())

    rpc(1, "initialize", {"protocolVersion": "2025-06-18", "capabilities": {}})
    rpc(2, "tools/call", {"name": "new_design", "arguments": {"title": "Private Design"}})
    record(PID, port, "tok-attach", int(time.time() * 1000))  # a window shows up now
    r = rpc(3, "tools/call", {"name": "get_design", "arguments": {}})
    p.stdin.close()
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
    body = json.loads(r["result"]["content"][0]["text"])
    print("lock:      still the private design:", body.get("title") == "Private Design", "| standalone:", body.get("standalone"))
    return body.get("title") == "Private Design" and body.get("standalone") is True


locked = lock_test()

print("stale:     falls back to the private design:", stale_ok)
print("--attach:  refuses to start without the app:", refused, "(exit %d)" % p.returncode)
ok = attached and said_live and standalone_ok and stale_ok and refused and locked
print("MCP attach test", "passed" if ok else "FAILED")
sys.exit(0 if ok else 1)
