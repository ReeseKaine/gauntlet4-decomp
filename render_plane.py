#!/usr/bin/env python3
"""render_plane.py -- helpers to turn VDP state (VRAM, CRAM, registers) into RGB images."""
from PIL import Image

def cram_rgb(c):
    # Mega Drive colour 0000BBB0GGG0RRR0 -> 8-bit RGB (simple linear scale)
    r, g, b = (c >> 1) & 7, (c >> 5) & 7, (c >> 9) & 7
    return (r * 36, g * 36, b * 36)

def tile_pixels(vram, idx):
    base = (idx & 0x7FF) * 32
    out = []
    for row in range(8):
        for col in range(4):
            v = vram[base + row * 4 + col]
            out += [v >> 4, v & 15]
    return out                                          # 64 colour indices, row-major

def render_plane(vram, cram, base, w=64, h=64, backdrop=None):
    pal = [cram_rgb(c) for c in cram]
    img = Image.new("RGB", (w * 8, h * 8), pal[backdrop] if backdrop is not None else (0, 0, 0))
    px = img.load()
    for ty in range(h):
        for tx in range(w):
            a = base + (ty * w + tx) * 2
            e = (vram[a] << 8) | vram[a + 1]
            idx, hf, vf, p = e & 0x7FF, (e >> 11) & 1, (e >> 12) & 1, (e >> 13) & 3
            t = tile_pixels(vram, idx)
            for y in range(8):
                for x in range(8):
                    ci = t[(7 - y if vf else y) * 8 + (7 - x if hf else x)]
                    if ci: px[tx * 8 + x, ty * 8 + y] = pal[p * 16 + ci]
    return img

def objects(emu):
    """All objects reachable from the active list and the two per-cell object tables."""
    seen = {}
    n = (emu.r16(0xFFAF14) + 1) & 0xFFFF
    ptrs = [emu.r32(0xFFAA64 + 4 * i) for i in range(n if n < 0x400 else 0)]
    for base in (0xFFC246, 0xFFD246):
        ptrs += [emu.r32(base + 4 * i) & 0x7FFFFFFF for i in range(1024)]
    for o in ptrs:
        o &= 0xFFFFFF
        if not (0xFF0B62 <= o < 0xFFA362) or (o - 0xFF0B62) % 0x26: continue
        t = emu.r16(o)
        if t == 0 or o in seen: continue
        seen[o] = dict(addr=o, type=t, x=emu.r16(o + 2), y=emu.r16(o + 4), size=emu.r16(o + 8),
                       look=emu.r16(o + 10), sub=emu.r8(o + 0x16))
    return list(seen.values())

def draw_sprite(img, vram, cram, look, size_word, x, y):
    """Mega Drive sprite: tiles run down each column first; look = nametable-style word."""
    pal = [cram_rgb(c) for c in cram]
    s = (size_word >> 8) & 0x0F
    w, h = ((s >> 2) & 3) + 1, (s & 3) + 1
    idx, hf, vf, p = look & 0x7FF, (look >> 11) & 1, (look >> 12) & 1, (look >> 13) & 3
    px = img.load()
    for cx in range(w):
        for cy in range(h):
            t = tile_pixels(vram, idx + cx * h + cy)
            dx = (w - 1 - cx) if hf else cx
            dy = (h - 1 - cy) if vf else cy
            for yy in range(8):
                for xx in range(8):
                    ci = t[(7 - yy if vf else yy) * 8 + (7 - xx if hf else xx)]
                    X, Y = x + dx * 8 + xx, y + dy * 8 + yy
                    if ci and 0 <= X < img.width and 0 <= Y < img.height:
                        px[X, Y] = pal[p * 16 + ci]
