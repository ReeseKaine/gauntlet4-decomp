import json, re, sys, urllib.request
U="http://127.0.0.1:8089/mcp"; B="/gauntlet4-199309.elf-4db237"
H={"Content-Type":"application/json","Accept":"application/json, text/event-stream"}
def post(body, sid=None):
    h=dict(H)
    if sid: h.update({"mcp-session-id":sid,"mcp-protocol-version":"2025-06-18"})
    r=urllib.request.urlopen(urllib.request.Request(U,json.dumps(body).encode(),h),timeout=120)
    txt=r.read().decode(); return r.headers.get("mcp-session-id"), txt
sid,_=post({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"client","version":"1"}}})
post({"jsonrpc":"2.0","method":"notifications/initialized"},sid)
def call(name,args,i=[2]):
    i[0]+=1
    _,t=post({"jsonrpc":"2.0","id":i[0],"method":"tools/call","params":{"name":name,"arguments":args}},sid)
    d=[l[6:] for l in t.splitlines() if l.startswith("data: ")][-1]
    return json.loads(d)["result"]["structuredContent"]
rows=[l.rstrip("\n").split("\t") for l in open(sys.argv[1]).readlines()[1:]]
addrs=[int(r[0],16) for r in rows]; names={int(r[0],16):r[1] for r in rows}
out=[]
for k,a in enumerate(addrs):
    nxt=addrs[k+1] if k+1<len(addrs) else 0x100000
    lst=call("disassemble",{"binary_name":B,"address":f"{a:08x}","count":51})["listing"].splitlines()
    ins=[]
    for l in lst:
        ad,_,txt=l.partition(" ")
        try: x=int(ad,16)
        except: break
        if x>=nxt: break
        ins.append((x,txt))
    hw=[]
    for x,t in ins:
        for h in re.findall(r"0x([0-9a-fA-F]+)",t):
            v=int(h,16)&0xFFFFFF
            if 0xC00000<=v<=0xC0001F or 0xA10000<=v<=0xA1FFFF: hw.append(f"{x:06x}:{t}")
    last=ins[-1][1].split()[0] if ins else ""
    out.append({"addr":f"{a:08x}","name":names[a],"n":len(ins),"capped":len(ins)>=51 or (len(ins)==51),"hw":hw,"last":last,"listing":[f"{x:08x} {t}" for x,t in ins]})
    print(k, end=" ", file=sys.stderr, flush=True)
json.dump(out,open(sys.argv[2],"w"),indent=1)
