# Gauntlet 4 (Mega Drive) decompilation

A translation of Gauntlet 4 (Sega Mega Drive / Genesis, 1993) from 68000 and Z80 machine code
into C, routine by routine. Every translated routine is checked against the original ROM code
in an emulator.

**No ROM is included.** You need your own copy of the Gauntlet 4 cartridge (September 1993
version).

Check your ROM's fingerprint (any ROM manager or hash tool shows these):

| Version | Size | CRC32 | SHA-1 |
|---|---|---|---|
| September 1993 (use this one) | 1,048,576 bytes | `f9d60510` | `d28e22207121f0e2980dd409b4fb24f9fb8967ae` |
| August 1993 (not supported: the translation was made from September) | 1,048,576 bytes | `3bf46dce` | `26c26ee2bb9571d51537d9328a5fd2a91b4e9dc1` |

## What is here

| Path | What |
|---|---|
| `c/g4_*.c` | Hand-written translations: main loop, passwords, level decoder. |
| `gen_code.sh` | Makes the machine translation from **your own ROM** (about 3 seconds): `c/gen/` (68000 routines; each line names its original address, e.g. `/* 01d664 move.w ... */`) and `c/gen_z80/` (the Z80 sound driver). These folders are not in the repository. |
| `lift*.py`, `regen_all.sh` | The translator itself (Python with `capstone` and `z80dis`; `gen_code.sh` sets that up). |
| `c/test_*.py` | Tests: each translated routine against the ROM routine (Unicorn emulator). |
| `verify.sh` / `verify.bat` | Runs every test. |
| `sram_patch.py` | Patches your own ROM to save to battery memory. |
| `functions*.tsv`, `ml/` | Maps of the ROM's routines and disassembly notes. |

## Making the translation

The machine-translated code is **not included**: you make it from your own ROM. In Linux or WSL,
with your ROM in this folder as `gauntlet4.bin` (or give its path):

    bash gen_code.sh                     # or: bash gen_code.sh /path/to/your/rom.bin

It checks that the ROM is the September 1993 version, sets up Python with `capstone` and
`z80dis` the first time (in `~/g4venv`), and writes `c/gen/` and `c/gen_z80/`. The output is
the same on every run, so everyone gets identical code. `verify.sh` runs it for you.

## Checking the translation

In Linux or WSL, with your ROM at `~/GAMEPROJECT/gauntlet4-199309.bin` (or set `G4ROM` to its
path):

    bash verify.sh --quick

The first run makes a Python environment with what it needs (`sudo apt install build-essential
python3-venv` first). On Windows with WSL (Ubuntu) installed, double-click `verify.bat` instead:
it asks for a quick or full check and runs the same tests. Use the unmodified ROM, not one
patched with `sram_patch.py`.

## Battery saves (SRAM patch)

Gauntlet 4 has no save memory: Quest, Battle and Record mode give you a password to write down.
`sram_patch.py` makes a patched copy of your own ROM that remembers those passwords in
battery-backed memory (SRAM), so emulators and flash carts keep them between sessions:

    python3 sram_patch.py gauntlet4.bin gauntlet4-sram.bin

It works on both the September and August 1993 ROMs (it tells them apart by the date in the
header). Your original file is not changed. Never share the patched ROM: it is a full copy of
the game.

### How it works

**Saving.** Whenever the game makes a password for a player who is in the game and has typed a
name, the patch copies the name and password into SRAM. There is one slot per password system
and hero: slots 0-3 for Quest/Battle passwords (30 characters) and slots 4-7 for Record mode
passwords (32 characters), one for each of the four heroes. The marker bytes `G`, `4` and a
version byte are written last, so a save cut short by a power loss is simply ignored.

**Loading.** When a player answers YES to "Enter password?", their password is still empty, and
the slot for their hero holds the same name they typed, the saved password is filled in for
them. Pressing Start then sends it through the game's own password decoder, so a damaged save
is rejected by the game's checksum like a mistyped password. If the player already has a
password in memory, or the names differ, nothing is filled in.

**The changes to the ROM:**

- The cartridge header gets an SRAM entry (8-bit memory on odd bytes, `$200001`-`$2003FF`),
  which tells emulators and flash carts to give the game battery memory.
- The new code (two small routines per password system, written in 68000 machine code by the
  script's own mini assembler) goes into an empty, zero-filled area of the ROM at `$FC000`.
- Four places in the game's code are changed to call it: for each system, the spot just after
  the password is made (saving) and the YES answer to "Enter password?" (loading). Each call
  first runs the original instructions it replaced.
- The header checksum is recomputed.

Before changing anything, the script checks the ROM's size, its header date, the exact bytes at
all four places, and that the code area and the header's SRAM field are empty. It stops if any
check fails, for example on an unknown ROM or one that is already patched. In the August build
the patched routines sit 0x22 bytes higher, and the script adjusts for that.

**SRAM layout** (each slot is 0x80 bytes; only odd addresses are used, so logical byte *n* of
slot *s* is at `$200001 + s*0x80 + 2*n`):

| Byte | Contents |
|---|---|
| 0, 1 | `G`, `4` (marks a valid save) |
| 2 | version: 1 = Quest/Battle, 2 = Record |
| 4-19 | name (up to 10 characters, ends with a zero byte) |
| 20- | password (30 or 32 characters) |

The heroes' slots are in the order Thor, Thyra, Merlin, Questor.

**Testing:** `python3 sram_emu_test.py gauntlet4-sram.bin` runs the patched code in an emulator
(Unicorn) for both systems: it makes a password with the game's own routine, checks it was
saved, loads it on a "fresh power-on", and decodes it with the game's decoder to check every
field comes back. It also checks that a different name or a damaged save is refused, that
players who never joined don't overwrite a save, and that Quest and Record saves stay apart.

## The playable version

The PC program built on this translation is a separate project,
**[Ghost](https://github.com/ReeseKaine/Ghost)**: an unofficial PC
port of Gauntlet 4 for Windows and Linux, with a launcher, extra level sets and online play.
If you want to play rather than study the code, go there.

## How this was made

This project was built with the help of Claude (Anthropic's AI assistant), which wrote much of
the translator, tools and code under my direction. Every translated routine is checked against
the original ROM by the included tests (`verify.sh`).

## Legal

Gauntlet 4 belongs to its rights holders. This project is not affiliated with them. It contains
no ROM data (no game code bytes, graphics, sound or music) and none of the machine translation:
that is generated on your own computer from your own cartridge dump. The hand-written files in
`c/g4_*.c` are rewrites of a few of the game's routines (main loop, passwords, level decoder),
shared for study and preservation.

## License

The code written for this project (the translator, tools, tests, hand-written C and the SRAM
patch) is under the MIT License: see `LICENSE`. The license cannot grant any rights to
Gauntlet 4 itself, which belongs to its rights holders; that includes the hand-written rewrites
of the game's routines and any code you generate from your ROM.
