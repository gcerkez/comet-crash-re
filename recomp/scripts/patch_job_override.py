#!/usr/bin/env python3
"""patch_job_override.py -- JOB187280 OVERRIDE (notes 050).

Redirects the generated lift's work-function call for SPURS job
0x187280 to the native host override, and adds the title glue TU to
the workspace build. Two edits, each independently idempotent, every
anchor asserted before any file is written:

  1. spu/spu_job_00187280/spu_recomp.c (generated -- re-run this after
     any SPU re-lift):
       - declare cc_job187280_compute() after the header include
       - the call inside func_0000029C:
           spu_job_00187280_spu_func_000004A0(ctx);
         becomes
           cc_job187280_compute(ctx);
         (the surrounding host_depth++/spu_drain_call/spu_img_restore
         sandwich is untouched)
  2. CMakeLists.txt: add job187280_glue.cpp to the executable sources
     (the glue file itself is a workspace source, copied separately).

Usage: patch_job_override.py [workspace-dir]   (default ~/tools/cc-recomp)
"""
import sys
from pathlib import Path

MARKER = "JOB187280 OVERRIDE"


def main() -> int:
    ws = Path(sys.argv[1] if len(sys.argv) > 1 else "~/tools/cc-recomp").expanduser()
    job_c = ws / "spu" / "spu_job_00187280" / "spu_recomp.c"
    cmake = ws / "CMakeLists.txt"

    # ---- edit 1: generated job TU ----
    src = job_c.read_text()
    if MARKER in src:
        print(f"{job_c}: already patched")
        new_src = src
    else:
        call = "spu_job_00187280_spu_func_000004A0(ctx);"
        assert src.count(call) == 1, f"call anchor count {src.count(call)} != 1"
        inc = '#include "spu_recomp.h"'
        assert src.count(inc) == 1, f"include anchor count {src.count(inc)} != 1"
        new_src = src.replace(
            inc,
            inc + "\nextern void cc_job187280_compute(spu_context* ctx);"
                  " /* " + MARKER + " */",
            1,
        )
        new_src = new_src.replace(call, "cc_job187280_compute(ctx);", 1)
        assert new_src.count("cc_job187280_compute(ctx);") == 1
        print(f"{job_c}: call site redirected")

    # ---- edit 2: workspace CMakeLists ----
    cm = cmake.read_text()
    if "job187280_glue.cpp" in cm:
        print(f"{cmake}: glue already listed")
        new_cm = cm
    else:
        anchor = "    main.cpp\n    stubs.cpp\n"
        assert cm.count(anchor) == 1, f"cmake anchor count {cm.count(anchor)} != 1"
        new_cm = cm.replace(anchor, anchor + "    job187280_glue.cpp\n", 1)
        print(f"{cmake}: glue TU added")

    # ---- write only after every assert passed ----
    if new_src != src:
        job_c.write_text(new_src)
    if new_cm != cm:
        cmake.write_text(new_cm)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
