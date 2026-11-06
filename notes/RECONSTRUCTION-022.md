# Reconstruction notes 022 — camera pass 2, and the
Antigravity second-analyst route

Date: 2026-11-06. Continuation of notes 021's probe list.

## The 0x133xxx family is a struct/vector helper library

- FUN_00133920 (called by FUN_000ff9d8 with five args) is a
  plain struct filler: it writes four floats (from double
  args) into a struct at +0x04/+0x08/+0x0C/+0x1C. So ff9d8
  computes four values per entity and stores them into the
  per-entity struct (base &DAT_00539f94 + id) — they are
  entity fields, not a view matrix.
- FUN_00133f54 resolves to bad data (a mis-carved island).
- Moral: this block is small helpers (setters/getters/
  vector ops), not the projection pipeline itself.

## FUN_00142f0c is a per-level object constructor

Called by the level-start handler FUN_000f7a68 as
FUN_00142f0c(FUN_000b1d80(300), level_value, 0x16, 1): it
initializes a fresh 300-byte object from a defaults table
reached via TOC slots 0x1F6B5C-0x1F6E78 (fields copied into
[0..0x21], flag bytes at +0x5D..+0x5F, a count/type field
[0x1E] = 0xF, and a sub-object init via FUN_000d81c4).
Kind/id argument 0x16 (22). Object identity still open,
but this is the object the level-start op builds on the
per-level runtime context (+0x53654C).

## Who WRITES the view-state block (writer sweep)

scripts/scan_writers.py (boundary-free store sweep) over
0x1F5E00-0x1F5F00 found exactly one substantive writer:
FUN_00104890 (5,356 B) stores the four-field group
0x1F5E38/3C/40/44 at two sites. Its body processes ranges
of 16-byte records keyed by a float at +4 — a render-list
sort/process pass (painter-style), i.e. part of the draw
pipeline; the 0x1F5E38 group is its persistent state
(bounds/keys), not the camera. Three more stores come from
an uncarved fragment at 0xFF31C-0xFF334. The camera fields
ff9d8 reads (0x1F5E60-0x1F5E98) have NO direct r2-relative
writers — they are written through pointers (consistent
with struct-field initialization at arena setup), so the
initializer is one of the arena-enter op handlers or
FUN_00100218 (still unread). Camera constants remain
unisolated; the milestone-14 estimates stay labelled.

## Antigravity as a second analyst (Jerry's suggestion)

Route established (2026-11-06): Antigravity 2.19.1 (brew
cask) ships ~/.gemini/antigravity/bin/agentapi —
new-conversation --model=pro / send-message /
get-conversation-metadata — riding Jerry's Google Pro.
Blocker: agentapi needs a RUNNING language server
(ANTIGRAVITY_LS_ADDRESS); the LS cannot start standalone
(it requires an IDE-fed metadata handshake on stdin), and
launching the IDE over SSH stalled before its LS came up
(no renderer/LS processes, no new main.log entries — OS
keyring prompt is the usual suspect). The IDE instance
launched for the test was stopped; box left as found.
Unblock: Jerry opens Antigravity at the desktop once; its
main.log banner then prints the LS address and the staged
prompt (/tmp/ag_prompt.txt: CAMERA / TRANSFORM / HUD with
citation rules, read-only scope) can be dispatched.
His agent permission preset is TURBO with eager
auto-execution — keep analysis prompts read-only and
verify repo state after any agent run.
