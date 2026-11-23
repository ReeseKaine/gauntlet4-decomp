#!/bin/bash
# verify.sh -- check the C translation of Gauntlet 4 against the original ROM code.
#
# Everything is automatic. Results go to verify_log.txt (next to this script) and a summary is
# printed at the end. Run it from Windows with verify.bat, or in Ubuntu with:
#     bash verify.sh            the full check (takes a long time: the routine groups are slow)
#     bash verify.sh --quick    a short check (a few minutes): fewer random trials everywhere
#
# What it does:
#   1. makes a Python environment with the emulator it needs (only the first time)
#   2. builds the test libraries from the C sources
#   3. runs every comparison test: each translated routine against the real ROM routine in an
#      emulator, comparing registers, memory and hardware access
#   4. builds the game, to make sure it still compiles
# It changes nothing in the project except the test libraries it builds in c/.

cd "$(dirname "$0")"
HERE="$(pwd)"
LOG="$HERE/verify_log.txt"
ROM="${G4ROM:-$HOME/GAMEPROJECT/gauntlet4-199309.bin}"
VENV="$HOME/g4venv"
QUICK=0; [ "$1" = "--quick" ] && QUICK=1

: > "$LOG"
say()  { echo "$*" | tee -a "$LOG"; }
fail=0; passed=0; results=()
record() {   # record NAME STATUS
    results+=("$(printf '%-34s %s' "$1" "$2")")
    if [ "$2" = "PASS" ]; then passed=$((passed + 1)); else fail=$((fail + 1)); fi
}

say "Gauntlet 4 C translation check -- $(date)"
say "project: $HERE"
say "ROM:     $ROM"
[ $QUICK = 1 ] && say "mode:    quick (fewer trials)" || say "mode:    full"
say ""

# ---- 0. what is needed ------------------------------------------------------------------------
if [ ! -f "$ROM" ]; then
    say "STOP: the ROM was not found at $ROM"
    say "      Put gauntlet4-199309.bin there, or run:  G4ROM=/path/to/rom.bin bash verify.sh"
    exit 1
fi
for tool in gcc python3; do
    if ! command -v $tool >/dev/null; then
        say "STOP: $tool is missing. In Ubuntu run:  sudo apt install build-essential python3-venv"
        exit 1
    fi
done

# ---- 1. Python environment ----------------------------------------------------------------------
if [ ! -x "$VENV/bin/python" ] || ! "$VENV/bin/python" -c "import unicorn, capstone, z80" 2>/dev/null; then
    say "[1/4] setting up the Python environment (first time only)..."
    python3 -m venv "$VENV" >>"$LOG" 2>&1 || { say "STOP: could not make $VENV -- run: sudo apt install python3-venv"; exit 1; }
    "$VENV/bin/pip" install -q unicorn capstone pillow z80 z80dis >>"$LOG" 2>&1 || { say "STOP: pip install failed (see $LOG)"; exit 1; }
else
    say "[1/4] Python environment ready"
fi
PY="$VENV/bin/python"

# the translated code (c/gen/, c/gen_z80/) is not in the repositories: it is made from your ROM
bash "$HERE/gen_code.sh" "$ROM" 2>&1 | tee -a "$LOG"
if [ ! -f "$HERE/c/gen/g4_gen_table.inc" ]; then say "STOP: could not make the translated code (see above)"; exit 1; fi

# ---- 2. test libraries -------------------------------------------------------------------------
say "[2/4] building the test libraries..."
cd "$HERE/c"
build() {   # build NAME command...
    local name=$1; shift
    if "$@" >>"$LOG" 2>&1; then record "build $name" PASS; else record "build $name" FAIL; fi
}
build libg4_full.so gcc -std=c99 -O1 -w -shared -fPIC -o libg4_full.so g4_*.c gen/*.c test_shim.c
build libz80.so     gcc -std=c99 -O1 -DG4_Z80_COUNT -w -shared -fPIC -o libz80.so gen_z80/g4_z80.c z80_shim.c

# ---- 3. comparison tests -----------------------------------------------------------------------
# a test passes when it exits normally, prints no FAIL line and reports no mismatches
run_test() {   # run_test NAME command...
    local name=$1; shift
    say "      $name ..."
    echo "===== $name: $*" >>"$LOG"
    local out; out=$("$@" 2>&1); local rc=$?
    echo "$out" >>"$LOG"
    # failure: a non-zero exit, a FAIL line, or a non-zero "N mismatch" count (the bits/password tests)
    if [ $rc -eq 0 ] && ! echo "$out" | grep -qE '(^|[^A-Z])FAIL' && ! echo "$out" | grep -qE '(^|[^0-9])[1-9][0-9]* mismatch'; then
        record "$name" PASS; else record "$name" FAIL; fi
}
say "[3/4] comparing the C routines with the ROM routines..."
if [ $QUICK = 1 ]; then T1=200; T2=100; TZ=100; TG=5; else T1=3000; T2=600; TZ=1000; TG=100; fi
run_test "bit helpers"        "$PY" -u test_bits.py       "$ROM" ./libg4_full.so $T1
run_test "passwords"          "$PY" -u test_password.py   "$ROM" ./libg4_full.so $T2
run_test "passwords (b)"      "$PY" -u test_password_b.py "$ROM" ./libg4_full.so $T2
run_test "level decoder"      "$PY" -u test_level.py      "$ROM" ./libg4_full.so $T1
run_test "main loop"          "$PY" -u test_main.py       "$ROM" ./libg4_full.so
run_test "Z80 sound driver"   "$PY" -u test_z80.py        "$ROM" ./libz80.so $TZ
for f in gen/*.c; do
    n=$(basename "$f" .c)
    G4SNAP=1 run_test "routines: $n" "$PY" -u test_gen.py "$ROM" ./libg4_full.so $TG "$f"
done

# ---- 4. the game builds ------------------------------------------------------------------------
cd "$HERE"
if [ -f ./host/build.sh ]; then
    say "[4/4] building the game..."
    if ./host/build.sh >>"$LOG" 2>&1; then record "game build (Linux)" PASS; else record "game build (Linux)" FAIL; fi
else
    say "[4/4] game build skipped: the playable game is a separate project (Ghost)"
fi

# ---- summary -----------------------------------------------------------------------------------
say ""
say "================ RESULTS ================"
for r in "${results[@]}"; do say "$r"; done
say "========================================="
say "$passed passed, $fail failed"
[ $fail -eq 0 ] && say "Everything checks out." || say "Something failed: the details are in verify_log.txt (search for FAIL)."
say "Full log: $LOG"
exit $fail
