/*
 * g4_dispatch.c -- call dispatcher and hardware hooks for the Gauntlet 4
 * C translation.
 *
 * Every JSR/BSR in translated code goes through g4_jsr(). If the target has
 * been translated (and g4_use_translated is set) the C routine runs;
 * otherwise the call is handed to g4_external(), which the host program
 * supplies -- e.g. a 68000 core that runs the original ROM code, or a test
 * harness. This lets the game be converted one routine at a time.
 */
#include <stddef.h>
#include "g4_m68k.h"
#include "gen/g4_gen_decls.inc"

G4_IO g4_io;                 /* no hooks: hardware addresses act as plain memory */
int   g4_use_translated = 1;
int16_t g4_hero_class_tab[4] = {-1, -1, -1, -1};   /* character select (g4_ops.h g4_hero_class) */

typedef struct { uint32_t addr; void (*fn)(M68K *); } G4_ROUTINE;

static const G4_ROUTINE g4_routines[] = {
    { 0x000400, g4_entry },
    { 0x000584, g4_game_init },
    { 0x0008C2, g4_play_level },
    { 0x000A58, g4_level_hud_a58 },
    { 0x000ADE, g4_start_skip },
    { 0x000B1E, g4_level_hud_b1e },
    { 0x000B5A, g4_game_setup },
    { 0x000C26, g4_setup_arcade },
    { 0x000C92, g4_setup_quest },
    { 0x000D3A, g4_setup_battle },
    { 0x000DD8, g4_setup_record },
    { 0x000E1C, g4_set_flags_e79c },
    { 0x000EA2, g4_hiscore_init },
    { 0x000EDC, g4_level_end },
    { 0x000F06, g4_arcade_next },
    { 0x00118C, g4_arcade_speed },
    { 0x0019EC, g4_quest_next },
    { 0x001A0E, g4_quest_from_hub },
    { 0x001B2E, g4_quest_from_stage },
    { 0x001C6E, g4_quest_tally },
    { 0x001ED6, g4_battle_next },
    { 0x001F22, g4_record_next },
    { 0x0040D8, g4_set_level_ptr },
    { 0x019122, g4_map_decode },
    { 0x01930A, g4_map_fill },
    { 0x019324, g4_map_vrun },
    { 0x019340, g4_map_remap },
#include "gen/g4_gen_table.inc"
};

/* a host program may replace routines (e.g. wait_vblank with its frame step); NULL = none */
void (*g4_host_override)(uint32_t addr, void (**fn)(M68K *));

void (*g4_lookup(uint32_t addr))(M68K *)
{
    size_t i;
    void (*fn)(M68K *) = NULL;
    addr = G4_ADDR(addr);
    for (i = 0; i < sizeof g4_routines / sizeof g4_routines[0]; i++)
        if (g4_routines[i].addr == addr) { fn = g4_routines[i].fn; break; }
    if (g4_host_override) g4_host_override(addr, &fn);
    return fn;
}

/* jmp to another routine (tail call): it returns straight to our caller */
void g4_jmp(M68K *c, uint32_t target)
{
    void (*fn)(M68K *) = g4_use_translated ? g4_lookup(target) : NULL;
    if (fn) fn(c);
    else    g4_external(c, G4_ADDR(target));
}

void g4_jsr(M68K *c, uint32_t ret_addr, uint32_t target)
{
    void (*fn)(M68K *) = g4_use_translated ? g4_lookup(target) : NULL;
    push32(c, ret_addr);
    if (fn) fn(c);
    else    g4_external(c, G4_ADDR(target));
    (void)pop32(c);
}
