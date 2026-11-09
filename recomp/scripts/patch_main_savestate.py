#!/usr/bin/env python3
"""patch_main_savestate.py -- SAVESTATE PATCH (title glue).

Adds the experimental save/load (savestate) module to the workspace
glue (~/tools/cc-recomp/main.cpp):

  1. Includes: <time.h> (save-file timestamps), <direct.h> on Windows.
  2. The module itself, inserted just before comet_crash_camera_tick:
     sparse guest-RAM snapshot save/restore, the strong definitions of
     the Vulkan backend's weak hooks ps3_host_savestate_save/_load
     (F5/F9), and a debug trigger file ("save"/"load" in
     saves/trigger) reaching the same code path for scripted drives.
  3. A trigger poll call inside comet_crash_camera_tick, next to the
     cheats tick.

File format, gates and known limitations: notes/051-save-states.md.
Every anchor is asserted; each edit is independently idempotent.

Usage: patch_main_savestate.py [workspace-dir]  (default ~/tools/cc-recomp)
"""
import sys
from pathlib import Path

MARKER = "SAVESTATE PATCH"

MODULE = r'''
/* ---------------------------------------------------------------------------
 * SAVE STATES (experimental) -- SAVESTATE PATCH
 *
 * F5 saves the whole guest-RAM state of the current match to a local
 * file, F9 loads it back. The Vulkan backend's event pump calls the
 * weak hooks ps3_host_savestate_save/_load (defined here); a debug
 * trigger file in the save directory (first word "save" or "load")
 * reaches the same code path for scripted drives.
 *
 * What is captured: guest RAM only -- every nonzero 4 KiB page in
 * guest [0, 0x40000000) that the OS has resident, sparse-encoded.
 * What is NOT captured: PPU thread contexts (guest registers and
 * stack pointers live host-side), SPU local-store state (the jobs
 * re-read entity data from guest RAM each kick, which is what makes
 * this work as well as it does), and host bookkeeping such as
 * sys_memory's allocation table -- the title's boot allocation
 * layout is deterministic enough in practice that the restored
 * in-RAM allocator state matches. v1, on muse/save-states, expected
 * to have sharp edges; notes/051-save-states.md keeps the list.
 *
 * Restore semantics: the span the save covered is zeroed, then the
 * saved pages are written back -- progress after the save point is
 * rolled back exactly, including campaign state: the mission id and
 * mode word are part of the snapshot (they live in the object at
 * 0x30E600), so a win after a load is a win on the level the save
 * was taken in, credited by the game's own progression code.
 * After the copy, host caches derived from guest memory are dropped
 * through the backend's ps3_rsx_flush_guest_caches (declared weak:
 * absent on other backends, where the engine's content hashes still
 * catch texture changes on their own). Audio is deliberately not
 * reset in v1; the mixer re-reads its guest ring state every block.
 * -----------------------------------------------------------------------*/
extern "C" void ps3_rsx_flush_guest_caches(void) __attribute__((weak));

namespace {

const uint32_t kSsObjBase   = 0x0030E600;
const uint32_t kSsStateAddr = kSsObjBase + 0x005520C0;
const uint32_t kSsModeAddr  = kSsObjBase + 0x0055209C;
const uint32_t kSsMisnAddr  = kSsObjBase + 0x00551EBC;
const uint32_t kSsPage      = 4096;
const uint32_t kSsScanSpan  = 0x20000000;   /* guest scan/restore ceiling:
 * 512 MB -- the span the Phase-1 prototype validated
 * (notes 051). The 0x20000000..0x21011000 band holds the
 * glue's own cheats-label page and runtime pages a
 * snapshot must not transplant across processes. */
const uint32_t kSsMinSpan   = 0x14000000;   /* the known live span */

struct SsHeader {
    char     magic[8];        /* "CCSV0001" */
    uint32_t version;         /* 1 */
    uint32_t header_bytes;    /* sizeof(SsHeader) */
    uint32_t mission_id;
    uint32_t mode;
    uint32_t state;
    uint32_t reserved;
    uint64_t timestamp_unix;
    uint64_t page_count;
    uint64_t restore_span;
    uint64_t reserved2;
};

bool ss_page_nonzero(const uint8_t* p)
{
    const uint64_t* q = (const uint64_t*)p;
    uint64_t acc = 0;
    for (uint32_t i = 0; i < kSsPage / 8; i++) acc |= q[i];
    return acc != 0;
}

void ss_save_path(char* out, size_t cap, const char* leaf)
{
    const char* dir = getenv("CC_SAVESTATE_DIR");
    if (dir && *dir) { snprintf(out, cap, "%s/%s", dir, leaf); return; }
    const char* home = getenv("HOME");
    if (home && *home) snprintf(out, cap, "%s/tools/cc-recomp/saves/%s", home, leaf);
    else snprintf(out, cap, "saves/%s", leaf);
}

void ss_ensure_dir(void)
{
    char path[1024];
    ss_save_path(path, sizeof path, "slot1.ccsave");
    char* slash = strrchr(path, '/');
    if (!slash) return;
    *slash = 0;
    for (char* p = path + 1; ; p++) {
        if (*p == '/' || *p == 0) {
            char save = *p;
            *p = 0;
#ifdef _WIN32
            _mkdir(path);
#else
            mkdir(path, 0755);
#endif
            *p = save;
            if (!save) break;
        }
    }
}

int ss_do_save(void)
{
    if (!vm_base) return 0;
    const uint32_t state = comet_rd32(kSsStateAddr);
    /* SAVESTATE PATCH (gate v2): pause-only. State-13 captures race
     * live guest allocations and tear (a state-13 save crashed a
     * fresh process on load with a guest heap assertion, drive 7 in
     * notes 051); the pause is quiescent and its captures restored
     * byte-exact in every test. */
    if (state != 14) {
        fprintf(stderr, "[SAVE] refused: guest state %u (need 14 pause)\n",
                state);
        return 0;
    }
    ss_ensure_dir();
    char path[1024];
    ss_save_path(path, sizeof path, "slot1.ccsave");
    FILE* f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[SAVE] refused: cannot open %s\n", path);
        return 0;
    }

    const ULONGLONG t0 = GetTickCount64();
    SsHeader h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, "CCSV0001", 8);
    h.version = 1;
    h.header_bytes = sizeof h;
    h.mission_id = comet_rd32(kSsMisnAddr);
    h.mode = comet_rd32(kSsModeAddr);
    h.state = state;
    h.timestamp_unix = (uint64_t)time(NULL);
    fwrite(&h, sizeof h, 1, f);          /* counts patched in below */

    uint64_t pages = 0;
    uint32_t highest_end = kSsMinSpan;
#ifndef _WIN32
    static unsigned char s_mincore_vec[1 << 16];
    const uint32_t chunk_pages = 1 << 16;
    const uint32_t total_pages = kSsScanSpan / kSsPage;
    for (uint32_t chunk = 0; chunk < total_pages; chunk += chunk_pages) {
        uint32_t n = chunk_pages;
        if (chunk + n > total_pages) n = total_pages - chunk;
        if (mincore(vm_base + (size_t)chunk * kSsPage, (size_t)n * kSsPage,
                    s_mincore_vec) != 0)
            memset(s_mincore_vec, 1, n);   /* mincore failed: scan anyway */
        for (uint32_t i = 0; i < n; i++) {
            if (!(s_mincore_vec[i] & 1)) continue;
            const uint32_t addr = (chunk + i) * kSsPage;
            const uint8_t* page = vm_base + addr;
            if (!ss_page_nonzero(page)) continue;
            fwrite(&addr, sizeof addr, 1, f);
            fwrite(page, 1, kSsPage, f);
            pages++;
            if (addr + kSsPage > highest_end) highest_end = addr + kSsPage;
        }
    }
#else
    for (uint32_t addr = 0; addr < kSsScanSpan; addr += kSsPage) {
        const uint8_t* page = vm_base + addr;
        if (!ss_page_nonzero(page)) continue;
        fwrite(&addr, sizeof addr, 1, f);
        fwrite(page, 1, kSsPage, f);
        pages++;
        if (addr + kSsPage > highest_end) highest_end = addr + kSsPage;
    }
#endif
    h.page_count = pages;
    h.restore_span = highest_end;
    fseek(f, 0, SEEK_SET);
    fwrite(&h, sizeof h, 1, f);
    fclose(f);
    const ULONGLONG ms = GetTickCount64() - t0;
    fprintf(stderr, "[SAVE] %s: %llu pages, span 0x%X, mission %u mode %u "
            "state %u, %llu ms\n", path, (unsigned long long)pages,
            highest_end, h.mission_id, h.mode, state,
            (unsigned long long)ms);
    return 1;
}

int ss_do_load(void)
{
    if (!vm_base) return 0;
    const uint32_t state = comet_rd32(kSsStateAddr);
    /* SAVESTATE PATCH (gate): the front-end idles in state 11 in this
     * title (observed live at the title screen), not in states <=6 as
     * the dispatch table alone suggested. Loading is allowed from the
     * front-end (11), gameplay (13) and pause (14); state 12
     * (pre-flight / battle-end flow) and 7-10 stay refused. */
    if (!(state <= 6 || state == 11 || state == 13 || state == 14)) {
        fprintf(stderr, "[LOAD] refused: guest state %u (need front-end "
                "11, gameplay 13 or pause 14)\n", state);
        return 0;
    }
    char path[1024];
    ss_save_path(path, sizeof path, "slot1.ccsave");
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[LOAD] refused: no save file at %s\n", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    const long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    SsHeader h;
    bool ok = fsize >= (long)sizeof h && fread(&h, sizeof h, 1, f) == 1 &&
              memcmp(h.magic, "CCSV0001", 8) == 0 && h.version == 1 &&
              h.header_bytes == sizeof h &&
              h.restore_span >= kSsPage && h.restore_span <= kSsScanSpan &&
              h.page_count <= kSsScanSpan / kSsPage &&
              fsize == (long)(sizeof h +
                               h.page_count * (sizeof(uint32_t) + kSsPage));
    uint8_t* blob = NULL;
    if (ok) {
        const size_t blob_bytes =
            (size_t)h.page_count * (sizeof(uint32_t) + kSsPage);
        blob = (uint8_t*)malloc(blob_bytes ? blob_bytes : 1);
        ok = blob && fread(blob, 1, blob_bytes, f) == blob_bytes;
        if (ok) {
            /* Validate every record before touching guest memory. */
            for (uint64_t i = 0; i < h.page_count; i++) {
                uint32_t addr;
                memcpy(&addr, blob + i * (sizeof(uint32_t) + kSsPage),
                       sizeof addr);
                if ((addr & (kSsPage - 1)) || addr + kSsPage > h.restore_span) {
                    ok = false;
                    break;
                }
            }
        }
    }
    fclose(f);
    if (!ok) {
        free(blob);
        fprintf(stderr, "[LOAD] refused: %s is corrupt or truncated; "
                "guest memory untouched\n", path);
        return 0;
    }
    const ULONGLONG t0 = GetTickCount64();
    if (ps3_rsx_flush_guest_caches) ps3_rsx_flush_guest_caches();
    /* SAVESTATE PATCH (reconcile): zero only pages that are nonzero
     * in the CURRENT span but absent from the save -- the Phase-1
     * prototype's reconciliation (notes 051). A blanket memset of
     * the span also wipes high pages the recorded span happens to
     * cover but whose current contents the snapshot never owned
     * (the span is set by the highest saved page, e.g. the cheats
     * label page at 0x21010000), and cross-instance loads then
     * deterministically collapsed the target match (drives 8/9 in
     * notes 051). */
    {
        const uint64_t npages = h.restore_span / kSsPage;
        uint8_t* in_save =
            (uint8_t*)calloc((size_t)((npages + 7) / 8), 1);
        if (in_save) {
            for (uint64_t i = 0; i < h.page_count; i++) {
                uint32_t addr;
                memcpy(&addr, blob + i * (sizeof(uint32_t) + kSsPage),
                       sizeof addr);
                in_save[(addr / kSsPage) >> 3] |=
                    (uint8_t)(1u << ((addr / kSsPage) & 7));
            }
            for (uint64_t pg = 0; pg < npages; pg++) {
                if (in_save[pg >> 3] & (1u << (pg & 7))) continue;
                uint8_t* p = vm_base + (size_t)pg * kSsPage;
                if (ss_page_nonzero(p)) memset(p, 0, kSsPage);
            }
            free(in_save);
        } else {
            memset(vm_base, 0, (size_t)h.restore_span);
        }
    }
    for (uint64_t i = 0; i < h.page_count; i++) {
        const uint8_t* rec = blob + i * (sizeof(uint32_t) + kSsPage);
        uint32_t addr;
        memcpy(&addr, rec, sizeof addr);
        memcpy(vm_base + addr, rec + sizeof addr, kSsPage);
    }
    free(blob);
    const ULONGLONG ms = GetTickCount64() - t0;
    fprintf(stderr, "[LOAD] %s: %llu pages, span 0x%llX, mission %u mode %u "
            "(saved in state %u) restored over state %u, %llu ms\n",
            path, (unsigned long long)h.page_count,
            (unsigned long long)h.restore_span, h.mission_id, h.mode,
            h.state, state, (unsigned long long)ms);
    return 1;
}

/* Debug trigger for scripted drives: a file named "trigger" in the
 * save directory whose first word is "save" or "load" runs the same
 * code path as F5/F9 and is consumed. Polled from the camera tick,
 * like the cheats control file. */
void comet_savestate_poll_trigger(void)
{
    char path[1024];
    ss_save_path(path, sizeof path, "trigger");
    struct stat st;
    if (stat(path, &st) != 0) return;
    FILE* f = fopen(path, "r");
    if (!f) return;
    char word[16] = { 0 };
    if (fscanf(f, "%15s", word) != 1) word[0] = 0;
    fclose(f);
    remove(path);
    if (strcmp(word, "save") == 0) ss_do_save();
    else if (strcmp(word, "load") == 0) ss_do_load();
}

} /* namespace */

extern "C" void ps3_host_savestate_save(void) { ss_do_save(); }
extern "C" void ps3_host_savestate_load(void) { ss_do_load(); }

'''


def replace_once(text: str, anchor: str, replacement: str, what: str) -> str:
    assert anchor in text, f"anchor NOT FOUND for {what}:\n{anchor}"
    assert text.count(anchor) == 1, f"anchor not unique for {what}"
    return text.replace(anchor, replacement)


def main() -> int:
    ws = Path(sys.argv[1] if len(sys.argv) > 1 else "~/tools/cc-recomp").expanduser()
    main_cpp = ws / "main.cpp"
    t = main_cpp.read_text()

    # ---- edit 1: includes ----
    if "#include <time.h>" not in t:
        anchor = "#include <sys/stat.h>"
        repl = anchor + """
#include <time.h>
#ifdef _WIN32
#include <direct.h>   /* _mkdir, for the savestate module below */
#endif"""
        t = replace_once(t, anchor, repl, "includes")
        print("edit 1 (includes): in")
    else:
        print("edit 1 (includes): already in")

    # ---- edit 2: the module, before the camera tick ----
    if "comet_savestate_poll_trigger" not in t:
        anchor = "static void comet_crash_camera_tick(void)"
        repl = MODULE.lstrip("\n") + "\n" + anchor
        t = replace_once(t, anchor, repl, "module insertion")
        print("edit 2 (module): in")
    else:
        print("edit 2 (module): already in")

    # ---- edit 3: trigger poll in the camera tick ----
    if "comet_savestate_poll_trigger();" not in t:
        anchor = "    comet_cheats_tick();\n"
        repl = anchor + "    comet_savestate_poll_trigger();\n"
        t = replace_once(t, anchor, repl, "trigger poll call")
        print("edit 3 (poll call): in")
    else:
        print("edit 3 (poll call): already in")

    assert MARKER in t
    main_cpp.write_text(t)
    print(f"{main_cpp}: SAVESTATE PATCH applied")
    return 0


if __name__ == "__main__":
    sys.exit(main())
