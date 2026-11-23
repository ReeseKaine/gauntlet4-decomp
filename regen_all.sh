#!/bin/bash
# regen_all.sh -- regenerate all lifted C (c/gen/) from the ROM, in the order the groups depend on.
# usage: ./regen_all.sh PYTHON   (a python with capstone installed)
set -e
PY=${1:-python3}
cd "$(dirname "$0")"
rm -f c/gen/*.c c/gen/*.failed.txt
$PY -I lift.py sound ff534=snd_init ff2ea=snd_tick ff640=snd_fe008 fed3e=snd_update ff524=snd_music_req \
    ff4ac=snd_z80_cmd ff4dc=snd_z80_cmds fe01c=snd_clear_mailbox fec86=snd_a6_handler --closure=fe000-ff700
$PY -I lift.py vblank 1a270=vblank 1b7f4 1be3a 1c986 1eb98 1eec2 1ede0
$PY -I lift.py irq 1b936=hblank_cont --keep-going --closure=1b800-1bc00
$PY -I lift_all.py
$PY -I lift.py options 24102 241a4 241b6 245c4 24668 --keep-going --closure=400-100000   # Options menu handlers: two-level table 0x24096 (per mode) lift_missing cannot follow
$PY -I lift.py quest 11e96 --keep-going --closure=400-100000   # called only from hand-written quest_next (c/g4_levelend.c), which lift_missing does not scan
$PY -I lift_missing.py
$PY -I lift_z80.py
