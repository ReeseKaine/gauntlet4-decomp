#!/usr/bin/env python3
"""build_viewer.py <levels.json> <template.html> <out.html> -- embed extracted levels (and cell_codes.json
from the same folder) in the atlas page."""
import json, os, sys
src, tpl, out = sys.argv[1:4]
D = json.load(open(src))
B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
recs, rid, slots = [], {}, []
for s in D["levels"]:
    if s["record"] not in rid:
        rid[s["record"]] = len(recs)
        assert max(s["raw"]) < 64
        recs.append({"a": s["record"], "h": s["header"], "n": s["nibbles"], "p": s["palette"],
                     "g": "".join(B64[v] for v in s["raw"])})
    slots.append({"t": s["table"], "l": s["level"], "r": rid[s["record"]], "d": s["remap"]})
codes = json.load(open(os.path.join(os.path.dirname(os.path.abspath(src)), "cell_codes.json")))
data = json.dumps({"slots": slots, "recs": recs, "tables": D["remap_tables"], "codes": codes}, separators=(",", ":"))
html = open(tpl).read().replace("__DATA__", data).replace("__ROM__", D["rom"])
open(out, "w").write(html)
print(f"{out}: {len(slots)} slots, {len(recs)} records, {len(html)//1024} KB")
