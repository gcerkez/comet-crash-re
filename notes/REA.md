# REA (Reverse Engineer Anything) — setup state and findings

Date: 2026-11-05. Machine: JerryDesktop.

## What is installed

- `rea-agents` **6.7.0** (npm, global) — the version REA's own skill doc pins.
  (The repo installer pulls `@latest`, which was 7.x and crashed on this
  machine's Node 20; 6.7.0 is the pinned, working line. See below re Node.)
- Registered with **Google Antigravity**: `rea-agents setup --client
  antigravity --yes`; the bundled skill was deployed to
  `~/.gemini/config/skills/`. Restart Antigravity to pick it up.
- `rea-agents doctor --client antigravity`: **healthy** (node ✓,
  registration ✓, ghidra ✓).
- Engines/prerequisites (installed under `~/tools/`, activated by
  `source ~/tools/env.sh`):
  - Temurin **JDK 21.0.13** (`~/tools/jdk-21.0.13+9`)
  - **Ghidra 12.2** (`~/tools/ghidra_12.2_PUBLIC`) — REA detects it fine;
    note REA's provider is *verified* against Ghidra 12.1.x and warns about
    other lines, but the 12.2 install is accepted by doctor.
  - **Node 24.22.2** (`~/tools/node24`) — REA's MCP server requires Node
    ≥22.19; the machine's stock Node 20.18 is left untouched and the new one
    is only on PATH via `env.sh`.
  - JADX headless MCP jar (for Android targets) downloaded by the repo
    installer to `~/.antigravity-tools/jadx/` — unused for this project.

## The hard finding: REA cannot open this target

```
$ rea-agents analyze EBOOT.ELF --format json
{"ok":false,"error":{"code":"target_unavailable",
 "category":"unsupported_target",
 "message":"Cannot open artifact: unsupported ELF architecture.",
 ... "constraint":"file_format_rejected"}}
```

Same result with `--provider ghidra` forced: the rejection happens in REA's
artifact-intake layer in ~200 ms, before any engine sees the file. REA
6.7.0's supported architecture set (per its CLI) is x86/x86_64/ARM/ARM64-class
targets (plus 6502/m68k for raw binaries) — a **big-endian PowerPC64 Cell
LV2 ELF is outside it**. This is a coverage limitation, not a setup problem:
doctor is fully healthy.

## Consequence for this project

The analysis corpus in this repo was produced by driving REA's own engine —
**Ghidra 12.2 headless** — directly (`analyzeHeadless`, language
`PowerPC:BE:64:default`, which covers the Cell PPU), with the export script
in `scripts/`. If a future REA release adds PPC64 support, the same ELF can
be re-run through `rea-agents analyze` and the outputs compared; the REA
attempt records are kept in `analysis/rea-overview.json` and
`analysis/rea-overview-ghidra.json` for that baseline.
