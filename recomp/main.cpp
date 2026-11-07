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
/*                 records + the float source channel) at 9999       */
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
/* stockpile u16s at +0x364+p*0x80+k*2.                                */
/* ------------------------------------------------------------------ */
#define COMET_CHEAT_COUNT 4
static int s_cheats[COMET_CHEAT_COUNT] = { 0, 0, 0, 0 };
static const char *const s_cheat_names[COMET_CHEAT_COUNT] = { "thorium", "buildables", "stockpiles", "health" };

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
    return (idx >= 0 && idx < COMET_CHEAT_COUNT) ? s_cheats[idx] : 0;
}
extern "C" void comet_cheats_flip(int idx)
{
    if (idx >= 0 && idx < COMET_CHEAT_COUNT) s_cheats[idx] = !s_cheats[idx];
}

static void comet_cheats_poll(void)
{
    static int s_init = 0;
    static long s_file_mtime = -1;
    static int s_frame = 0;
    if (!s_init) {
        s_init = 1;
        const char *envs[COMET_CHEAT_COUNT] = {
            getenv("CC_CHEAT_THORIUM"), getenv("CC_CHEAT_BUILDABLES"), getenv("CC_CHEAT_STOCKPILES"), getenv("CC_CHEAT_HEALTH")
        };
        for (int i = 0; i < COMET_CHEAT_COUNT; i++)
            if (envs[i] && envs[i][0] && strcmp(envs[i], "0") != 0) s_cheats[i] = 1;
        if (s_cheats[0] || s_cheats[1] || s_cheats[2] || s_cheats[3])
            fprintf(stderr, "[cheats] env: thorium=%d buildables=%d stockpiles=%d health=%d\n",
                    s_cheats[0], s_cheats[1], s_cheats[2], s_cheats[3]);
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
        for (int i = 0; i < COMET_CHEAT_COUNT; i++) {
            size_t n = strlen(s_cheat_names[i]);
            if (strncmp(line, s_cheat_names[i], n) == 0 && line[n] == '=') {
                int v = (line[n + 1] == '1') ? 1 : 0;
                if (s_cheats[i] != v) { s_cheats[i] = v; changed = 1; }
            }
        }
    }
    fclose(f);
    if (changed)
        fprintf(stderr, "[cheats] file: thorium=%d buildables=%d stockpiles=%d health=%d\n",
                s_cheats[0], s_cheats[1], s_cheats[2], s_cheats[3]);
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

    /* Buildables: requirement-table neutralise/restore is edge-triggered
     * (the table is static data; patch once, restore on toggle off). */
    if (s_cheats[1] && !s_req_patched) {
        for (int i = 0; i < 28; i++) {
            s_req_orig[i] = comet_rd32(0x0017F330 + i * 20);
            comet_wr32(0x0017F330 + i * 20, 0);
        }
        s_req_patched = 1;
        fprintf(stderr, "[cheats] requirement table neutralised\n");
    } else if (!s_cheats[1] && s_req_patched) {
        for (int i = 0; i < 28; i++)
            comet_wr32(0x0017F330 + i * 20, s_req_orig[i]);
        s_req_patched = 0;
        fprintf(stderr, "[cheats] requirement table restored\n");
    }
    if (s_cheats[1] && in_match) {
        /* Local player = stat-array index 1; index 0 is the opponent
           (verified live in Battle + Campaign, 2026-11-07). */
        for (int p = 1; p < 2; p++) {
            uint32_t a = kBase + 0x302 + p * 0x80;
            comet_wr16(a, (uint16_t)(comet_rd16(a) | 0x0030));
        }
    }

    if (!in_match) return;

    if (s_cheats[0]) {
        /* Pin only when both shadow records carry their sentinel, so a
         * different layout can never be written by mistake. */
        if (comet_rd32(kBase + 0x1B0) == 0x7FFFFFFF && comet_rd32(kBase + 0x1D0) == 0x7FFFFFFF) {
            comet_wr32(kBase + 0x1B4, 9999);
            comet_wr32(kBase + 0x1D4, 9999);
            float thf = 9999.0f; uint32_t thbits; memcpy(&thbits, &thf, 4);
            comet_wr32(kBase + 0x3C8, thbits);
        }
    }
    if (s_cheats[2]) {
        for (int p = 1; p < 2; p++)
            for (int k = 0; k < 8; k++)
                comet_wr16(kBase + 0x364 + p * 0x80 + k * 2, 60);
    }
}

static void comet_crash_camera_tick(void)
{
    if (!vm_base) return;

    comet_cheats_tick();

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
