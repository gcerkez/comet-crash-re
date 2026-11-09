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
     v2: the restore's page copy runs stop-the-world -- after total
     validation and after the pad/codec host resyncs have fired,
     the glue forks a helper, SIGSTOPs the whole process, and the
     helper writes the snapshot through /proc/self/mem while every
     game thread is frozen, then SIGCONTs the parent (notes 051).
     The round 1/2 RSX guest-cache flush is deliberately not
     fired: it wedges cross-process loads (notes 051, v2).
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
 * Restore semantics: pages the save owns are written back, and
 * pages that are nonzero in the current span but absent from the
 * save are zero-filled first (the Phase-1 reconciliation) --
 * progress after the save point is rolled back exactly,
 * including campaign state: the mission id and mode word are
 * part of the snapshot (they live in the object at 0x30E600),
 * so a win after a load is a win on the level the save was
 * taken in, credited by the game's own progression code.
 *
 * Restore atomicity (v2): the page copy runs stop-the-world.
 * After the file has been totally validated and the host resyncs
 * below have fired, the glue forks a helper and SIGSTOPs the
 * whole process; the helper writes the snapshot through
 * /proc/self/mem while every game thread is frozen, then
 * SIGCONTs the parent. Rounds 1-2 copied ~115 MB with every
 * thread live -- the RSX thread draining a FIFO out of the
 * memory being swapped, the audio task writing its rings,
 * SPURS bookkeeping advancing -- and the restored match wedged
 * or tore itself down every time; Phase 1's external
 * prototype, which SIGSTOPped around both capture and restore,
 * resumed. Two of the resyncs fire BEFORE the stop -- the
 * pad backend's admission resync (ps3_pad_resync_savestate)
 * and the host MP3 codec's stream-table reset
 * (ps3_spu_mp3_reset_streams), both declared weak -- because
 * they touch host-side bookkeeping only: at the pause
 * boundary the guest's match state is provably static (Phase
 * 1 measured identical samples 6 s apart, entity banks
 * included), so re-syncing them while the world still runs
 * cannot race guest match state, and it keeps the stopped
 * window down to exactly the page copy -- the only part that
 * must be atomic. The round 1/2 RSX guest-cache flush
 * (ps3_rsx_flush_guest_caches) is deliberately NOT fired:
 * with it in the path, a cross-process load restored
 * byte-exact but the guest never polled the pad again,
 * while the identical load without it resumed and played
 * (notes 051, v2 addendum, drives 14-17). The flush is the
 * only one of the three whose effects reach guest-visible
 * machinery -- the RSX FIFO and the surface bookkeeping
 * span the guest/host boundary, and the snapshot already
 * carries the guest half of that state. The mixer itself
 * is not reset either; it re-reads its guest ring state
 * every block.
 * -----------------------------------------------------------------------*/
extern "C" void ps3_pad_resync_savestate(void) __attribute__((weak));
extern "C" void ps3_spu_mp3_reset_streams(void) __attribute__((weak));

#ifndef _WIN32
/* SAVESTATE PATCH (v2): POSIX surface for the stop-the-world
 * restore (fork, SIGSTOP/SIGCONT, /proc mem, shared status page).
 * Inside the forked child only raw-syscall wrappers run: no
 * stdio, no malloc -- everything it touches is allocated before
 * the fork. */
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#endif

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

#ifndef _WIN32
/* SAVESTATE PATCH (v2): the stop-the-world restore machinery.
 *
 * The parent opens /proc/self/mem and maps one shared status
 * page BEFORE forking. The child inherits both (the memfd stays
 * bound to this process's address space; the status page is
 * MAP_SHARED, so the parent can read the outcome the moment it
 * resumes). The child waits until /proc/<ppid>/stat reports the
 * parent stopped, so no page is written before every parent
 * thread is frozen; then it runs the same restore the live copy
 * did -- zero-fill reconciliation first, then every saved page
 * -- through the memfd, records progress in the status page,
 * SIGCONTs the parent, and _exits. Exit codes: 0 = restored;
 * 2 = failed before any write (parent memory untouched);
 * 3 = failed mid-write (parent memory may be mixed). The
 * status page is authoritative; the exit code is the
 * cross-check for when waitpid works. */
struct SsStopStatus {
    volatile uint32_t phase;         /* 1 waiting, 2 zeroing, 3 writing, 4 done */
    volatile uint32_t pages_written;
    volatile uint32_t pages_zeroed;
    volatile int32_t  error;         /* errno of the failing op, 0 = none */
};

uint8_t s_ss_zero_page[kSsPage];   /* BSS is zero: a zero page */
uint8_t s_ss_scratch[kSsPage];     /* child-only read scratch */

int ss_io_all(int fd, uint8_t* buf, off_t off, bool write_op)
{
    size_t done = 0;
    while (done < kSsPage) {
        ssize_t n = write_op
            ? pwrite(fd, buf + done, kSsPage - done, off + (off_t)done)
            : pread(fd, buf + done, kSsPage - done, off + (off_t)done);
        if (n < 0) { if (errno == EINTR) continue; return errno; }
        if (n == 0) return EIO;
        done += (size_t)n;
    }
    return 0;
}

/* Poll /proc/<parent>/stat until the parent's state reads T
 * (stopped). Bounded (~3 s); returns 0 or errno. The path is
 * built by hand: no stdio in the child. */
int ss_wait_stopped(pid_t parent)
{
    char procpath[64];
    {
        char rev[16];
        int ri = 0;
        unsigned v = (unsigned)parent;
        if (!v) rev[ri++] = '0';
        while (v) { rev[ri++] = (char)('0' + (v % 10)); v /= 10; }
        const char* pre = "/proc/";
        const char* suf = "/stat";
        int o = 0;
        while (*pre) procpath[o++] = *pre++;
        while (ri > 0) procpath[o++] = rev[--ri];
        while (*suf) procpath[o++] = *suf++;
        procpath[o] = 0;
    }
    for (int i = 0; i < 3000; i++) {
        int fd = open(procpath, O_RDONLY);
        if (fd < 0) return errno;            /* parent vanished */
        char buf[512];
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n > 0) {
            buf[n] = 0;
            const char* rp = strrchr(buf, ')');
            if (rp && rp[1] == ' ' && (rp[2] == 'T' || rp[2] == 't'))
                return 0;
        }
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000;                /* 1 ms */
        nanosleep(&ts, NULL);
    }
    return ETIMEDOUT;
}

int ss_child_restore(int memfd, pid_t parent, const uint8_t* blob,
                     const SsHeader* h, const uint8_t* in_save,
                     SsStopStatus* status)
{
    int wrote = 0;
    if (status) status->phase = 1;
    int err = ss_wait_stopped(parent);
    if (!err) {
        const uint64_t span_pages = h->restore_span / kSsPage;
        if (status) status->phase = 2;
        for (uint64_t pg = 0; pg < span_pages && !err; pg++) {
            if (in_save[pg >> 3] & (1u << (pg & 7))) continue;
            off_t off = (off_t)((uintptr_t)vm_base + (size_t)pg * kSsPage);
            err = ss_io_all(memfd, s_ss_scratch, off, false);
            if (!err && ss_page_nonzero(s_ss_scratch)) {
                err = ss_io_all(memfd, s_ss_zero_page, off, true);
                if (!err) {
                    wrote = 1;
                    if (status) status->pages_zeroed = status->pages_zeroed + 1;
                }
            }
        }
        if (status) status->phase = 3;
        for (uint64_t i = 0; i < h->page_count && !err; i++) {
            const uint8_t* rec = blob + i * (sizeof(uint32_t) + kSsPage);
            uint32_t addr;
            memcpy(&addr, rec, sizeof addr);
            off_t off = (off_t)((uintptr_t)vm_base + addr);
            err = ss_io_all(memfd, (uint8_t*)rec + sizeof(uint32_t),
                            off, true);
            if (!err) {
                wrote = 1;
                if (status) status->pages_written = (uint32_t)(i + 1);
            }
        }
    }
    if (status) { status->error = err; status->phase = 4; }
    kill(parent, SIGCONT);      /* on EVERY path: never leave it stopped */
    return err ? (wrote ? 3 : 2) : 0;
}

/* Returns 0 = restored, 1 = refused/failed before any write
 * (guest memory untouched, session unharmed), 2 = failed
 * mid-write (guest memory may be mixed; session corrupt).
 * Logs its own [LOAD] outcome line. */
int ss_restore_stop_the_world(const char* path, const uint8_t* blob,
                              const SsHeader* h,
                              const uint8_t* in_save, ULONGLONG t0)
{
    int memfd = open("/proc/self/mem", O_RDWR);
    if (memfd < 0) {
        fprintf(stderr, "[LOAD] refused: cannot open /proc/self/mem "
                "(%s); guest memory untouched\n", strerror(errno));
        return 1;
    }
    SsStopStatus* status = (SsStopStatus*)mmap(NULL, 4096,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (status == MAP_FAILED) {
        fprintf(stderr, "[LOAD] refused: cannot map the restore status "
                "page (%s); guest memory untouched\n", strerror(errno));
        close(memfd);
        return 1;
    }
    memset((void*)status, 0, sizeof *status);
    const pid_t self = getpid();
    const ULONGLONG tstop = GetTickCount64();
    const pid_t pid = fork();
    if (pid == 0)
        _exit(ss_child_restore(memfd, self, blob, h, in_save, status));
    if (pid < 0) {
        fprintf(stderr, "[LOAD] refused: fork failed (%s); guest memory "
                "untouched\n", strerror(errno));
        munmap((void*)status, 4096);
        close(memfd);
        return 1;
    }
    raise(SIGSTOP);
    /* Frozen here until the child finishes the copy and SIGCONTs
     * us. The child published the outcome in the status page
     * before the SIGCONT, so it is already readable. */
    int wst = 0;
    const pid_t w = waitpid(pid, &wst, 0);
    const ULONGLONG stop_ms = GetTickCount64() - tstop;
    const int child_err = status->error;
    const uint32_t wrote_pages = status->pages_written;
    const uint32_t zeroed_pages = status->pages_zeroed;
    const int child_rc = (w > 0 && WIFEXITED(wst)) ? WEXITSTATUS(wst) : -1;
    munmap((void*)status, 4096);
    close(memfd);
    if (!child_err && wrote_pages == h->page_count) {
        const ULONGLONG ms = GetTickCount64() - t0;
        fprintf(stderr, "[LOAD] %s: %llu pages (+%u zero-filled), span "
                "0x%llX, mission %u mode %u (saved in state %u) restored "
                "over state 14, stop-the-world %llu ms, total %llu ms\n",
                path, (unsigned long long)h->page_count, zeroed_pages,
                (unsigned long long)h->restore_span, h->mission_id,
                h->mode, h->state, (unsigned long long)stop_ms,
                (unsigned long long)ms);
        return 0;
    }
    if (!wrote_pages && !zeroed_pages) {
        fprintf(stderr, "[LOAD] FAILED before any page was written "
                "(%s, child rc %d); guest memory untouched, session "
                "unharmed\n",
                child_err ? strerror(child_err) : "no errno", child_rc);
        return 1;
    }
    fprintf(stderr, "[LOAD] FAILED mid-restore after %u pages (%s, "
            "child rc %d); guest memory may be mixed -- consider this "
            "session corrupt and restart the game\n",
            wrote_pages + zeroed_pages,
            child_err ? strerror(child_err) : "no errno", child_rc);
    return 2;
}
#endif /* !_WIN32 */

int ss_do_load(void)
{
    if (!vm_base) return 0;
    const uint32_t state = comet_rd32(kSsStateAddr);
    /* SAVESTATE PATCH (gate v3): pause only. Phase 1 (notes 051)
     * proved the sound restore target is a pause in the same
     * mission the process entered itself: a title-screen load
     * transplants RAM faithfully but the title scene's SPURS
     * chain never simulates the restored match, and in-place /
     * gameplay loads desync the host runtime. State 14 is the
     * only state whose host-side phase agrees with a pause
     * snapshot, so it is the only state load is offered in. */
    if (state != 14) {
        fprintf(stderr, "[LOAD] refused: guest state %u (need 14 pause)\n",
                state);
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
    /* SAVESTATE PATCH (v2 resync): the pad backend's admission
     * resync and the MP3 codec's stream-table reset fire BEFORE
     * the stop-the-world copy (rationale in the banner). The RSX
     * guest-cache flush is deliberately not fired -- see the
     * banner for the drive evidence. */
    if (ps3_pad_resync_savestate) ps3_pad_resync_savestate();
    if (ps3_spu_mp3_reset_streams) ps3_spu_mp3_reset_streams();
    /* Bitmap of the pages the save owns; the restore zero-fills
     * pages that are nonzero in the current span but absent from
     * the save (the Phase-1 reconciliation). */
    const uint64_t span_pages = h.restore_span / kSsPage;
    uint8_t* in_save = (uint8_t*)calloc((size_t)((span_pages + 7) / 8), 1);
    if (!in_save) {
        free(blob);
        fprintf(stderr, "[LOAD] refused: out of memory building the "
                "restore map; guest memory untouched\n");
        return 0;
    }
    for (uint64_t i = 0; i < h.page_count; i++) {
        uint32_t addr;
        memcpy(&addr, blob + i * (sizeof(uint32_t) + kSsPage), sizeof addr);
        in_save[(addr / kSsPage) >> 3] |=
            (uint8_t)(1u << ((addr / kSsPage) & 7));
    }
#ifndef _WIN32
    const int rc = ss_restore_stop_the_world(path, blob, &h, in_save, t0);
    free(in_save);
    free(blob);
    return rc == 0 ? 1 : 0;
#else
    /* Windows has no stop-the-world equivalent in v2; keep the
     * round-1 live copy there (reconcile, then copy back). */
    for (uint64_t pg = 0; pg < span_pages; pg++) {
        if (in_save[pg >> 3] & (1u << (pg & 7))) continue;
        uint8_t* p = vm_base + (size_t)pg * kSsPage;
        if (ss_page_nonzero(p)) memset(p, 0, kSsPage);
    }
    for (uint64_t i = 0; i < h.page_count; i++) {
        const uint8_t* rec = blob + i * (sizeof(uint32_t) + kSsPage);
        uint32_t addr;
        memcpy(&addr, rec, sizeof addr);
        memcpy(vm_base + addr, rec + sizeof addr, kSsPage);
    }
    free(in_save);
    free(blob);
    const ULONGLONG ms = GetTickCount64() - t0;
    fprintf(stderr, "[LOAD] %s: %llu pages, span 0x%llX, mission %u mode %u "
            "(saved in state %u) restored over state %u, %llu ms\n",
            path, (unsigned long long)h.page_count,
            (unsigned long long)h.restore_span, h.mission_id, h.mode,
            h.state, state, (unsigned long long)ms);
    return 1;
#endif
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
    if "int ss_do_load(void)" not in t:
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
