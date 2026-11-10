// job187f00_glue.cpp -- title glue for the SPU sprite depth-sort job override.
//
// The generated lift for SPURS job 0x187F00 calls cc_job187f00_compute() at
// the (patched) call site in func_00000054, in place of
// spu_job_00187F00_spu_func_00000160 (the work function: depth keys, capped
// gnome sort, gather). The native version lives in the fork runtime
// (runtime/spu/spu_job187f00.c); it declines (returns 0) when the job's
// buffers overlap, and then the lifted function runs unchanged.
//
//   PS3_NO_JOB_OVERRIDE=1   shared kill switch for the job overrides
//   PS3_NO_JOB187F00=1      this override only (A/B)
//   CC_JOB187F00_VERIFY=1   run native on the live context AND the lifted
//                           function on a private copy of the pre-call
//                           context; compare every LS byte, report mismatches
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "spu_context.h"

extern "C" void spu_job_00187F00_spu_func_00000160(spu_context *ctx);
extern "C" int ps3_job187f00_compute_native(spu_context *ctx);

static bool env_set(const char *n)
{
    const char *e = std::getenv(n);
    return e != nullptr && e[0] != '\0';
}

/* Pump the trampoline chain to the return point, as the caller's
 * spu_drain_call(ctx, return_pc) does for the lifted function. */
static bool pump(spu_context *c, uint32_t return_pc)
{
    for (long guard = 0; guard < 20000000L; guard++) {
        if (g_spu_trampoline_fn) {
            if (g_spu_trampoline_fn == spu_indirect_branch &&
                (c->pc & SPU_LS_MASK) == (return_pc & SPU_LS_MASK)) {
                g_spu_trampoline_fn = 0;
                return true;
            }
            void (*fn)(spu_context *) = g_spu_trampoline_fn;
            g_spu_trampoline_fn = 0;
            fn(c);
            continue;
        }
        return (c->pc & SPU_LS_MASK) == (return_pc & SPU_LS_MASK);
    }
    return false;
}

static void native_exit_state(spu_context *ctx)
{
    /* Exit state the lifted chain leaves for the caller's
     * spu_drain_call(ctx, 0xE8): the final SPU_RET publishes the link
     * register as the pc and the trampoline slot is empty. */
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = 0;
}

static void verify_call(spu_context *ctx)
{
    static thread_local spu_context *shadow = nullptr;
    static unsigned long long calls, declined, bad;
    if (!shadow) {
        shadow = (spu_context *)std::calloc(1, sizeof(spu_context));
        if (!shadow) { spu_job_00187F00_spu_func_00000160(ctx); return; }
    }
    std::memcpy(shadow, ctx, sizeof(spu_context));   /* includes ls_store */
    shadow->ls = shadow->ls_store;                   /* not ctx's store */

    auto saved = g_spu_trampoline_fn;
    shadow->host_depth = ctx->host_depth + 1;
    spu_job_00187F00_spu_func_00000160(shadow);
    const bool ok = pump(shadow, ctx->gpr[0]._u32[0]);
    g_spu_trampoline_fn = saved;

    const int handled = ps3_job187f00_compute_native(ctx);
    calls++;
    if (!handled) {
        declined++;
        /* run the lifted function on the live context too */
        spu_job_00187F00_spu_func_00000160(ctx);
        return;
    }
    native_exit_state(ctx);
    if (!ok || std::memcmp(shadow->ls_store, ctx->ls, SPU_LS_SIZE) != 0) {
        bad++;
        unsigned first = 0;
        while (first < SPU_LS_SIZE && shadow->ls_store[first] == ctx->ls[first]) first++;
        std::fprintf(stderr, "[job187f00] VERIFY MISMATCH (pump %s) first diff LS 0x%05X (%llu bad / %llu calls)\n",
                     ok ? "ok" : "FAILED", first, bad, calls);
    }
    if ((calls % 20000ull) == 0)
        std::fprintf(stderr, "[job187f00] VERIFY %llu calls, %llu declined, %llu bad\n", calls, declined, bad);
}

extern "C" void cc_job187f00_compute(spu_context *ctx)
{
    static const bool s_off    = env_set("PS3_NO_JOB_OVERRIDE") || env_set("PS3_NO_JOB187F00");
    static const bool s_verify = env_set("CC_JOB187F00_VERIFY");
    if (s_off) {
        /* The lifted function drives the trampoline chain itself; the
         * caller's spu_drain_call pumps it to the return point. */
        spu_job_00187F00_spu_func_00000160(ctx);
        return;
    }
    if (s_verify) { verify_call(ctx); return; }
    if (!ps3_job187f00_compute_native(ctx)) {
        spu_job_00187F00_spu_func_00000160(ctx);
        return;
    }
    native_exit_state(ctx);
}
