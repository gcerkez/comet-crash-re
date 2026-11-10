/* harness.c -- equivalence check for the SPU sprite-sort job native override.
 * Runs the lifted spu_job_00187F00_spu_func_00000160 (pumped through the
 * trampoline like spu_drain_call) and cc_job187f00_compute (native, or its
 * fallback to the lifted function) on identical spu_context copies, then
 * byte-compares the full 256 KB LS and the registers live after the call
 * (gpr[0..1], gpr[80..127]; gpr[2..79] are dead -- see spu_job187f00.c).
 * Build: see README at the bottom of this file's header in the stage dir. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include "spu_context.h"
#include "spu_helpers.h"

void spu_job_00187F00_spu_func_00000160(spu_context *ctx);
void cc_job187f00_compute(spu_context *ctx);

void spu_register_function(uint32_t a, void (*f)(spu_context *)) { (void)a; (void)f; }
void spu_indirect_branch(spu_context *c) { (void)c; }
void spu_indirect_branch_mt(spu_context *c) { (void)c; }
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context *) = 0;
void spu_wrch(spu_context *c, uint32_t ch, u128 v) { (void)c; (void)ch; (void)v; }
void spu_drain_call(spu_context *c, uint32_t r) { (void)c; (void)r; }
void spu_img_restore(spu_context *c, int32_t s) { (void)c; (void)s; }
void spu_stop(spu_context *c) { (void)c; }
int g_spu_ls_watch_n = 0;
volatile int g_spu_direct = 0;
uint8_t g_spu_stack_reset_hint[1024];
void spu_ls_watch_slow(uint32_t a, int w, const uint8_t *p, uint32_t pc, uint32_t lr)
{ (void)a; (void)w; (void)p; (void)pc; (void)lr; }

static uint64_t rng;
static uint32_t rnd32(void)
{
    uint64_t x = rng; x ^= x >> 12; x ^= x << 25; x ^= x >> 27; rng = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1Dull) >> 32);
}
static float rndf(float lo, float hi) { return lo + (hi - lo) * (float)(rnd32() >> 8) / 16777216.0f; }
static void put_f(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); u = __builtin_bswap32(u); memcpy(p, &u, 4); }
static void put_u(uint8_t *p, uint32_t u) { u = __builtin_bswap32(u); memcpy(p, &u, 4); }

/* default layout (game-like): T 32K, E 16K, O 32K, M 64 B -- all disjoint */
typedef struct { uint32_t T, E, O, M; } layout;
static const layout L_DEFAULT = { 0x08000, 0x10000, 0x18000, 0x30000 };

/* z pattern: 0 random, 1 descending (already sorted), 2 ascending (worst),
 * 3 nearly sorted, 4 random bit patterns (NaN/denorm/inf), 5 all equal */
static void setup_ctx(spu_context *c, uint64_t seed, int pattern, layout L, int fill_junk)
{
    memset(c, 0, sizeof *c);
    c->ls = c->ls_store;
    rng = seed;
    for (uint32_t i = 0; i < SPU_LS_SIZE; i += 4) { uint32_t v = rnd32(); memcpy(c->ls + i, &v, 4); }
    (void)fill_junk;
    /* records: first 16 B = position (x,y,z,pad), z carries the key */
    for (uint32_t i = 0; i < 1024; i++) {
        uint8_t *r = c->ls + ((L.T + 32u * i) & SPU_LS_MASK);
        put_f(r + 0, rndf(-50.f, 50.f));
        put_f(r + 4, rndf(-50.f, 50.f));
        float z;
        switch (pattern) {
        case 1: z = 1000.f - (float)i; break;
        case 2: z = (float)i; break;
        case 3: z = 1000.f - (float)i + ((rnd32() % 40 == 0) ? rndf(-30.f, 30.f) : 0.f); break;
        case 5: z = 3.5f; break;
        default: z = rndf(-500.f, 500.f); break;
        }
        if (pattern == 4) { uint32_t u = rnd32(); memcpy(r + 8, &u, 4); }
        else put_f(r + 8, z);
    }
    /* matrix rows: v.z = x*R0.z + y*R1.z + z*R2.z + w*R3.z; pattern 4 gets a
     * full random matrix, others a clean z-extract with a small perturbation */
    float rows[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
    if (pattern != 4) { rows[3][2] = (pattern == 0) ? rndf(-1.f, 1.f) : 0.f; }
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 4; k++) {
            uint8_t *dst = c->ls + ((L.M + 16 * r + 4 * k) & SPU_LS_MASK);
            if (pattern == 4) { uint32_t u = rnd32(); memcpy(dst, &u, 4); }
            else put_f(dst, rows[r][k]);
        }
    for (int r = 0; r < 128; r++) for (int i = 0; i < 4; i++) c->gpr[r]._u32[i] = 0;
    c->gpr[0] = spu_link(0xE8);
    c->steps = g_spu_direct ? 1 : 0;   /* direct tail calls need a nonzero step count */
    for (int i = 0; i < 4; i++) c->gpr[1]._u32[i] = 0x3E000;
    c->gpr[3]._u32[0] = L.M;  c->gpr[3]._u32[1] = 0x1234;
    c->gpr[4]._u32[0] = L.T;
    c->gpr[5]._u32[0] = L.O;
    c->gpr[6]._u32[0] = L.E;
    for (int r = 80; r < 128; r++) for (int i = 0; i < 4; i++) c->gpr[r]._u32[i] = rnd32();
}

static void drain_pump(spu_context *c, uint32_t return_pc)
{
    long guard = 0;
    for (;;) {
        while (g_spu_trampoline_fn) {
            if (g_spu_trampoline_fn == spu_indirect_branch && (c->pc & SPU_LS_MASK) == (return_pc & SPU_LS_MASK)) {
                g_spu_trampoline_fn = 0; return;
            }
            void (*fn)(spu_context *) = g_spu_trampoline_fn;
            g_spu_trampoline_fn = 0;
            fn(c);
            if (++guard > 80000000L) { fprintf(stderr, "pump runaway pc=%05X\n", c->pc & SPU_LS_MASK); exit(3); }
        }
        if ((c->pc & SPU_LS_MASK) == (return_pc & SPU_LS_MASK)) return;
        fprintf(stderr, "pump stalled at pc=%05X\n", c->pc & SPU_LS_MASK); exit(3);
    }
}

static int run_case(uint64_t seed, int pattern, layout L, const char *tag, int expect_native)
{
    spu_context *A = calloc(1, sizeof *A), *B = calloc(1, sizeof *B);
    setup_ctx(A, seed, pattern, L, 0);
    memcpy(B, A, sizeof *B); B->ls = B->ls_store;

    A->host_depth++; spu_job_00187F00_spu_func_00000160(A); drain_pump(A, 0xE8); A->host_depth--;
    B->host_depth++; cc_job187f00_compute(B);               drain_pump(B, 0xE8); B->host_depth--;

    int fail = 0;
    if (memcmp(A->ls, B->ls, SPU_LS_SIZE)) {
        uint32_t i = 0, n = 0;
        while (A->ls[i] == B->ls[i]) i++;
        for (uint32_t k = 0; k < SPU_LS_SIZE; k++) n += A->ls[k] != B->ls[k];
        printf("FAIL %s pat=%d seed=%llx: LS diff first 0x%05X (%u bytes)\n", tag, pattern, (unsigned long long)seed, i, n);
        fail = 1;
    }
    if (memcmp(&A->gpr[0], &B->gpr[0], 2 * sizeof(u128)) || memcmp(&A->gpr[80], &B->gpr[80], 48 * sizeof(u128))) {
        printf("FAIL %s pat=%d: live-register diff\n", tag, pattern); fail = 1;
    }
    if ((A->pc & SPU_LS_MASK) != (B->pc & SPU_LS_MASK)) { printf("FAIL %s: pc %05X vs %05X\n", tag, A->pc, B->pc); fail = 1; }
    (void)expect_native;
    free(A); free(B);
    return fail;
}


static double now_s(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static void bench(void)
{
    static const char *names[] = {"random","descending(sorted)","ascending(worst,capped)","nearly sorted"};
    static const int pats[] = {0,1,2,3};
    spu_context *base = calloc(1, sizeof *base), *w = calloc(1, sizeof *w);
    for (int k = 0; k < 4; k++) {
        setup_ctx(base, 777 + k, pats[k], L_DEFAULT, 0);
        const int reps = 2000;
        double t0 = now_s();
        for (int i = 0; i < reps; i++) {
            memcpy(w->ls_store + 0x8000, base->ls_store + 0x8000, 0x18000);  /* T,E,O region */
            memcpy(w->gpr, base->gpr, sizeof w->gpr); w->ls = w->ls_store; memcpy(w->ls_store + 0x30000, base->ls_store + 0x30000, 0x40);
            w->host_depth = 1; w->steps = g_spu_direct ? 1 : 0;
            spu_job_00187F00_spu_func_00000160(w); drain_pump(w, 0xE8);
        }
        double tl = now_s() - t0; t0 = now_s();
        for (int i = 0; i < reps; i++) {
            memcpy(w->ls_store + 0x8000, base->ls_store + 0x8000, 0x18000);
            memcpy(w->gpr, base->gpr, sizeof w->gpr); w->ls = w->ls_store; memcpy(w->ls_store + 0x30000, base->ls_store + 0x30000, 0x40);
            w->host_depth = 1; w->steps = g_spu_direct ? 1 : 0;
            cc_job187f00_compute(w); drain_pump(w, 0xE8);
        }
        double tn = now_s() - t0;
        printf("%-26s lifted %7.1f us   native %7.1f us   %.2fx\n", names[k], tl/reps*1e6, tn/reps*1e6, tl/tn);
    }
}

int main(int argc, char **argv)
{
    { const char *d = getenv("HARNESS_DIRECT"); g_spu_direct = (d && *d == '1'); printf("mode: %s\n", g_spu_direct ? "direct tail calls" : "trampoline"); }
    if (argc > 1 && !strcmp(argv[1], "bench")) { bench(); return 0; }
    int fails = 0, total = 0;
    uint64_t seed = 0xFEEDFACE12345ull;
    for (int rep = 0; rep < 12; rep++)
        for (int pat = 0; pat <= 5; pat++) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            fails += run_case(seed, pat, L_DEFAULT, "default", 1); total++;
        }
    /* other disjoint placements, incl. unaligned matrix pointer */
    static const layout alt[] = {
        { 0x00000, 0x10000, 0x20000, 0x3F000 },
        { 0x20000, 0x08000, 0x00000, 0x3FFC0 },
        { 0x18000, 0x00000, 0x20000, 0x30004 },   /* M unaligned */
        { 0x00000, 0x0C000, 0x10000, 0x3FFF8 },   /* M unaligned, near top */
    };
    for (unsigned a = 0; a < sizeof alt / sizeof *alt; a++)
        for (int pat = 0; pat <= 5; pat++) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            fails += run_case(seed, pat, alt[a], "alt", 1); total++;
        }
    /* overlapping / misaligned: native must decline, result must still match */
    static const layout bad[] = {
        { 0x08000, 0x0C000, 0x18000, 0x30000 },   /* E inside T */
        { 0x08000, 0x10000, 0x10000, 0x30000 },   /* O == E */
        { 0x08004, 0x10000, 0x18000, 0x30000 },   /* T misaligned */
        { 0x08000, 0x10000, 0x18000, 0x08010 },   /* M inside T */
        { 0x3E000, 0x10000, 0x18000, 0x30000 },   /* T past end of LS */
    };
    for (unsigned a = 0; a < sizeof bad / sizeof *bad; a++)
        for (int pat = 0; pat <= 2; pat++) {
            seed = seed * 6364136223846793005ull + 1442695040888963407ull;
            fails += run_case(seed, pat, bad[a], "fallback", 0); total++;
        }
    printf("%s: %d/%d cases failed\n", fails ? "FAIL" : "PASS", fails, total);
    return fails != 0;
}
