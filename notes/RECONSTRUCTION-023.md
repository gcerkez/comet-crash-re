# Reconstruction notes 023 — arenaGraphics module mapped;
the PSGL layer is now linked and readable

Date: 2026-11-06. Method: the arenaGraphics assertion-string
slots (0x1F5E70-0x1F5EA0) were re-swept against corpus v3 —
their users finally resolve to named functions.

## The arenaGraphics module

- **arena_createRenderTargets** (FUN_000fcebc, 2,976 B):
  builds the frame's render targets — color/depth textures
  and FBO attachments, filter/wrap parameters, a quality
  scale (1x or 2x from a settings field), PSGL texture
  formats (0x6007/0x6018-0x6033). All seven arenaGraphics
  assertion strings are referenced here.
- **arena_postChain** (FUN_000fdf00, 1,592 B): orchestrates
  the fullscreen post passes over those targets — viewport
  per pass, ping-pong buffers (toggle at +0x551e60), a
  128x64 downsample pass (bloom), final composite to the
  default framebuffer. Sets a symmetric glOrtho for the
  post passes from two runtime floats (slots -0x6988 /
  -0x69A0). The scene itself is drawn before this chain —
  FUN_0012f66c, called first inside the chain's scene
  branch, is the next link to read.
- FUN_000ff9d8: per-entity projection math (notes 021).
- FUN_000ff6d0: flip-trigger caller (notes 013/018).

## PSGL linking (renames applied, corpus re-exported)

Call signatures + GL enum arguments identify the statically
linked PSGL entry points; they are renamed in Ghidra and
in scripts/renames.csv, and the re-exported corpus now
shows 255 psgl_* call sites:

psgl_bindTexture (0x1978C), psgl_texImage2D (0x198B8 —
exact 9-arg glTexImage2D signature), psgl_texParameterf
(0x1A814), psgl_texParameteri (0x1A35C), psgl_bindFramebuffer
(0x25408), psgl_framebufferTexture2D (0x25700),
psgl_viewport (0x20218), psgl_matrixMode (0x175B8),
psgl_loadIdentity (0x17F10), psgl_ortho (0x175C8),
psgl_drawArrays (0x18380), psgl_enable (0x11320).

Enum key used: 0xDE1 TEXTURE_2D, 0x2800-0x2803 texture
params, 0x2600/0x2601 NEAREST/LINEAR, 0x1908 RGBA, 0x1902
DEPTH_COMPONENT, 0x8D40 FRAMEBUFFER, 0x8CE0
COLOR_ATTACHMENT0, 0x1700/0x1701 MODELVIEW/PROJECTION.

## The camera, honestly

The scene projection parameters are RUNTIME state: the
values feeding psgl_ortho and the entity projection live
in BSS floats (pointers in the 0x1F5Exx slots, e.g.
0x8B3358) written during initialization — they are not
constants in the file, which is why three passes of
constant-hunting found pointers instead of numbers. The
remaining extraction path is dynamic in nature: read the
scene-draw function (FUN_0012f66c) and the model draw path
to find which matrix the game builds, and which init code
writes those BSS floats. The port's camera/HUD therefore
remain the labelled milestone-14 estimates for now.

## Antigravity second analyst — one action from ready

The LS is up and agentapi authenticates (CSRF via
ANTIGRAVITY_CSRF_TOKEN from the LS cmdline; address
127.0.0.1:44103 today). new-conversation still fails:
"project_id is required when providing project_env_config"
— the LS binds conversations to the IDE's open workspace
project, and no folder is open yet. Opening
~/comet-crash-re in Antigravity should unblock it; the
staged read-only prompt (/tmp/ag_prompt.txt) is ready to
fire the moment it does.
