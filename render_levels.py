#!/usr/bin/env python3
"""
render_levels.py <rom.bin> <levels.json> <out_dir>

Renders every distinct level record as the game draws it: boots the real ROM
headless (g4emu.py), sets the level up with the game's own code, then draws
plane A (VRAM 0xE000) plus every object as a sprite (look word +$0A, size +$08).
Arcade tables run in Arcade mode; levels 0x81-0x8D in Battle mode; 0x8E as the
Quest hub; 0x8F+ as Quest stage (n % 10)+1 of area n // 10, n = level-0x8F. Writes <out_dir>/<record>.png
(palette PNG, 512x512) and <out_dir>/render.json (per record: status).
The two random steps of level_build are skipped (see g4emu.py), so each image
matches the decoded map cell for cell.
"""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from g4emu import Emu
from render_plane import render_plane, objects, draw_sprite

rom = open(sys.argv[1], "rb").read()
D = json.load(open(sys.argv[2]))
out = sys.argv[3]; os.makedirs(out, exist_ok=True)
jobs = {}
for s in D["levels"]:
    if s["record"] in jobs: continue
    mode = 1 if s["table"] == "quest_battle" else 0
    jobs[s["record"]] = (mode, s["level"], s["table"] == "arcade_alt")
status = {}; t0 = time.time()
for rec, (mode, level, alt) in jobs.items():
    name = "%06x" % rec
    try:
        e = Emu(rom)
        where = e.boot_to_level(mode, level, alt)
        if where != 0x94C: raise RuntimeError(f"stopped at {where}")
        if e.r32(0xFFE75C) != rec: raise RuntimeError(f"level pointer {e.r32(0xFFE75C):#x}")
        img = render_plane(e.vdp.vram, e.vdp.cram, 0xE000, backdrop=e.vdp.regs[7] & 0x3F)
        objs = objects(e)
        for o in objs:
            sz = (o["size"] >> 8) & 15; w, h = ((sz >> 2) & 3) + 1, (sz & 3) + 1
            draw_sprite(img, e.vdp.vram, e.vdp.cram, o["look"], o["size"], o["x"] + 8 - w * 4, o["y"] + 8 - h * 4)
        img.convert("P", palette=1, colors=255).save(os.path.join(out, name + ".png"), optimize=True)
        status[name] = {"ok": True, "objects": len(objs), "mode": mode, "level": level}
    except Exception as x:
        status[name] = {"ok": False, "error": str(x), "mode": mode, "level": level}
json.dump(status, open(os.path.join(out, "render.json"), "w"), indent=1)
bad = {k: v for k, v in status.items() if not v["ok"]}
size = sum(os.path.getsize(os.path.join(out, k + ".png")) for k in status if status[k]["ok"])
print(f"{len(status) - len(bad)}/{len(status)} rendered in {time.time() - t0:.0f}s, {size // 1024} KB of PNG; failures: {bad}")
