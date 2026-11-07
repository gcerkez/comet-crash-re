/*
 * CometCrashRecomp -- host entry point and RSX frame clock.
 *
 * Boots a lifted Comet Crash (NPUB30061) build through ps3recomp's PPU/SPU
 * runtime: maps the 4 GB flat guest address space, loads the user's decrypted
 * EBOOT.ELF for read-only data/TOC initialization, registers the lifted PPU
 * and SPU function tables plus HLE NID handlers, starts the RSX FIFO + VBlank
 * ticker with async Vulkan split-present, and dispatches entry OPD 0x001F2B38.
 */

#include "ppu_recomp.h"
#include "win32_compat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#endif

#ifdef _WIN32
#include <timeapi.h>
#endif

/* ---------------------------------------------------------------------------
 * ps3recomp scaffold imports
 * -----------------------------------------------------------------------*/
extern "C" {
uint32_t ppu_load_elf(const char* path);
void     ppu_recomp_register(void);
void     ppu_hle_init(void);
void     ppu_sysprx_register(void);
void     ppu_fs_register(void);
void     lv2_init_syscalls(void);
int      ppu_run(uint32_t entry_opd, uint32_t stack_top);

void     ps3_load_prx_modules(void);

extern const char* ppu_vfs_root;
void     ps3_host_quit(const char* why);

extern uint32_t g_ppu_main_stack_addr;
extern uint32_t g_ppu_main_stack_size;

extern int     g_sys_process_exit_called;
extern int32_t g_sys_process_exit_code;

uint8_t* vm_base = nullptr;
extern uint32_t ppu_vm_size;

typedef void (*ps3_guest_caller_fn)(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                                    uint64_t, uint64_t, uint64_t, uint64_t);
extern ps3_guest_caller_fn g_ps3_guest_caller;
uint64_t ppu_guest_call(uint32_t, uint64_t, uint64_t, uint64_t, uint64_t,
                        uint64_t, uint64_t, uint64_t, uint64_t);

void     cellGcmTickVBlank(void);
void     cellGcmTickFlip(void);
int      cellGcm_take_flip_pending(void);
void     cellGcm_rsx_process_fifo(void);
unsigned cellGcm_flip_request_count(void);
void     cellGcm_fifo_kick_wait(unsigned ms);
void     cellGame_init_from_paramsfo(const char* sfo_path);
void     cellfs_set_root_path(const char* root);
void     cellfs_add_path_mapping(const char* ps3_prefix, const char* host_path);
}

/* ---------------------------------------------------------------------------
 * RSX present backend selection
 * -----------------------------------------------------------------------*/
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
#define VM_SIZE       0x100010000ull   /* Full 32-bit guest space + 64 KB guard */
#define STACK_TOP     0x0FF00000u      /* Main-thread stack below 0x10000000    */
#define STACK_SIZE    0x00100000u      /* 1 MB main-thread stack                */
#define WINDOW_WIDTH  1280
#define WINDOW_HEIGHT 720

/* ---------------------------------------------------------------------------
 * Frame clock & async Vulkan split-present
 * -----------------------------------------------------------------------*/
static volatile LONG g_frames_presented = 0;

extern "C" unsigned ppu_boot_frames_presented(void)
{
    return (unsigned)g_frames_presented;
}

#if defined(PS3RECOMP_RSX_VULKAN)
static HANDLE s_vk_present_ev = NULL;

/* Runs present_fence wait + Wayland/X11 swapchain blit off the RSX FIFO
 * drain thread so mid-frame PSGL fence labels retire without swapchain stalls. */
static DWORD WINAPI vk_present_worker(LPVOID)
{
    for (;;) {
        WaitForSingleObject(s_vk_present_ev, INFINITE);
        rsx_vulkan_backend_present_finish();
    }
    return 0;
}
#endif

static void comet_crash_camera_tick(void)
{
    if (!vm_base) return;

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

    uint32_t zoom_addr   = kGameBase + 0x005520D8;
    uint32_t follow_addr = kGameBase + 0x00551F1C;
    uint32_t ship_addr   = kGameBase + 0x00552070;
    uint32_t state_addr  = kGameBase + 0x005520C0;
    uint32_t proj_addr   = kGameBase + 0x0054DFA0;
    uint32_t mview_addr  = kGameBase + 0x0054DFE0;

    if (s_lock_zoom > 0) {
        uint32_t zero = 0;
        *(uint32_t*)(vm_base + zoom_addr) = zero;
    }
    if (s_force_overview > 0) {
        *(uint8_t*)(vm_base + follow_addr) = 0;
    }

    unsigned fc = cellGcm_flip_request_count();
    uint32_t raw_z = __builtin_bswap32(*(uint32_t*)(vm_base + zoom_addr));
    float cur_z = 0.0f;
    memcpy(&cur_z, &raw_z, 4);

    uint8_t follow = *(uint8_t*)(vm_base + follow_addr);
    uint32_t raw_st = __builtin_bswap32(*(uint32_t*)(vm_base + state_addr));

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
    static float s_last_sx = -999.0f, s_last_sy = -999.0f;
    static unsigned s_last_fc = 0;
    if (s_cam_diag && (fabsf(cur_z - s_last_z) > 0.001f || follow != s_last_f || raw_st != s_last_st ||
                       fabsf(ship[0] - s_last_sx) > 0.1f || fabsf(ship[1] - s_last_sy) > 0.1f || (fc > 0 && fc - s_last_fc >= 120))) {
        uint8_t gw = *(uint8_t*)(vm_base + kGameBase + 0x37E1);
        uint8_t gh = *(uint8_t*)(vm_base + kGameBase + 0x37E2);
        printf("[CC_CAM] flip %u: state=%u follow=%u zoom=%.4f ship=(%.2f, %.2f, %.2f) grid=%ux%u cam_trans=(%.2f, %.2f, %.2f) proj=(%.3f, %.3f)\n",
               fc, raw_st, follow, cur_z, ship[0], ship[1], ship[2], gw, gh,
               mv[12], mv[13], mv[14], proj[0], proj[5]);
        fflush(stdout);
        s_last_z = cur_z;
        s_last_f = follow;
        s_last_st = raw_st;
        s_last_sx = ship[0];
        s_last_sy = ship[1];
        s_last_fc = fc;
    }
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
    InterlockedIncrement(&g_frames_presented);
}

static DWORD WINAPI frame_clock(LPVOID)
{
    const char* title = getenv("PS3_TITLE");
    if (!title || !*title) title = "Comet Crash";

    int rsx_ok = (rsx_backend_init(WINDOW_WIDTH, WINDOW_HEIGHT, title) == 0);
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
        /* Wake immediately when the guest flushes ctrl->put, or after 1 ms. */
        cellGcm_fifo_kick_wait(1);
        ULONGLONG now = GetTickCount64();

        int fired = 0;
        while ((long long)(now - next_tick) >= 0 && fired < 240) {
            cellGcmTickVBlank();
            cellGcmTickFlip();
            comet_crash_camera_tick();
            if (rsx_ok && cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            if (rsx_ok) cellGcm_rsx_process_fifo();
            next_tick += 16;             /* ~60 Hz */
            fired++;
        }
        if (fired >= 240) next_tick = now;

        if (rsx_ok) {
            if (cellGcm_take_flip_pending()) {
                present_guest_frame();
                last_flip = cellGcm_flip_request_count();
            }
            cellGcm_rsx_process_fifo();

            if (rsx_backend_pump() != 0) {
                rsx_ok = 0;
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
 * VFS root resolution
 * -----------------------------------------------------------------------*/
static char s_vfs_root[1024];

static void derive_vfs_root(const char* eboot)
{
    const char* env = getenv("PS3_VFS_ROOT");
    if (env && *env) { ppu_vfs_root = env; return; }

    strncpy(s_vfs_root, eboot, sizeof s_vfs_root - 1);
    for (char* p = s_vfs_root; *p; p++) if (*p == '\\') *p = '/';
    for (int i = 0; i < 3; i++) { char* s = strrchr(s_vfs_root, '/'); if (s) *s = 0; }
    if (!s_vfs_root[0]) strcpy(s_vfs_root, ".");
    ppu_vfs_root = s_vfs_root;
}

/* ---------------------------------------------------------------------------
 * Flat 4 GB Guest VM
 * -----------------------------------------------------------------------*/
#ifdef _WIN32
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
    ppu_vm_size = 0;
#else
    void* p = mmap(NULL, VM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    vm_base = (p == MAP_FAILED) ? nullptr : (uint8_t*)p;
    ppu_vm_size = 0;
#endif
    return vm_base != nullptr;
}

/* ---------------------------------------------------------------------------
 * Guest execution entry
 * -----------------------------------------------------------------------*/
static void harness_guest_caller(uint32_t opd, uint64_t a0, uint64_t a1,
                                 uint64_t a2, uint64_t a3, uint64_t a4,
                                 uint64_t a5, uint64_t a6, uint64_t a7)
{
    ppu_guest_call(opd, a0, a1, a2, a3, a4, a5, a6, a7);
}

#if defined(__APPLE__)
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
        printf("usage: %s <path/to/decrypted/EBOOT.ELF>\n", argv[0]);
        printf("  Set PS3_VFS_ROOT=/path/to/vfs if VFS is not adjacent to EBOOT.ELF.\n");
        return 2;
    }

#ifdef _WIN32
#pragma comment(lib, "winmm.lib")
    timeBeginPeriod(1);
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("=== Comet Crash (NPUB30061) -- ps3recomp native runner ===\n");

    if (!alloc_guest_vm()) {
        fprintf(stderr, "ERROR: could not allocate the 4 GB guest address space\n");
        return 1;
    }

    uint32_t entry = ppu_load_elf(argv[1]);
    if (!entry) {
        fprintf(stderr, "ERROR: could not load decrypted PPU ELF: %s\n", argv[1]);
        return 1;
    }

    derive_vfs_root(argv[1]);
    printf("[boot] VFS root: %s\n", ppu_vfs_root);

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

    ppu_recomp_register();
    ps3_load_prx_modules();
    ppu_hle_init();
    ppu_sysprx_register();
    ppu_fs_register();
    cellfs_set_root_path(ppu_vfs_root);
    cellfs_add_path_mapping("/dev_hdd0/", ppu_vfs_root);
    cellfs_add_path_mapping("/dev_bdvd/", ppu_vfs_root);
    lv2_init_syscalls();

    g_ps3_guest_caller = harness_guest_caller;
    CreateThread(NULL, 4u * 1024 * 1024, frame_clock, NULL, 0, NULL);

    printf("\n[boot] dispatching entry OPD 0x%08X (stack top 0x%08X)\n\n",
           entry, STACK_TOP);

    g_ppu_main_stack_addr = STACK_TOP - STACK_SIZE;
    g_ppu_main_stack_size = STACK_SIZE;

    int rc = run_guest(entry, STACK_TOP);
    printf("\n[boot] ppu_run returned %d (entry function unwound)\n", rc);

    return g_sys_process_exit_called ? (int)g_sys_process_exit_code : 0;
}
