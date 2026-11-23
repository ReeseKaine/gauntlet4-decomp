#!/usr/bin/env python3
"""gh.py TOOL '{json args}'  -- call a pyghidra-mcp tool, print text/structured result."""
import json, sys, urllib.request
U = "http://127.0.0.1:8089/mcp"; BIN = "/gauntlet4-199309.elf-4db237"
H = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream", "mcp-protocol-version": "2025-06-18"}
def post(body, sid=None, timeout=300):
    h = dict(H); 
    if sid: h["mcp-session-id"] = sid
    r = urllib.request.urlopen(urllib.request.Request(U, json.dumps(body).encode(), h), timeout=timeout)
    return r.headers.get("mcp-session-id"), r.read().decode()
def session():
    sid, _ = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "client", "version": "1.0"}}})
    post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid); return sid
def call(tool, args, sid=None):
    sid = sid or session(); args.setdefault("binary_name", BIN)
    _, txt = post({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": tool, "arguments": args}}, sid)
    for line in txt.splitlines():
        if line.startswith("data: "):
            res = json.loads(line[6:])
            if "error" in res: return res["error"]
            r = res["result"]; return r.get("structuredContent") or [c.get("text") for c in r["content"]]
if __name__ == "__main__":
    if sys.argv[1] == "tools":
        sid = session(); _, t = post({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}}, sid)
        for l in t.splitlines():
            if l.startswith("data: "):
                for x in json.loads(l[6:])["result"]["tools"]: print(x["name"], "-", list(x["inputSchema"]["properties"]))
    else:
        print(json.dumps(call(sys.argv[1], json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}), indent=1))
