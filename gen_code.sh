#!/bin/bash
# gen_code.sh [--force] [ROM] -- make the translated game code (c/gen/*.c, c/gen_z80/g4_z80.c) from
# YOUR OWN Gauntlet 4 ROM. The repositories don't contain it: it is generated here, in about 3 seconds,
# by the translator in this project (lift*.py, regen_all.sh). host/build.sh, host/build_win.sh and
# verify.sh run this first; it does nothing when the code is already there (--force: make it again).
#
# The ROM is looked for in this order: the ROM argument, $G4ROM, gauntlet4.bin in this folder,
# host/win/gauntlet4.bin, ~/GAMEPROJECT/gauntlet4-199309.bin. It must be the September 1993 version
# (CRC32 f9d60510). Python 3 with capstone and z80dis is needed: the first run makes ~/g4venv for it
# (the same environment verify.sh uses).
cd "$(dirname "$0")"
FORCE=0; [ "$1" = "--force" ] && { FORCE=1; shift; }

have_code() { [ -f c/gen/g4_gen_table.inc ] && [ -f c/gen_z80/g4_z80.c ] && ls c/gen/game_*.c >/dev/null 2>&1; }
if [ $FORCE = 0 ] && have_code; then exit 0; fi

ROM=""
for r in "$1" "$G4ROM" gauntlet4.bin host/win/gauntlet4.bin "$HOME/GAMEPROJECT/gauntlet4-199309.bin"; do
    if [ -n "$r" ] && [ -f "$r" ]; then ROM="$r"; break; fi
done
if [ -z "$ROM" ]; then
    echo "gen_code.sh: the game's translated code is made from your own Gauntlet 4 ROM, and none was found."
    echo "  Put it in this folder as gauntlet4.bin, or run:  bash gen_code.sh /path/to/your/rom.bin"
    exit 1
fi
CRC=$(python3 -c "import sys, zlib; print('%08x' % (zlib.crc32(open(sys.argv[1], 'rb').read()) & 0xffffffff))" "$ROM") || exit 1
if [ "$CRC" != "f9d60510" ]; then
    echo "gen_code.sh: $ROM is not the September 1993 Gauntlet 4 ROM (CRC32 $CRC, expected f9d60510)."
    echo "  The translation was made from that version only (the August 1993 ROM is not supported)."
    exit 1
fi

VENV="$HOME/g4venv"
if ! "$VENV/bin/python" -c "import capstone, z80dis" 2>/dev/null; then
    echo "gen_code.sh: setting up Python with capstone and z80dis in $VENV (first time only)..."
    [ -x "$VENV/bin/python" ] || python3 -m venv "$VENV" || {
        echo "  could not make $VENV -- in Ubuntu run:  sudo apt install python3-venv"; exit 1; }
    "$VENV/bin/pip" install -q capstone z80dis || { echo "  pip install failed"; exit 1; }
fi

echo "gen_code.sh: translating the game code from $ROM ..."
mkdir -p c/gen c/gen_z80
if ! G4ROM="$ROM" bash regen_all.sh "$VENV/bin/python" > gen_code.log 2>&1; then
    echo "  the translation failed: see gen_code.log"; exit 1
fi
if ls c/gen/*.failed.txt >/dev/null 2>&1; then
    echo "  some routines could not be translated: see c/gen/*.failed.txt and gen_code.log"; exit 1
fi
have_code || { echo "  the translation made no code: see gen_code.log"; exit 1; }
rm -f gen_code.log
echo "gen_code.sh: done ($(ls c/gen/*.c | wc -l) files in c/gen, plus c/gen_z80/g4_z80.c)"
