import json, re, sys
exec(open(sys.argv[1]).read().split("rows=")[0])   # MCP session helpers from scan.py
TERM=re.compile(r"^(rts|rte|rtr|jmp|bra(\.[bsw])?)\b")
BR=re.compile(r"^(b\w+|db\w+|jmp|bra\S*)\s+0x([0-9a-f]+)")
IND=re.compile(r"^jmp\s+\(")
def end_index(ins, hi):
    """index of the instruction that ends the function, or None if not yet provable"""
    tgt=[]; ind=False
    for k,(x,n,t) in enumerate(ins):
        m=BR.match(t)
        if m: tgt.append(int(m.group(2),16))
        if IND.match(t): ind=True
        if TERM.match(t) and not ind and not any(x<y<hi for y in tgt): return k
    return None
rows=[l.rstrip("\n").split("\t") for l in open(sys.argv[2]).readlines()[1:]]
starts=[int(r[0],16) for r in rows]
out=[]
for k,a in enumerate(starts):
    hi=starts[k+1] if k+1<len(starts) else 0x100000
    ins=[]; cur=a; e=None
    while cur<hi:
        lst=call("disassemble",{"binary_name":B,"address":f"{cur:08x}","count":200,"include_bytes":True})["listing"].splitlines()
        new=[]
        for l in lst:
            p=l.split(" ",2)
            try: x=int(p[0],16)
            except: break
            if x>=hi or (ins and x<=ins[-1][0]): break
            new.append([x,len(p[1])//2,p[2] if len(p)>2 else ""])
        if not new: break
        ins+=new; cur=new[-1][0]+new[-1][1]
        e=end_index(ins,hi)
        if e is not None: break
    out.append({"addr":a,"name":rows[k][1],"thunk":rows[k][4]=="True","hi":hi,"end":e,"ins":ins})
    print(k,end=" ",file=sys.stderr,flush=True)
json.dump(out,open(sys.argv[3],"w"))
