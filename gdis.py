#!/usr/bin/env python3
"""dis.py ADDR [ADDR...] -- full Ghidra disassembly of each function (bounds from functions_full.tsv)."""
import csv, sys, gh
F = sorted((int(r['address'], 16), int(r['size_bytes']), r['name']) for r in csv.DictReader(open('functions_full.tsv'), delimiter='\t'))
def dis(start, end, sid):
    out, a = [], start
    while a < end:
        r = gh.call('disassemble', {'address': '%08x' % a, 'count': 200, 'include_bytes': True}, sid)
        lines = [l for l in r['listing'].splitlines() if int(l.split()[0], 16) < end]
        if not lines: break
        out += lines; a = int(lines[-1].split()[0], 16) + len(lines[-1].split()[1]) // 2
        if len(lines) < 200: break
    return out
if __name__ == '__main__':
    sid = gh.session()
    for x in sys.argv[1:]:
        a = int(x, 16); f = [f for f in F if f[0] == a]
        end = a + f[0][1] if f else a + 0x100
        print(f'; ===== {a:06x} {f[0][2] if f else "?"} ({end - a} bytes)'); print('\n'.join(dis(a, end, sid)))
