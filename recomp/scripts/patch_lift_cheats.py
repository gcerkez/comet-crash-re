#!/usr/bin/env python3
"""Cheats stage 2: patch the lifted ppu_recomp_000.cpp to add a guest
"Cheats" submenu to the game's Options menu.

Design (verified against the lift + corpus, 2026-11-07; see
notes/RECONSTRUCTION-041 and cc-cheat-investigation/stage2_liftmap.md):
- The lifter merged op_dispatch + all handlers + both menu builders
  into ONE host function, func_000F6990. Dispatch computes its target
  from the GUEST jump table at runtime, so new opcodes are handled by
  intercepting them in host code right after the bounds bail (opcodes
  31/14/30/37/47 all map to the no-op epilogue in the guest table and
  have no other item use).
- Edit 4 appends a "Cheats" item (opcode 31) after Credits in the
  Options builder, mirroring the Credits append exactly but with a
  planted label (the game has no "Cheats" string).
- Edit 6 appends the same "Cheats" item (opcode 31) in the pause
  menu's Settings builder (guest FUN_000f88c8, dispatch opcode 74 —
  a separate builder from Options, not a gated variant), inserted at
  the builder's convergent tail (loc_000F8B7C) with its own free
  string temp (gpr[1]+0x86C) and gpr[20] as the temp pointer (gpr[19]
  carries the item-vector pointer in that builder's tail).
- loc_cheats_build mirrors the Video Record builder (loc_000FA0B4)
  construction idiom, minus its state-gating: alloc -> ctor -> title
  -> 4 state-labelled toggle rows -> layout/push tail verbatim.
  Labels are planted by the glue at guest 0x21010000 and are all
  <= 15 chars, so the string temps stay in SSO form and need no heap
  cleanup (each temp is still reset after use, mirroring the game's
  own idiom).
- Toggle rows use opcodes 14/30/37/47 -> loc_cheats_flip0..3, which
  call the glue's comet_cheats_flip(i), unlink-and-close the current
  submenu via FUN_001339E0 (menu anchor = game object + 0x53654C,
  guest 0x844B4C; its ->next is the open submenu), then re-run the
  builder (goto loc_cheats_build) so the row label is recomposed
  from the new state in place. The unlink matters: the builder
  INSERTS the fresh submenu at the anchor without removing the old
  one, so a bare rebuild stacks stale copies that Circle then has
  to wade through (driven-verified 2026-11-07; see
  cc-cheat-investigation/toggle_redraw.md and notes 041).

Assert-anchored and idempotent per edit: a file with the stage-2
edits but without Edit 6 gets only Edit 6; a fully patched file
exits 0 without changes. Usage:
    patch_lift_cheats.py <path-to-ppu_recomp_000.cpp>

Stage 3 (2026-11-08) is folded in the same way (marker "CHEATS
STAGE3", applied by apply_stage3 on both the fresh and the
already-stage-2 paths): a "Player: N" selector row (opcode 49) at
the top of the submenu cycles the glue's selected player; the four
toggle rows beneath it show/flip that player's toggles via the
unchanged comet_cheats_get/flip bridge. See
notes/RECONSTRUCTION-049.
"""
import sys

PATH = sys.argv[1]
src = open(PATH).read()

EDIT6_MARKER = 'CHEATS PATCH: append the "Cheats" item (pause Settings)'

def apply_edit6(s):
    """Append the "Cheats" item (opcode 31) in the pause Settings
    builder (guest FUN_000f88c8), at its convergent tail loc_000F8B7C.
    Mirrors Edit 4 with the differences that builder requires: string
    temp gpr[1]+0x86C (free in that builder; its fields +0x870/+0x880/
    +0x884 are likewise unreferenced there), temp pointer in gpr[20]
    (gpr[19] carries the item-vector pointer in F88C8's tail; gpr[20]
    is dead after the Effects slider fixup), and lr values from
    F88C8's own last append pair (D9218 site 0xF8B2C, append site
    0xF8B54 — the inserted copy reuses the append lr, so the append-lr
    count goes 1 -> 2). The temp holds an SSO string (<= 15 chars),
    so there is no heap buffer to reset or free."""
    if EDIT6_MARKER in s:
        return s
    assert s.count('ctx->lr = 0x000F8B54; func_001533D0(ctx);') == 1, \
        "edit6 guard: F8B54 append count != 1"
    anchor = ('loc_000F8B7C:\n'
              '        { int64_t a = (int32_t)ctx->gpr[23]; int64_t b = (int64_t)0; uint32_t cr_val = ((a < b) ? 8 : (a > b) ? 4 : 2) | ((ctx->xer >> 31) & 1u); ctx->cr = (ctx->cr & ~(0xFu << 28)) | (cr_val << 28); }')
    assert s.count(anchor) == 1, f"edit6 anchor count={s.count(anchor)}"
    snippet = '''        /* === CHEATS PATCH: append the "Cheats" item (pause Settings) === */
        ctx->gpr[20] = ctx->gpr[1] + (int64_t)(0x86C);
        ctx->gpr[28] = ppc_rldicl(ctx->gpr[20], 0, 32);
        ctx->gpr[4] = (int64_t)(uint32_t)(0x21010000u);
        ctx->gpr[3] = ctx->gpr[28] | ctx->gpr[28];
        ctx->lr = 0x000F8B2C; func_000D9218(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[3] = ctx->gpr[25] | ctx->gpr[25];
        ctx->gpr[4] = ctx->gpr[28] | ctx->gpr[28];
        ctx->gpr[5] = (int64_t)(int32_t)(31);
        ctx->gpr[6] = (int64_t)(int32_t)(0);
        ctx->gpr[7] = (int64_t)(int32_t)(0);
        ctx->gpr[8] = (int64_t)(int32_t)(0);
        ctx->gpr[9] = (int64_t)(int32_t)(0);
        ctx->gpr[10] = (int64_t)(int32_t)(0);
        ctx->lr = 0x000F8B54; func_001533D0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
'''
    label, compare = anchor.split('\n', 1)
    s = s.replace(anchor, label + '\n' + snippet + compare)
    assert s.count('ctx->lr = 0x000F8B54; func_001533D0(ctx);') == 2, \
        "edit6 post-count != 2"
    return s

STAGE3_MARKER = "CHEATS STAGE3"

def apply_stage3(s):
    """Stage 3: Player selector row in the Cheats submenu.

    T1: bridge declarations for comet_cheats_get_player /
        comet_cheats_cycle_player next to the stage-2 externs.
    T2: opcode 49 intercept -> loc_cheats_cycle.
    T3: the "Player: N" row appended FIRST in loc_cheats_build
        (temp gpr[1]+0x738, following the row idiom; label address
        computed from the glue's selected player, strings planted
        at 0x21010120 + player*0x20).
    T4: loc_cheats_cycle block (cycle + unlink + rebuild, exactly
        the flip blocks' shape) after loc_cheats_flip3.
    """
    if STAGE3_MARKER in s:
        return s

    def rep(old, new, tag):
        nonlocal s
        assert s.count(old) == 1, f"stage3 anchor '{tag}' count={s.count(old)}"
        s = s.replace(old, new)

    rep('''extern "C" int  comet_cheats_get(int idx);
extern "C" void comet_cheats_flip(int idx);''',
        '''extern "C" int  comet_cheats_get(int idx);
extern "C" void comet_cheats_flip(int idx);
/* === CHEATS STAGE3: Player selector bridge === */
extern "C" int  comet_cheats_get_player(void);
extern "C" void comet_cheats_cycle_player(void);''',
        "t1-externs")

    rep('''        if ((uint32_t)ctx->gpr[4] == 47) goto loc_cheats_flip3;   /* Health toggle */''',
        '''        if ((uint32_t)ctx->gpr[4] == 47) goto loc_cheats_flip3;   /* Health toggle */
        /* === CHEATS STAGE3: Player selector (opcode 49) === */
        if ((uint32_t)ctx->gpr[4] == 49) goto loc_cheats_cycle;   /* Player row */''',
        "t2-intercept")

    player_row = '''        /* === CHEATS STAGE3: Player selector row (appended first) === */
        ctx->gpr[11] = ctx->gpr[1] + (int64_t)(0x738);
        ctx->gpr[29] = ppc_rldicl(ctx->gpr[11], 0, 32);
        ctx->gpr[4] = (int64_t)(uint32_t)(0x21010120u + (uint32_t)comet_cheats_get_player() * 0x20u);
        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];
        ctx->lr = 0x000FA184; func_000D9218(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[3] = ctx->gpr[30] | ctx->gpr[30];
        ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];
        ctx->gpr[5] = (int64_t)(int32_t)(49);
        ctx->gpr[6] = (int64_t)(int32_t)(0);
        ctx->gpr[7] = (int64_t)(int32_t)(0);
        ctx->gpr[8] = (int64_t)(int32_t)(0);
        ctx->gpr[9] = (int64_t)(int32_t)(0);
        ctx->gpr[10] = (int64_t)(int32_t)(0);
        ctx->lr = 0x000FA1AC; func_001533D0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[28] = (int64_t)(int32_t)(0xF);
        ctx->gpr[31] = (int64_t)(int32_t)(0);
        vm_write32(ctx->gpr[1] + 0x750, ctx->gpr[28]);
        vm_write32(ctx->gpr[1] + 0x74C, ctx->gpr[31]);
        vm_write8(ctx->gpr[1] + 0x73C, ctx->gpr[31]);
'''
    thorium_anchor = '''        ctx->gpr[11] = ctx->gpr[1] + (int64_t)(0x71C);
        ctx->gpr[29] = ppc_rldicl(ctx->gpr[11], 0, 32);
        ctx->gpr[4] = (int64_t)(uint32_t)(comet_cheats_get(0) ? 0x21010020u : 0x21010040u);'''
    rep(thorium_anchor, player_row + thorium_anchor, "t3-player-row")

    # T4 anchor: flip3 is the last flip block, immediately before
    # loc_000F6BBC. (The flip blocks' comment text differs between
    # the folded script form and the tools lift's original
    # redraw+unlink form; the tail lines are identical in both.)
    rep('''        goto loc_cheats_build;
loc_000F6BBC:''',
        '''        goto loc_cheats_build;
loc_cheats_cycle:
        /* === CHEATS STAGE3: cycle the selected player, then the
           same unlink + rebuild as the toggle flips. === */
        comet_cheats_cycle_player();
        ctx->gpr[3] = (int64_t)(uint32_t)(0x844B4Cu);
        ctx->lr = 0x000F6BB0; func_001339E0(ctx); DRAIN_TRAMPOLINE(ctx);
        goto loc_cheats_build;
loc_000F6BBC:''',
        "t4-cycle-block")
    return s

if "CHEATS PATCH" in src and EDIT6_MARKER in src and "CHEATS STAGE3" in src:
    print("already patched:", PATH)
    sys.exit(0)

if "CHEATS PATCH" in src:
    # Stage-2 edits already applied (e.g. the tools lift): apply only
    # the missing incremental edits (Edit 6, then stage 3).
    out = src
    if EDIT6_MARKER not in out:
        out = apply_edit6(out)
    out = apply_stage3(out)
    open(PATH, "w").write(out)
    print("stage-2/3 lift patch: incremental edits applied to", PATH)
    sys.exit(0)

def rep(old, new, tag):
    global src
    assert src.count(old) == 1, f"anchor '{tag}' count={src.count(old)}"
    src = src.replace(old, new)

# --- Edit 1: glue bridge declarations at the TU top -------------------
rep('extern "C" void ps3_hle_call(unsigned int nid, ppu_context* ctx);',
    '''extern "C" void ps3_hle_call(unsigned int nid, ppu_context* ctx);

/* === CHEATS PATCH (stage 2): bridge to the glue's cheat state === */
extern "C" int  comet_cheats_get(int idx);
extern "C" void comet_cheats_flip(int idx);''',
    "edit1-externs")

# --- Edit 2: opcode intercepts in op_dispatch -------------------------
rep('''        if (((ctx->cr >> 2) & 1)) goto loc_000F6BBC;
        ctx->gpr[6] = ppc_rldic(ctx->gpr[4], 2, 30);
        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x6A2C);''',
    '''        if (((ctx->cr >> 2) & 1)) goto loc_000F6BBC;
        /* === CHEATS PATCH: opcodes the guest table leaves on the no-op === */
        if ((uint32_t)ctx->gpr[4] == 31) goto loc_cheats_build;   /* Options > Cheats */
        if ((uint32_t)ctx->gpr[4] == 14) goto loc_cheats_flip0;   /* Thorium toggle */
        if ((uint32_t)ctx->gpr[4] == 30) goto loc_cheats_flip1;   /* Buildables toggle */
        if ((uint32_t)ctx->gpr[4] == 37) goto loc_cheats_flip2;   /* Stockpiles toggle */
        if ((uint32_t)ctx->gpr[4] == 47) goto loc_cheats_flip3;   /* Health toggle */
        ctx->gpr[6] = ppc_rldic(ctx->gpr[4], 2, 30);
        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x6A2C);''',
    "edit2-intercepts")

# --- Edit 3: the builder + flip blocks, before the no-op epilogue -----
ROWS = [
    # (temp offset, On addr, Off addr, opcode, glue idx)
    (0x71C, 0x21010020, 0x21010040, 14, 0),   # Thorium
    (0x700, 0x21010060, 0x21010080, 30, 1),   # Buildables
    (0x6E4, 0x210100A0, 0x210100C0, 37, 2),   # Stockpiles
    (0x6C8, 0x210100E0, 0x21010100, 47, 3),   # Health
]

def row_block(temp, on, off, opcode, idx):
    return f'''        ctx->gpr[11] = ctx->gpr[1] + (int64_t)(0x{temp:X});
        ctx->gpr[29] = ppc_rldicl(ctx->gpr[11], 0, 32);
        ctx->gpr[4] = (int64_t)(uint32_t)(comet_cheats_get({idx}) ? 0x{on:08X}u : 0x{off:08X}u);
        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];
        ctx->lr = 0x000FA184; func_000D9218(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[3] = ctx->gpr[30] | ctx->gpr[30];
        ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];
        ctx->gpr[5] = (int64_t)(int32_t)({opcode});
        ctx->gpr[6] = (int64_t)(int32_t)(0);
        ctx->gpr[7] = (int64_t)(int32_t)(0);
        ctx->gpr[8] = (int64_t)(int32_t)(0);
        ctx->gpr[9] = (int64_t)(int32_t)(0);
        ctx->gpr[10] = (int64_t)(int32_t)(0);
        ctx->lr = 0x000FA1AC; func_001533D0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[28] = (int64_t)(int32_t)(0xF);
        ctx->gpr[31] = (int64_t)(int32_t)(0);
        vm_write32(ctx->gpr[1] + 0x{temp + 0x18:X}, ctx->gpr[28]);
        vm_write32(ctx->gpr[1] + 0x{temp + 0x14:X}, ctx->gpr[31]);
        vm_write8(ctx->gpr[1] + 0x{temp + 0x4:X}, ctx->gpr[31]);
'''

build_block = '''        /* === CHEATS PATCH: Cheats submenu builder + toggle handlers ===
           Reached only via the opcode intercepts above. Mirrors
           loc_000FA0B4's construction idiom with planted SSO labels at
           guest 0x21010000 (written by the glue at start-up). */
loc_cheats_build:
        ctx->gpr[3] = (int64_t)(int32_t)(0x158);
        ctx->lr = 0x000FA0BC; func_000B1D80(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = ((a < b) ? 8 : (a > b) ? 4 : 2) | ((ctx->xer >> 31) & 1u); ctx->cr = (ctx->cr & ~(0xFu << 24)) | (cr_val << 24); }
        ctx->gpr[30] = ppc_rldicl(ctx->gpr[3], 0, 32);
        if (((ctx->cr >> 25) & 1)) goto loc_cheats_title;
        ctx->gpr[4] = (int64_t)(int32_t)(-1);
        ctx->gpr[3] = ctx->gpr[30] | ctx->gpr[30];
        ctx->lr = 0x000FA0D8; func_0013E374(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
loc_cheats_title:
        ctx->gpr[5] = ctx->gpr[1] + (int64_t)(0x86C);
        ctx->gpr[16] = ppc_rldicl(ctx->gpr[5], 0, 32);
        ctx->gpr[4] = (int64_t)(uint32_t)(0x21010000u);
        ctx->gpr[3] = ctx->gpr[16] | ctx->gpr[16];
        ctx->lr = 0x000FA0FC; func_000D9218(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[3] = ctx->gpr[30] | ctx->gpr[30];
        ctx->gpr[4] = ctx->gpr[16] | ctx->gpr[16];
        ctx->lr = 0x000FA10C; func_00138CE0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[28] = (int64_t)(int32_t)(0xF);
        ctx->gpr[31] = (int64_t)(int32_t)(0);
        vm_write32(ctx->gpr[1] + 0x884, ctx->gpr[28]);
        vm_write32(ctx->gpr[1] + 0x880, ctx->gpr[31]);
        vm_write8(ctx->gpr[1] + 0x870, ctx->gpr[31]);
''' + "".join(row_block(*r) for r in ROWS) + '''        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x6A98);
        ctx->gpr[27] = ctx->gpr[26] + (int64_t)(int32_t)((uint32_t)0x53 << 16);
        { uint32_t tmp = vm_read32(ctx->gpr[2] + -0x6A94); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[10] = ftmp; }
        ctx->gpr[3] = ctx->gpr[30] | ctx->gpr[30];
        ctx->fpr[2] = ctx->fpr[10];
        ctx->gpr[26] = ctx->gpr[27] + (int64_t)(0x654C);
        { uint32_t tmp = vm_read32(ctx->gpr[2] + -0x6A70); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[3] = ftmp; }
        { uint32_t tmp = vm_read32(ctx->gpr[31] + 0x0); float ftmp; memcpy(&ftmp, &tmp, 4); ctx->fpr[9] = ftmp; }
        ctx->fpr[1] = ppu_fp_single(ppu_fmul(ctx->fpr[9], ctx->fpr[10]));
        ctx->lr = 0x000FA25C; func_00140DA8(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[3] = ppc_rldicl(ctx->gpr[26], 0, 32);
        ctx->gpr[4] = ctx->gpr[30] | ctx->gpr[30];
        ctx->lr = 0x000FA26C; func_001339D0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[3] = (int64_t)(int32_t)(1);
        goto loc_000F6BC0;
loc_cheats_flip0:
        comet_cheats_flip(0);
        /* CHEATS REDRAW + UNLINK: close the current submenu (the
           menu anchor's head, guest 0x844B4C) via the game's own
           unlink-and-close helper, then rebuild so the label
           reflects the new state. Without the unlink each rebuild
           stacks a stale submenu copy above the old one. */
        ctx->gpr[3] = (int64_t)(uint32_t)(0x844B4Cu);
        ctx->lr = 0x000F6BB0; func_001339E0(ctx); DRAIN_TRAMPOLINE(ctx);
        goto loc_cheats_build;
loc_cheats_flip1:
        comet_cheats_flip(1);
        /* CHEATS REDRAW + UNLINK: see flip0. */
        ctx->gpr[3] = (int64_t)(uint32_t)(0x844B4Cu);
        ctx->lr = 0x000F6BB0; func_001339E0(ctx); DRAIN_TRAMPOLINE(ctx);
        goto loc_cheats_build;
loc_cheats_flip2:
        comet_cheats_flip(2);
        /* CHEATS REDRAW + UNLINK: see flip0. */
        ctx->gpr[3] = (int64_t)(uint32_t)(0x844B4Cu);
        ctx->lr = 0x000F6BB0; func_001339E0(ctx); DRAIN_TRAMPOLINE(ctx);
        goto loc_cheats_build;
loc_cheats_flip3:
        comet_cheats_flip(3);
        /* CHEATS REDRAW + UNLINK: see flip0. */
        ctx->gpr[3] = (int64_t)(uint32_t)(0x844B4Cu);
        ctx->lr = 0x000F6BB0; func_001339E0(ctx); DRAIN_TRAMPOLINE(ctx);
        goto loc_cheats_build;
'''

rep('''loc_000F6BBC:
        ctx->gpr[3] = (int64_t)(int32_t)(0);''',
    build_block + '''loc_000F6BBC:
        ctx->gpr[3] = (int64_t)(int32_t)(0);''',
    "edit3-blocks")

# --- Edit 4: the "Cheats" item in the Options builder ------------------
rep('''        ctx->lr = 0x000F8544; func_001533D0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[21] = vm_read32(ctx->gpr[1] + 0x76C);''',
    '''        ctx->lr = 0x000F8544; func_001533D0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        /* === CHEATS PATCH: append the "Cheats" item after Credits === */
        ctx->gpr[19] = ctx->gpr[1] + (int64_t)(0x5CC);
        ctx->gpr[28] = ppc_rldicl(ctx->gpr[19], 0, 32);
        ctx->gpr[4] = (int64_t)(uint32_t)(0x21010000u);
        ctx->gpr[3] = ctx->gpr[28] | ctx->gpr[28];
        ctx->lr = 0x000F851C; func_000D9218(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[3] = ctx->gpr[25] | ctx->gpr[25];
        ctx->gpr[4] = ctx->gpr[28] | ctx->gpr[28];
        ctx->gpr[5] = (int64_t)(int32_t)(31);
        ctx->gpr[6] = (int64_t)(int32_t)(0);
        ctx->gpr[7] = (int64_t)(int32_t)(0);
        ctx->gpr[8] = (int64_t)(int32_t)(0);
        ctx->gpr[9] = (int64_t)(int32_t)(0);
        ctx->gpr[10] = (int64_t)(int32_t)(0);
        ctx->lr = 0x000F8544; func_001533D0(ctx); DRAIN_TRAMPOLINE(ctx);
        /* nop */;
        ctx->gpr[21] = (int64_t)(int32_t)(0xF);
        ctx->gpr[19] = (int64_t)(int32_t)(0);
        vm_write32(ctx->gpr[1] + 0x5E4, ctx->gpr[21]);
        vm_write32(ctx->gpr[1] + 0x5E0, ctx->gpr[19]);
        vm_write8(ctx->gpr[1] + 0x5D0, ctx->gpr[19]);
        ctx->gpr[21] = vm_read32(ctx->gpr[1] + 0x76C);''',
    "edit4-options-item")

# --- Edit 6: the "Cheats" item in the pause Settings builder ----------
src = apply_edit6(src)

# --- Stage 3: Player selector row --------------------------------------
src = apply_stage3(src)

open(PATH, "w").write(src)
print("stage-2 lift patch applied to", PATH)
