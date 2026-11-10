/*
 * ps3recomp game project -- entry point template.
 *
 * This is the runner. It boots a lifted title through the toolkit's PPU
 * scaffold: allocate the flat guest VM, load the PPU ELF named on the command
 * line, register the lifted function table and the HLE NID handlers, start a
 * frame clock, and dispatch the entry OPD.
 *
 * The scaffold does the work; nothing below reimplements it. ppu_load_elf,
 * ppu_recomp_register, ppu_hle_init, ppu_sysprx_register, ppu_fs_register,
 * lv2_init_syscalls and ppu_run all come from the toolkit, compiled into this
 * project from PS3RECOMP_DIR (see CMakeLists.txt). What is left here is the
 * part a port owns: which backend to present through, how the frame clock is
 * paced, and whatever diagnostics the title turns out to need.
 *
 * runtime/ppu/tests/boot_main.cpp in the toolkit is the same boot in its
 * fully-instrumented form -- crash filter, hang watchdog, guest-PC sampling
 * profiler. Read it when a boot goes wrong; copy from it what the title
 * needs. It is deliberately not what a fresh project starts with.
 *
 * The one part that IS duplicated from it is the frame clock below, because a
 * port is expected to change its pacing and a shared one would be the wrong
 * shape for that. It was copied faithfully, comments and all. If a pacing bug
 * is fixed in one of the two, look at the other.
 *
 * This builds on Windows, macOS and Linux. CI builds and runs it against the
 * boot smoke title on the last two, and compile-checks it against clang-cl on
 * the first (tools/check_ppu_scaffold.py).
 */

/* The lifter's generated header, which declares ppu_context and the function
 * table. It comes first: everything below is written against that struct. */
#include "ppu_recomp.h"

/* The Win32 names -- Sleep, GetTickCount64, CreateThread, InterlockedIncrement,
 * VirtualAlloc, the scalar typedefs -- from one place. On Windows this is a
 * passthrough to <windows.h>; off Windows the toolkit's shim supplies the same
 * names over pthreads and mmap, so the call sites below are written once. */
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>   /* _mkdir, for the savestate module below */
#endif

#ifndef _WIN32
#include <sys/mman.h>   /* mmap: the guest arena, see alloc_guest_vm */
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>   /* the main thread's run loop, see run_guest */
#include <pthread.h>
#endif

#ifdef _WIN32
/* timeBeginPeriod: <windows.h> arrives with WIN32_LEAN_AND_MEAN set, which
 * excludes the multimedia timer API, so it is asked for by name -- and after
 * windows.h, since timeapi.h uses UINT. No shim exists off Windows because
 * there is nothing to shim: the 15.6 ms default timer granularity this works
 * around is a Windows problem. */
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * What the scaffold gives this file
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);      /* ELF -> guest RAM, returns entry OPD */
void     ppu_recomp_register(void);           /* generated: lifted table -> address map */
void     ppu_hle_init(void);                  /* firmware import NID -> HLE handler */
void     ppu_sysprx_register(void);           /* boot-critical CRT (sys_initialize_tls, ...) */
void     ppu_fs_register(void);               /* cellFs over the real game directory */
void     lv2_init_syscalls(void);             /* the lv2 syscall table */
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

/* This project's own: load whatever real system PRX modules the title needs
 * into guest RAM and register their exports. stubs.cpp has the empty version
 * a game with no lifted PRX wants. */
void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;              /* host dir the PS3 mount points map into */
void     ps3_host_quit(const char* why);      /* cellSysutil.c: REQUEST_EXITGAME, then exit 0 */

/* sys_ppu_thread.c: the main thread's stack, as sys_ppu_thread_get_stack_information
 * reports it. Left at 0 it names VM_STACK_BASE, the first created thread's stack. */
extern uint32_t g_ppu_main_stack_addr;
extern uint32_t g_ppu_main_stack_size;

/* The status the guest handed to sys_process_exit. A title that exits that way
 * never returns through ppu_run, so this is only read on the path where the
 * entry function unwound instead. */
extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

/* Host-provided symbols the runtime and the HLE libraries link against. */
uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;                  /* ppu_loader.cpp: the OOB guard */

/* Guest-callback dispatch. g_ps3_guest_caller is the hook the HLE runtime
 * calls back into recompiled code through -- cellSysutil events and the GCM
 * vblank/flip handlers. ppu_guest_call does the OPD -> dispatch. */
typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

/* The GCM half of the frame clock. */
void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);      /* drain get -> put */
unsigned cellGcm_flip_request_count(void);
void     cellGame_init_from_paramsfo(const char* sfo_path);
void     cellfs_set_root_path(const char* root);
void     cellfs_add_path_mapping(const char* ps3_prefix, const char* host_path);
}

/* ---------------------------------------------------------------------------
 * RSX present backend
 * -----------------------------------------------------------------------*/
/* D3D12 on Windows, Metal on Apple, and the null backend's headless software
 * path anywhere else -- the same selection runtime/host/host_posix.c and the
 * toolkit's boot harness make. All three expose the same three entry points,
 * so the frame clock below is backend-agnostic. */
#if defined(_WIN32)
extern "C" int  rsx_d3d12_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_d3d12_backend_present(void);
extern "C" int  rsx_d3d12_backend_pump_messages(void);
#  define rsx_backend_init    rsx_d3d12_backend_init
#  define rsx_backend_present rsx_d3d12_backend_present
#  define rsx_backend_pump    rsx_d3d12_backend_pump_messages
#  define RSX_BACKEND_NAME    "D3D12"
#elif defined(__APPLE__)
extern "C" int  rsx_metal_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_metal_backend_present(void);
extern "C" int  rsx_metal_backend_pump_messages(void);
#  define rsx_backend_init    rsx_metal_backend_init
#  define rsx_backend_present rsx_metal_backend_present
#  define rsx_backend_pump    rsx_metal_backend_pump_messages
#  define RSX_BACKEND_NAME    "Metal"
#elif defined(PS3RECOMP_RSX_VULKAN)
extern "C" int  rsx_vulkan_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_vulkan_backend_present(void);
extern "C" void rsx_vulkan_backend_present_begin(void);
extern "C" void rsx_vulkan_backend_present_finish(void);
extern "C" int  rsx_vulkan_backend_pump_messages(void);
#  define rsx_backend_init    rsx_vulkan_backend_init
#  define rsx_backend_present rsx_vulkan_backend_present
#  define rsx_backend_pump    rsx_vulkan_backend_pump_messages
#  define RSX_BACKEND_NAME    "Vulkan"
#else
extern "C" int  rsx_null_backend_init(uint32_t w, uint32_t h, const char* title);
extern "C" void rsx_null_backend_present(void);
extern "C" int  rsx_null_backend_pump_messages(void);
#  define rsx_backend_init    rsx_null_backend_init
#  define rsx_backend_present rsx_null_backend_present
#  define rsx_backend_pump    rsx_null_backend_pump_messages
#  define RSX_BACKEND_NAME    "null (headless software)"
#endif

/* ---------------------------------------------------------------------------
 * Configuration
 * -----------------------------------------------------------------------*/
/* The flat VM must span every region the PS3 memory map uses, not just the
 * game image: sys_ppu_thread_create puts thread stacks at 0xD0000000, so a
 * smaller arena makes every spawned thread's stack access out of bounds. */
#define VM_SIZE       0x100010000ull   /* the full 32-bit guest space + a 64K guard */
#define STACK_TOP     0x0FF00000u      /* main-thread stack, below the 0x10000000 segment */
#define STACK_SIZE    0x00100000u      /* what the main thread is told it has */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* The backend's present target and window are created once, at this size,
 * and never resized afterwards -- so the size has to match the mode the
 * guest will actually present. PS3_VIDEO_RES is the same switch the
 * cellVideoOut HLE reads when it decides which modes to offer the guest:
 * "1080" offers (and this title selects) 1920x1080, so init the backend
 * at 1920x1080 too; anything else keeps the 1280x720 default. Without
 * this, a 1080p guest frame is downscaled into a 720p present target on
 * every flip and the "1080p" output is a 720p picture stretched back up. */
static void comet_window_size(int* w, int* h)
{
    const char* r = getenv("PS3_VIDEO_RES");
    if (r && strcmp(r, "1080") == 0) { *w = 1920; *h = 1080; }
    else { *w = WINDOW_WIDTH; *h = WINDOW_HEIGHT; }
}

/* ---------------------------------------------------------------------------
 * Frame clock
 * -----------------------------------------------------------------------*/
/* On real hardware the RSX raises a vblank interrupt about 60 times a second
 * and that is what drives the game's frame loop. With no RSX it is synthesized
 * here: a host thread calls cellGcmTickVBlank/TickFlip, which invoke the
 * handlers the guest registered, and drains the GCM FIFO. Without this the
 * title initialises, registers its handlers and then waits forever.
 *
 * The ticks are driven off real elapsed time rather than off how long present()
 * took. A hidden or occluded window makes present block hard, and pacing the
 * ticks behind it paces the whole game behind it. */
static volatile LONG g_frames_presented = 0;

/* Frames handed to the backend at a guest FLIP boundary -- one per frame the
 * guest actually finished. The presents made before the guest's first flip, so
 * a fresh window is not left blank through a long boot, carry no guest frame
 * and are deliberately not counted. */
extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

#if defined(PS3RECOMP_RSX_VULKAN)
static HANDLE s_vk_present_ev = NULL;

static DWORD WINAPI vk_present_worker(LPVOID)
{
    for (;;) {
        WaitForSingleObject(s_vk_present_ev, INFINITE);
        rsx_vulkan_backend_present_finish();
    }
    return 0;
}
#endif


/* ------------------------------------------------------------------ */
/* Comet Crash cheats -- stage 1: host-side effects.                   */
/*                                                                     */
/* Four toggles (index order fixed; stage 2's in-game Cheats submenu   */
/* flips them through comet_cheats_flip):                              */
/*   0 thorium     pin the player's Thorium wallet (both shadow        */
/*                 records + the float source channel) at 1000       */
/*   1 buildables  neutralise the build palette's requirement table    */
/*                 and set the two tech-mask bits, so every model      */
/*                 kind is buildable in every mode                     */
/*   2 stockpiles  pin unit stockpiles (60 per type) so produced       */
/*                 unit types can be sent indefinitely                 */
/*   3 health      pin the player's base health at its maximum         */
/*                                                                     */
/* Initial state comes from CC_CHEAT_THORIUM / CC_CHEAT_BUILDABLES /   */
/* CC_CHEAT_STOCKPILES (set and not "0" = on). A control file          */
/* (CC_CHEATS_FILE, default $HOME/.cc_cheats) with lines               */
/* "thorium=1" / "buildables=0" / "stockpiles=1" is re-read whenever   */
/* its mtime changes, so toggles can flip while the game runs.         */
/*                                                                     */
/* Guest addresses (verified 2026-11-07 against the corpus and live    */
/* memory; see notes/RECONSTRUCTION-041): the central game-state       */
/* object sits at guest 0x30E600; its state word at +0x5520C0 (13 =    */
/* arena, 14 = pause); the Thorium stat block at +0x1B0 (sentinel      */
/* 0x7FFFFFFF, balance +0x1B4, shadow copy +0x1D0/+0x1D4) is seeded     */
/* from float channels at +0x348+i*0x80 (channel 1 at +0x3C8); the     */
/* requirement table at guest 0x17F330 holds 28 records of 20 bytes    */
/* (first word = required level, indexed (kind-20)*20); the tech       */
/* mask u16 is at +0x302+p*0x80 (bits 4/5 gate kinds 32/27); unit      */
/* stockpile u16s at +0x364+p*0x80+k*2; the player's life is
/* the i16 at block +0x58 (human +0x3D8), its maximum the i16 at
/* +0x3530+p*2 (human +0x3532).                                */
/* ------------------------------------------------------------------ */
/* Pin amounts are configurable (Jerry, 2026-11-07): the control file
 * may carry "thorium_amount=N" / "stockpile_amount=N", and the env
 * CC_CHEAT_THORIUM_AMOUNT / CC_CHEAT_STOCKPILE_AMOUNT seeds them at
 * startup. Both clamp to the safe maxima determined in play: Thorium
 * 1000 (larger values spill the HUD gauge across the screen) and
 * stockpiles 60 per type (larger values flood the unit cap and break
 * abilities such as thief conversion). */
/* ------------------------------------------------------------------ */
/* Stage 3 (2026-11-08): cheats are per player. The submenu gains a    */
/* "Player: N" selector row (the game's own option-row idiom: press   */
/* to cycle); the four toggle rows below it show and flip the         */
/* selected player's toggles. Battle fields up to four players        */
/* (stat-array slots 0-3 in join order); Campaign's local human is    */
/* Player 1 (slot 1) and its AI opponent is never a cheat target.     */
/* See notes/RECONSTRUCTION-049.                                      */
/* AMOUNTS PATCH (state) */
static int s_thorium_amount   = 1000;
static int s_stockpile_amount = 60;
static int comet_cheat_amount(const char *s, int def, int max)
{
    if (!s || !s[0]) return def;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return def;
    if (v < 0) v = 0;
    if (v > max) v = max;
    return (int)v;
}

#define COMET_CHEAT_COUNT 5  /* SHIP CHEAT (stage 4): index 4 = ship */
#define COMET_PLAYER_COUNT 4
/* STAGE3 PATCH (state): toggles are per player: s_cheats[player][idx].
   s_cur_player is the player the in-game submenu is showing (its
   Player row cycles it). Player numbering is the on-screen one; the
   tick maps players to stat-array slots per mode (see the tick). */
static int s_cheats[COMET_PLAYER_COUNT][COMET_CHEAT_COUNT] = { { 0, 0, 0, 0, 0 } };
static int s_cur_player = 0;
static const char *const s_cheat_names[COMET_CHEAT_COUNT] = { "thorium", "buildables", "stockpiles", "health", "ship" };

static uint32_t comet_rd32(uint32_t guest_addr)
{
    return __builtin_bswap32(*(uint32_t *)(vm_base + guest_addr));
}
static void comet_wr32(uint32_t guest_addr, uint32_t v)
{
    *(uint32_t *)(vm_base + guest_addr) = __builtin_bswap32(v);
}
static uint16_t comet_rd16(uint32_t guest_addr)
{
    return __builtin_bswap16(*(uint16_t *)(vm_base + guest_addr));
}
static void comet_wr16(uint32_t guest_addr, uint16_t v)
{
    *(uint16_t *)(vm_base + guest_addr) = __builtin_bswap16(v);
}

extern "C" int comet_cheats_get(int idx)
{
    /* STAGE3 PATCH (getflip): the submenu shows the selected player. */
    return (idx >= 0 && idx < COMET_CHEAT_COUNT) ? s_cheats[s_cur_player][idx] : 0;
}
extern "C" void comet_cheats_flip(int idx)
{
    if (idx >= 0 && idx < COMET_CHEAT_COUNT)
        s_cheats[s_cur_player][idx] = !s_cheats[s_cur_player][idx];
}
/* STAGE3 PATCH (getflip): Player selector row bridge for the lift. */
extern "C" int comet_cheats_get_player(void)
{
    return s_cur_player;
}
extern "C" void comet_cheats_cycle_player(void)
{
    s_cur_player = (s_cur_player + 1) % COMET_PLAYER_COUNT;
}

static void comet_cheats_poll(void)
{
    static int s_init = 0;
    static long s_file_mtime = -1;
    static int s_frame = 0;
    if (!s_init) {
        s_init = 1;
        const char *envs[COMET_CHEAT_COUNT] = {
            getenv("CC_CHEAT_THORIUM"), getenv("CC_CHEAT_BUILDABLES"), getenv("CC_CHEAT_STOCKPILES"), getenv("CC_CHEAT_HEALTH"), getenv("CC_CHEAT_SHIP")
        };
        for (int i = 0; i < COMET_CHEAT_COUNT; i++)
            if (envs[i] && envs[i][0] && strcmp(envs[i], "0") != 0) s_cheats[0][i] = 1;
        /* STAGE3 PATCH (env): per-player seeds CC_CHEAT_P<N>_<NAME>. */
        {
            static const char *const upper[COMET_CHEAT_COUNT] = {
                "THORIUM", "BUILDABLES", "STOCKPILES", "HEALTH", "SHIP"
            };
            for (int p = 1; p < COMET_PLAYER_COUNT; p++)
                for (int i = 0; i < COMET_CHEAT_COUNT; i++) {
                    char name[40];
                    snprintf(name, sizeof name, "CC_CHEAT_P%d_%s", p + 1, upper[i]);
                    const char *e = getenv(name);
                    if (e && e[0] && strcmp(e, "0") != 0) s_cheats[p][i] = 1;
                }
        }
        /* AMOUNTS PATCH (env) */
        s_thorium_amount   = comet_cheat_amount(getenv("CC_CHEAT_THORIUM_AMOUNT"),
                                                s_thorium_amount, 1000);
        s_stockpile_amount = comet_cheat_amount(getenv("CC_CHEAT_STOCKPILE_AMOUNT"),
                                                s_stockpile_amount, 60);
        if (getenv("CC_CHEAT_THORIUM_AMOUNT") || getenv("CC_CHEAT_STOCKPILE_AMOUNT"))
            fprintf(stderr, "[cheats] env amounts: thorium=%d stockpiles=%d\n",
                    s_thorium_amount, s_stockpile_amount);
        /* STAGE3 PATCH (env): per-player env log line. */
        {
            int any_env = 0;
            for (int p = 0; p < COMET_PLAYER_COUNT; p++)
                for (int i = 0; i < COMET_CHEAT_COUNT; i++)
                    any_env |= s_cheats[p][i];
            if (any_env)
                fprintf(stderr, "[cheats] env: P1 t%d b%d s%d h%d v%d | P2 t%d b%d s%d h%d v%d | P3 t%d b%d s%d h%d v%d | P4 t%d b%d s%d h%d v%d\n",
                        s_cheats[0][0], s_cheats[0][1], s_cheats[0][2], s_cheats[0][3], s_cheats[0][4],
                        s_cheats[1][0], s_cheats[1][1], s_cheats[1][2], s_cheats[1][3], s_cheats[1][4],
                        s_cheats[2][0], s_cheats[2][1], s_cheats[2][2], s_cheats[2][3], s_cheats[2][4],
                        s_cheats[3][0], s_cheats[3][1], s_cheats[3][2], s_cheats[3][3], s_cheats[3][4]);
        }
    }
    if ((++s_frame % 30) != 0) return;
    const char *path = getenv("CC_CHEATS_FILE");
    char defpath[512];
    if (!path || !path[0]) {
        const char *home = getenv("HOME");
        snprintf(defpath, sizeof defpath, "%s/.cc_cheats", home ? home : ".");
        path = defpath;
    }
    struct stat st;
    if (stat(path, &st) != 0) return;
    if ((long)st.st_mtime == s_file_mtime) return;
    s_file_mtime = (long)st.st_mtime;
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    int changed = 0;
    while (fgets(line, sizeof line, f)) {
        /* STAGE3 PATCH (fileparse): legacy "thorium=1" lines address
           Player 1; "p2_thorium=1" .. "p4_health=0" address Players
           2-4. */
        {
            int p = 0;
            const char *rest = line;
            if (line[0] == 'p' && line[1] >= '2' && line[1] <= '4' && line[2] == '_') {
                p = line[1] - '1';
                rest = line + 3;
            }
            for (int i = 0; i < COMET_CHEAT_COUNT; i++) {
                size_t n = strlen(s_cheat_names[i]);
                if (strncmp(rest, s_cheat_names[i], n) == 0 && rest[n] == '=') {
                    int v = (rest[n + 1] == '1') ? 1 : 0;
                    if (s_cheats[p][i] != v) { s_cheats[p][i] = v; changed = 1; }
                }
            }
        }
        /* AMOUNTS PATCH (fileparse) */
        {
            static const char *const amt_names[2] = { "thorium_amount", "stockpile_amount" };
            for (int a = 0; a < 2; a++) {
                size_t n = strlen(amt_names[a]);
                if (strncmp(line, amt_names[a], n) == 0 && line[n] == '=') {
                    int *slot = a ? &s_stockpile_amount : &s_thorium_amount;
                    int v = comet_cheat_amount(line + n + 1, *slot, a ? 60 : 1000);
                    if (*slot != v) { *slot = v; changed = 1; }
                }
            }
        }
    }
    fclose(f);
    if (changed)
        /* AMOUNTS PATCH (logline) */
        /* STAGE3 PATCH (filelog): per-player file log line. */
        fprintf(stderr, "[cheats] file: P1 t%d b%d s%d h%d v%d | P2 t%d b%d s%d h%d v%d | P3 t%d b%d s%d h%d v%d | P4 t%d b%d s%d h%d v%d (amounts thorium=%d stockpiles=%d)\n",
                s_cheats[0][0], s_cheats[0][1], s_cheats[0][2], s_cheats[0][3], s_cheats[0][4],
                s_cheats[1][0], s_cheats[1][1], s_cheats[1][2], s_cheats[1][3], s_cheats[1][4],
                s_cheats[2][0], s_cheats[2][1], s_cheats[2][2], s_cheats[2][3], s_cheats[2][4],
                s_cheats[3][0], s_cheats[3][1], s_cheats[3][2], s_cheats[3][3], s_cheats[3][4],
                s_thorium_amount, s_stockpile_amount);
}

/* Stage 2: plant the Cheats menu's label strings in the unclaimed
   guest window at 0x21010000 (above the runtime's HLE inject window,
   below the SPU windows). The lifted menu patch reads them from
   there; the game itself never generates addresses in that range. */
static void comet_cheats_plant_labels(void)
{
    static int s_planted = 0;
    if (s_planted) return;
    s_planted = 1;
    static const struct { uint32_t off; const char *text; } kLabels[] = {
        { 0x000, "Cheats" },
        { 0x020, "Thorium: On" },    { 0x040, "Thorium: Off" },
        { 0x060, "Buildables: On" }, { 0x080, "Buildables: Off" },
        { 0x0A0, "Stockpiles: On" }, { 0x0C0, "Stockpiles: Off" },
        { 0x0E0, "Health: On" },     { 0x100, "Health: Off" },
        /* STAGE3 PATCH (labels): submenu Player selector row. */
        { 0x120, "Player: 1" },      { 0x140, "Player: 2" },
        { 0x160, "Player: 3" },      { 0x180, "Player: 4" },
        /* SHIP CHEAT (stage 4): Ship toggle row labels. */
        { 0x1A0, "Ship: On" },       { 0x1C0, "Ship: Off" },
    };
    for (unsigned i = 0; i < sizeof kLabels / sizeof kLabels[0]; i++)
        memcpy(vm_base + 0x21010000 + kLabels[i].off, kLabels[i].text,
               strlen(kLabels[i].text) + 1);
    fprintf(stderr, "[cheats] menu labels planted at guest 0x21010000\n");
}

static void comet_cheats_tick(void)
{
    static const uint32_t kBase = 0x0030E600;
    static uint32_t s_req_orig[28];
    static int s_req_patched = 0;
    if (!vm_base) return;
    comet_cheats_plant_labels();
    comet_cheats_poll();

    uint32_t state = comet_rd32(kBase + 0x005520C0);
    int in_match = (state == 13 || state == 14);

    /* STAGE3 PATCH (tick): per-player pins. The mode word at
     * +0x55209C is 2 in Battle, 0 in Campaign (verified live
     * 2026-11-08; mission ids alongside: Battle 101, Campaign 1).
     * Battle: Player N -> slot N-1. Otherwise: Player 1 -> slot 1
     * (the local human), Players 2/3 -> slots 2/3 (inactive in
     * Campaign), Player 4 -> no slot. Slot 0 outside Battle -- the
     * Campaign AI opponent -- is never pinned. */
    int battle = (comet_rd32(kBase + 0x0055209C) == 2);
    int slot_of[COMET_PLAYER_COUNT];
    for (int p = 0; p < COMET_PLAYER_COUNT; p++)
        slot_of[p] = battle ? p : (p < 3 ? p + 1 : -1);
    int any_buildables = 0;
    for (int p = 0; p < COMET_PLAYER_COUNT; p++)
        any_buildables |= s_cheats[p][1];

    /* Buildables: requirement-table neutralise/restore is edge-triggered
     * (the table is static data; patch once, restore when no player's
     * toggle remains on). The table is shared by all seats, so in
     * Battle one player's Buildables opens the palette for everyone;
     * the per-seat tech-mask bits below stay individual. */
    if (any_buildables && !s_req_patched) {
        for (int i = 0; i < 28; i++) {
            s_req_orig[i] = comet_rd32(0x0017F330 + i * 20);
            comet_wr32(0x0017F330 + i * 20, 0);
        }
        s_req_patched = 1;
        fprintf(stderr, "[cheats] requirement table neutralised\n");
    } else if (!any_buildables && s_req_patched) {
        for (int i = 0; i < 28; i++)
            comet_wr32(0x0017F330 + i * 20, s_req_orig[i]);
        s_req_patched = 0;
        fprintf(stderr, "[cheats] requirement table restored\n");
    }
    if (any_buildables && in_match) {
        for (int p = 0; p < COMET_PLAYER_COUNT; p++) {
            int slot = slot_of[p];
            if (slot < 0 || !s_cheats[p][1]) continue;
            uint32_t a = kBase + 0x302 + slot * 0x80;
            comet_wr16(a, (uint16_t)(comet_rd16(a) | 0x0030));
        }
    }

    if (!in_match) return;

    /* Thorium: the authoritative balance is the float channel in each
     * player's stat block (+0x348 + slot*0x80); pin it per player.
     * The shared u32 shadow pair (+0x1B4/+0x1D4) keeps following the
     * slot-1 player exactly as the stage-1/2 pin did (Campaign: that
     * is Player 1, the human; Battle: Player 2), and only while that
     * player's Thorium toggle is on. Pin the pair only when both
     * shadow records carry their sentinel, so a different layout can
     * never be written by mistake. */
    {
        float thf = (float)s_thorium_amount; uint32_t thbits; memcpy(&thbits, &thf, 4);
        for (int p = 0; p < COMET_PLAYER_COUNT; p++) {
            int slot = slot_of[p];
            if (slot < 0 || !s_cheats[p][0]) continue;
            /* AMOUNTS PATCH (thoriumpin) */
            comet_wr32(kBase + 0x348 + slot * 0x80, thbits);
        }
        int pair_player = battle ? 1 : 0;
        if (s_cheats[pair_player][0] &&
            comet_rd32(kBase + 0x1B0) == 0x7FFFFFFF && comet_rd32(kBase + 0x1D0) == 0x7FFFFFFF) {
            comet_wr32(kBase + 0x1B4, (uint32_t)s_thorium_amount);
            comet_wr32(kBase + 0x1D4, (uint32_t)s_thorium_amount);
        }
    }
    for (int p = 0; p < COMET_PLAYER_COUNT; p++) {
        int slot = slot_of[p];
        if (slot < 0) continue;
        if (s_cheats[p][2]) {
            for (int k = 0; k < 8; k++)
                /* AMOUNTS PATCH (stockpilepin) */
                comet_wr16(kBase + 0x364 + slot * 0x80 + k * 2, (uint16_t)s_stockpile_amount);
            /* STOCKPILE AGGREGATE FIX: keep the aggregate counter
             * (block +0x5E, the game's own sum of the per-type
             * stockpiles) equal to the pinned total, so cheat-fed
             * sends cannot underflow it. 2-byte store only: +0x5C
             * and +0x5D are flag bytes. */
            comet_wr16(kBase + 0x35E + slot * 0x80, (uint16_t)(8 * s_stockpile_amount));
        }
        if (s_cheats[p][3]) {
            /* Infinite Health, per player: pin current life (i16 at
             * block +0x58) to the per-match maximum (i16 array at
             * +0x3530 + slot*2). 2-byte write only -- the neighbouring
             * i16 at block +0x5A is the units-waiting counter. When
             * the max reads 0 the seat is inactive; skip. */
            int16_t max_life = (int16_t)comet_rd16(kBase + 0x3530 + slot * 2);
            if (max_life > 0)
                comet_wr16(kBase + 0x300 + slot * 0x80 + 0x58, (uint16_t)max_life);
        }
        if (s_cheats[p][4]) {
            /* SHIP CHEAT (stage 4): the player's ship is a type-1
             * entity in the double-buffered entity banks. Its index
             * sits in the per-player registry at +0x36C8 + slot*2;
             * bank bases are the u32s at +0x180 (A) and +0x198 (B).
             * Health is the float at record +0x50, its maximum the
             * float at +0x64 (1000.0); the sim (SPU job 00188300)
             * applies damage/heal as float deltas to that field and
             * tests it <= 0 for the death path, so pinning it to the
             * record's own max every tick makes the ship unkillable.
             * Pin BOTH banks (the sim alternates them per frame) and
             * only while the record is still a live ship (type byte
             * +0x0F == 1; after game over the slot is a type-63
             * tombstone). If the registry slot for this player is
             * empty, fall back to the first live ship record in
             * registry order for Player 1 only (solo human ships are
             * the only ones observed; AI seats hold no ship). */
            uint16_t idx = comet_rd16(kBase + 0x36C8 + slot * 2);
            if (!idx && p == 0) {
                for (int r = 0; r < COMET_PLAYER_COUNT && !idx; r++) {
                    uint16_t cand = comet_rd16(kBase + 0x36C8 + r * 2);
                    if (!cand) continue;
                    uint32_t bank0 = comet_rd32(kBase + 0x180);
                    if (bank0 && *(uint8_t *)(vm_base + bank0 + (uint32_t)cand * 256 + 0x0F) == 1)
                        idx = cand;
                }
            }
            if (idx) {
                for (int b = 0; b < 2; b++) {
                    uint32_t bank = comet_rd32(kBase + (b ? 0x198 : 0x180));
                    if (!bank) continue;
                    uint32_t rec = bank + (uint32_t)idx * 256;
                    if (*(uint8_t *)(vm_base + rec + 0x0F) == 1)
                        comet_wr32(rec + 0x50, comet_rd32(rec + 0x64));
                }
            }
        }
    }
}


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
 *
 * Round 4 (SAVESTATE FIFO RESYNC): a real-user load on the real
 * GPU restored the match but killed the picture -- fragment
 * programs that translated fine pre-load failed HLSL
 * translation after it ('array index out of range'), and a
 * second attempt desynced the guest FIFO outright (unmapped
 * JUMP + resync). Root cause: the host FIFO walker's position
 * (fork-side s_fifo_getoff) is host-private state from the
 * PRE-load session and nothing re-derived it from the restored
 * guest state, so on resume the walker drained the restored
 * ring from its stale offset -- replaying consumed history,
 * parsing inline vertex/uniform payload as method headers
 * (garbage state: absurd blits, poisoned shader/texture state,
 * fragment programs decompiled from the wrong bytes), or
 * hitting an unmapped JUMP. The fix is a walker hold + resync,
 * not another cache flush: ps3_gcm_fifo_savestate_hold parks
 * the walker with the pre-stop resyncs, and
 * ps3_gcm_fifo_savestate_resync -- after the copy, on every
 * outcome -- moves it to the restored guest context's current
 * position and clears its pass state. Only host-private shadow
 * state moves; guest-visible caches stay exactly as restored.
 * -----------------------------------------------------------------------*/
extern "C" void ps3_pad_resync_savestate(void) __attribute__((weak));
extern "C" void ps3_spu_mp3_reset_streams(void) __attribute__((weak));
/* SAVESTATE FIFO RESYNC: the FIFO walker hold + resync
 * (round 4, notes 051). */
extern "C" void ps3_gcm_fifo_savestate_hold(void) __attribute__((weak));
extern "C" void ps3_gcm_fifo_savestate_resync(void) __attribute__((weak));

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
    /* SAVESTATE FIFO RESYNC: park the host FIFO walker before
     * the copy and keep it parked until the resync below moves
     * it to the restored guest position. Its position is
     * host-private state from this session; left alone it
     * would drain the restored ring from the wrong offset. */
    if (ps3_gcm_fifo_savestate_hold) ps3_gcm_fifo_savestate_hold();
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
    /* SAVESTATE FIFO RESYNC: memory is the saved session's
     * again; move the walker to match before it drains
     * anything (fires on every restore outcome -- on a
     * pre-write failure the memory is still this session's
     * and the resync is a position no-op). */
    if (ps3_gcm_fifo_savestate_resync) ps3_gcm_fifo_savestate_resync();
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
    /* SAVESTATE FIFO RESYNC: as the POSIX path. */
    if (ps3_gcm_fifo_savestate_resync) ps3_gcm_fifo_savestate_resync();
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


static void comet_crash_camera_tick(void)
{
    if (!vm_base) return;

    comet_cheats_tick();
    comet_savestate_poll_trigger();

    static const uint32_t kGameBase = 0x0030E600;
    static int s_cam_diag = -1;
    if (s_cam_diag < 0) {
        const char* e = getenv("CC_CAM_DIAG");
        s_cam_diag = e ? atoi(e) : (getenv("CC_ZOOM_DIAG") || getenv("CC_GAME_STATE")) ? 1 : 1;
    }
    static int s_lock_zoom = -1;
    if (s_lock_zoom < 0) {
        const char* e = getenv("CC_LOCK_ZOOM");
        s_lock_zoom = e ? atoi(e) : 0;
    }
    static int s_force_overview = -1;
    if (s_force_overview < 0) {
        const char* e = getenv("CC_FORCE_OVERVIEW");
        s_force_overview = e ? atoi(e) : 0;
    }
    static int s_force_follow = -1;
    if (s_force_follow < 0) {
        const char* e = getenv("CC_FORCE_FOLLOW");
        s_force_follow = e ? atoi(e) : 0;
    }

    uint32_t zoom_addr   = kGameBase + 0x005520D8;
    uint32_t follow_addr = kGameBase + 0x00551F1C;
    uint32_t ship_addr   = kGameBase + 0x00552070;
    uint32_t state_addr  = kGameBase + 0x005520C0;
    uint32_t proj_addr   = kGameBase + 0x0054DFA0;
    uint32_t mview_addr  = kGameBase + 0x0054DFE0;

    uint32_t raw_st = __builtin_bswap32(*(uint32_t*)(vm_base + state_addr));

    if (s_lock_zoom > 0) {
        uint32_t zero = 0;
        *(uint32_t*)(vm_base + zoom_addr) = zero;
    }
    if (s_force_overview > 0) {
        *(uint8_t*)(vm_base + follow_addr) = 0;
    } else if (s_force_follow > 0 && (raw_st == 12 || raw_st == 13)) {
        *(uint8_t*)(vm_base + follow_addr) = 1;
    }

    unsigned fc = cellGcm_flip_request_count();
    uint32_t raw_z = __builtin_bswap32(*(uint32_t*)(vm_base + zoom_addr));
    float cur_z = 0.0f;
    memcpy(&cur_z, &raw_z, 4);

    uint8_t follow = *(uint8_t*)(vm_base + follow_addr);

    float ship[4] = {0};
    for (int i = 0; i < 4; i++) {
        uint32_t sw = __builtin_bswap32(*(uint32_t*)(vm_base + ship_addr + i * 4));
        memcpy(&ship[i], &sw, 4);
    }

    float mv[16] = {0};
    for (int i = 0; i < 16; i++) {
        uint32_t mw = __builtin_bswap32(*(uint32_t*)(vm_base + mview_addr + i * 4));
        memcpy(&mv[i], &mw, 4);
    }

    float proj[16] = {0};
    for (int i = 0; i < 16; i++) {
        uint32_t pw = __builtin_bswap32(*(uint32_t*)(vm_base + proj_addr + i * 4));
        memcpy(&proj[i], &pw, 4);
    }

    static float s_last_z = -999.0f;
    static uint8_t s_last_f = 255;
    static uint32_t s_last_st = 0xFFFFFFFF;
    static float s_last_sx = -999.0f, s_last_sz = -999.0f;
    static unsigned s_last_fc = 0;
    if (s_cam_diag && (fabsf(cur_z - s_last_z) > 0.001f || follow != s_last_f || raw_st != s_last_st ||
                       fabsf(ship[0] - s_last_sx) > 0.1f || fabsf(ship[2] - s_last_sz) > 0.1f || (fc > 0 && fc - s_last_fc >= 120))) {
        uint8_t gw = *(uint8_t*)(vm_base + kGameBase + 0x37E1);
        uint8_t gh = *(uint8_t*)(vm_base + kGameBase + 0x37E2);
        printf("[CC_CAM] flip %u: state=%u follow=%u (force_fol=%d) zoom=%.4f ship=(X=%.2f, Y=%.2f, Z=%.2f) grid=%ux%u row_trans=(%.2f, %.2f, %.2f) col_trans=(%.2f, %.2f, %.2f) proj=(%.3f, %.3f)\n",
               fc, raw_st, follow, s_force_follow, cur_z, ship[0], ship[1], ship[2], gw, gh,
               mv[3], mv[7], mv[11], mv[12], mv[13], mv[14], proj[0], proj[5]);
        fflush(stdout);
        s_last_z = cur_z;
        s_last_f = follow;
        s_last_st = raw_st;
        s_last_sx = ship[0];
        s_last_sz = ship[2];
        s_last_fc = fc;
    }
}

/* The Vulkan backend asks before honouring a window quit (close button
 * or Escape): quitting mid-run throws the run away, so the arena states
 * -- 13 (gameplay) and 14 (pause / battle end) -- need confirmation.
 * Menus and loading quit immediately. Guest state word: DAT_005520C0,
 * read exactly as comet_crash_camera_tick reads it. */
extern "C" int ps3_host_quit_needs_confirm(void)
{
    if (!vm_base) return 0;
    static const uint32_t kStateAddr = 0x0030E600u + 0x005520C0u;
    const uint32_t state = __builtin_bswap32(*(const uint32_t*)(vm_base + kStateAddr));
    return state == 13 || state == 14;
}

static void present_guest_frame(void)
{
    comet_crash_camera_tick();
#if defined(PS3RECOMP_RSX_VULKAN)
    if (s_vk_present_ev) {
        rsx_vulkan_backend_present_begin();
        SetEvent(s_vk_present_ev);
    } else {
        rsx_backend_present();
    }
#else
    rsx_backend_present();
#endif
    /* This thread increments and guest threads read, so interlocked rather
     * than a volatile ++, which on arm64 is neither atomic nor a fence. */
    InterlockedIncrement(&g_frames_presented);
}

extern "C" void cellGcm_fifo_kick_wait(unsigned ms);

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "ps3recomp";

    int win_w, win_h;
    comet_window_size(&win_w, &win_h);
    int rsx_ok = (rsx_backend_init(win_w, win_h, title) == 0);
    fprintf(stderr, "[rsx] %s backend init %s\n", RSX_BACKEND_NAME,
            rsx_ok ? "OK -- window open" : "FAILED");
#if defined(PS3RECOMP_RSX_VULKAN)
    if (rsx_ok) {
        const char* sp = getenv("VK_SPLIT_PRESENT");
        if (!sp || *sp != '0') {
            s_vk_present_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
            if (s_vk_present_ev) {
                HANDLE pth = CreateThread(NULL, 0, vk_present_worker, NULL, 0, NULL);
                if (pth) CloseHandle(pth);
                else { CloseHandle(s_vk_present_ev); s_vk_present_ev = NULL; }
            }
        }
    }
#endif

    unsigned  last_flip = 0;
    (void)last_flip;
    ULONGLONG next_tick = GetTickCount64();

    for (;;) {
        cellGcm_fifo_kick_wait(1);
        ULONGLONG now = GetTickCount64();

        int fired = 0;
        while ((long long)(now - next_tick) >= 0 && fired < 240) {
            cellGcmTickVBlank();
            cellGcmTickFlip();
            comet_crash_camera_tick();
            /* Present a pending flip BEFORE draining any further. The flip
             * fires at a get==put frame boundary on the guest thread, so the
             * batch held right now is exactly the completed frame; presenting
             * after the drain races the guest's next-frame writes and shows a
             * mixed one. */
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            /* Drain the FIFO every tick. This is what writes the RSX sync-fence
             * labels the game's per-frame logic blocks on, so it has to keep
             * advancing at 60 Hz even while present() throttles. */
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick += 16;             /* ~60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;   /* fell too far behind -- resync */

        /* Drain at the outer cadence too, not only on the 16 ms tick: titles
         * fence every render pass on a label the drain writes, and one wave of
         * those at 16 ms apiece paces the guest into single-figure frame rates.
         * The real RSX writes them in microseconds. */
        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            cellGcm_rsx_process_fifo();

            if (rsx_backend_pump() != 0) {
                rsx_ok = 0;              /* window closed */
                /* ...so quit, as the console's Quit Game does, instead of
                 * leaving the game running with no window. */
                ps3_host_quit("window closed");
                continue;
            }
            if (cellGcm_flip_request_count() == 0) {
                rsx_backend_present();
            }
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * VFS root
 * -----------------------------------------------------------------------*/
/* Derive the directory holding PS3_GAME from the EBOOT path
 * <root>/PS3_GAME/USRDIR/EBOOT.elf -> <root>. $PS3_VFS_ROOT overrides. */
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    /* strip EBOOT.elf, USRDIR and PS3_GAME */
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
/* Demand-paging for the flat VM: reserve the whole 4 GB guest space, which
 * costs no commit, and commit each 64 KB page on first access. Every 32-bit
 * guest offset is then valid, so a garbage guest pointer reads zero instead of
 * killing the process. Faults outside the arena fall through untouched. */
static LONG WINAPI vm_commit_veh(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR fault = ep->ExceptionRecord->ExceptionInformation[1];
        uintptr_t base  = (uintptr_t)vm_base;
        if (vm_base && fault >= base && fault < base + VM_SIZE) {
            void* page = (void*)(fault & ~(uintptr_t)0xFFFF);
            if (VirtualAlloc(page, 0x10000, MEM_COMMIT, PAGE_READWRITE))
                return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static bool alloc_guest_vm(void)
{
#ifdef _WIN32
    AddVectoredExceptionHandler(1, vm_commit_veh);
    vm_base = (uint8_t*)VirtualAlloc(NULL, VM_SIZE, MEM_RESERVE, PAGE_READWRITE);
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#else
    /* No vectored exception handlers off Windows, so the arena is mapped up
     * front and lazily backed by the OS: only pages the title touches cost
     * anything. It spans the whole 32-bit space plus the guard, as on Windows.
     * It used to stop below 0xE0000000, but the raw SPU windows (local store
     * and problem-state registers, runtime/spu/spu_raw.c) live from there up.
     * Every load and store a title made into one was reported as an
     * out-of-bounds access and dropped, so a raw SPU was never loaded or
     * started, and sys_raw_spu_create, which clears the registers through
     * vm_base directly, wrote past the end of the mapping. With the whole
     * space backed, ppu_vm_size 0 also gives the loader's bounds check the
     * one-compare fast path Windows already has.
     *
     * mmap, not calloc: vm_commit() hands vm_base + addr to mprotect, which
     * takes page-aligned addresses only. glibc serves an allocation this big
     * from its own mmap chunk and returns a pointer 16 bytes past the page
     * start, so every commit failed with EINVAL and sys_memory_allocate
     * answered ENOMEM to every call. mmap's base is page-aligned and zeroed. */
    void* p = mmap(NULL, VM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    vm_base = (p == MAP_FAILED) ? nullptr : (uint8_t*)p;
    ppu_vm_size = 0;              /* the whole space is backed; no OOB guard needed */
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Main
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

/* ---------------------------------------------------------------------------
 * Running the guest
 * -----------------------------------------------------------------------*/
#if defined(__APPLE__)
/* AppKit only runs on the process's main thread. The Metal backend creates
 * its window and pumps its events with dispatch_sync onto the main queue
 * when it is called from any other thread, as it is from the frame clock.
 * With the guest on the main thread nothing ever serviced that queue: the
 * frame clock blocked in its first rsx_backend_init, no vblank or flip was
 * ever delivered and the FIFO never drained. A title that waits for a flip
 * hangs there. So the guest runs on a thread of its own, with the same
 * 256 MB host stack every guest thread gets (sys_ppu_thread.c), and the
 * main thread turns the run loop until ppu_run returns. Headless runs never
 * touch the main queue, which is why CI does not see the difference. */
struct guest_run { uint32_t entry, stack_top; int rc; volatile int done; };

static void* guest_thread(void* arg)
{
    guest_run* g = (guest_run*)arg;
    g->rc = ppu_run(g->entry, g->stack_top);
    __atomic_store_n(&g->done, 1, __ATOMIC_RELEASE);
    CFRunLoopStop(CFRunLoopGetMain());
    return nullptr;
}

static int run_guest(uint32_t entry, uint32_t stack_top)
{
    static guest_run g;
    g.entry = entry; g.stack_top = stack_top; g.rc = 0; g.done = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 256u * 1024u * 1024u);
    pthread_t th;
    int err = pthread_create(&th, &attr, guest_thread, &g);
    pthread_attr_destroy(&attr);
    if (err != 0) {
        fprintf(stderr, "[boot] could not start the guest thread (%d)\n", err);
        return -1;
    }
    /* Short slices: CFRunLoopStop only ends a run that is already going. */
    while (!__atomic_load_n(&g.done, __ATOMIC_ACQUIRE))
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    pthread_join(th, nullptr);
    return g.rc;
}
#else
static int run_guest(uint32_t entry, uint32_t stack_top) { return ppu_run(entry, stack_top); }
#endif

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <PPU ELF>\n", argv[0]);
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    /* 1 ms timer resolution. The default granularity is about 15.6 ms, which
     * inflates every shorter wait the title makes and throttles the whole
     * thing. POSIX timers are already fine-grained. */
    timeBeginPeriod(1);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: do not lose prints on a kill */
    setvbuf(stderr, NULL, _IONBF, 0);

    /* G8B8 textures: present the two bytes the other way round. Comet Crash's
     * ground-shadow stamps (a G8B8 atlas sampled into the 384x384 shadow map)
     * came out as flat squares with the runtime's default order and as the
     * intended soft blobs with this one; the default is kept for the other
     * titles until they are checked. Overridable: PS3RECOMP_G8B8_SWAP=0. */
#ifdef _WIN32
    if (!getenv("PS3RECOMP_G8B8_SWAP")) _putenv_s("PS3RECOMP_G8B8_SWAP", "1");
#else
    setenv("PS3RECOMP_G8B8_SWAP", "1", 0);
#endif

    printf("=== ps3recomp game runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

    /* Look for PARAM.SFO to initialize cellGame (title id, category/game type). */
    {
        static const char* const kSfoLayouts[] = {
            "%s/PS3_GAME/PARAM.SFO",
            "%s/PARAM.SFO",
            "%s/../PARAM.SFO",
        };
        char sfo[1100];
        int found = 0;
        for (size_t i = 0; i < sizeof kSfoLayouts / sizeof kSfoLayouts[0]; i++) {
            snprintf(sfo, sizeof sfo, kSfoLayouts[i], ppu_vfs_root);
            FILE* f = fopen(sfo, "rb");
            if (f) { fclose(f); found = 1; break; }
        }
        if (!found)
            snprintf(sfo, sizeof sfo, "%s/PS3_GAME/PARAM.SFO", ppu_vfs_root);
        cellGame_init_from_paramsfo(sfo);
    }

    ppu_recomp_register();   /* lifted function table -> address map */
    ps3_load_prx_modules();  /* this game's lifted system PRX, if it has any */
    ppu_hle_init();          /* firmware import NID -> HLE handlers */
    ppu_sysprx_register();   /* boot-critical CRT */
    ppu_fs_register();       /* cellFs over the game directory */
    cellfs_set_root_path(ppu_vfs_root);
    cellfs_add_path_mapping("/dev_hdd0/", ppu_vfs_root);
    cellfs_add_path_mapping("/dev_bdvd/", ppu_vfs_root);
    lv2_init_syscalls();     /* the lv2 syscall table */

    /* Install the guest-callback hook, then start the frame clock. It no-ops
     * until the title registers its vblank and flip handlers during init. */
    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    /* A title that reads its own stack bounds (to find data its parent left
     * below the top, say) gets the stack it really runs on. */
    g_ppu_main_stack_addr = STACK_TOP - STACK_SIZE;
    g_ppu_main_stack_size = STACK_SIZE;

    int rc = run_guest(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    /* A title that called sys_process_exit never reaches this line: that path
     * ends in the host exit(). Getting here means the entry function returned
     * instead, so hand back the status the guest published if it published
     * one. Returning a hardcoded 0 would report success for a run that never
     * got anywhere. */
    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
