// job187280_glue.cpp -- title glue for the SPU geometry-job override.
//
// The generated lift for SPURS job 0x187280 calls cc_job187280_compute()
// at the (patched) work-function call site inside func_0000029C, in
// place of spu_job_00187280_spu_func_000004A0. The default is the
// native host implementation in the fork runtime
// (runtime/spu/spu_job187280.c), which fans the job's per-element work
// across the host's cores. PS3_NO_JOB_OVERRIDE=1 restores the lifted
// function exactly (kill switch).
#include <cstdlib>
#include "spu_context.h"

extern "C" void spu_job_00187280_spu_func_000004A0(spu_context *ctx);
extern "C" void ps3_job187280_compute_native(spu_context *ctx);

extern "C" void cc_job187280_compute(spu_context *ctx)
{
    static const bool s_no_override = [] {
        const char *e = std::getenv("PS3_NO_JOB_OVERRIDE");
        return e != nullptr && e[0] != '\0';
    }();
    if (s_no_override) {
        /* The lifted function drives the trampoline chain itself; the
         * caller's spu_drain_call pumps it to the return point. */
        spu_job_00187280_spu_func_000004A0(ctx);
        return;
    }
    ps3_job187280_compute_native(ctx);
    /* Exit state, exactly what the lifted chain leaves behind for the
     * caller's spu_drain_call(ctx, 0x2B0): its final SPU_RET publishes
     * the link register as the pc and leaves the trampoline slot empty
     * (the drain cleared it before the last hop). Reproduce both so
     * the drain sees its return condition and exits immediately. */
    ctx->pc = ctx->gpr[0]._u32[0];
    g_spu_trampoline_fn = 0;
}
