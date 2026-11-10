#!/usr/bin/env python3
"""patch_job187f00_override.py -- JOB187F00 OVERRIDE (RUNTIME_PATCHES #35).

Redirects the generated lift's work-function call for SPURS job 0x187F00
(the sprite depth-sort job) to the native host override, and adds the title
glue TU to the workspace build. Same shape as patch_job_override.py: two
independently idempotent edits, every anchor asserted before any file is
written.

  1. spu/spu_job_00187F00/spu_recomp.c (generated -- re-run after any SPU
     re-lift): the call inside func_00000054
         spu_job_00187F00_spu_func_00000160(ctx);
     becomes
         cc_job187f00_compute(ctx);
     (the host_depth++/spu_drain_call/spu_img_restore sandwich is untouched).
  2. CMakeLists.txt: add job187f00_glue.cpp to the executable sources.
     The glue file is a workspace source (recomp/job187f00_glue.cpp in this
     repo), copied next to CMakeLists.txt here if it is missing.

Usage: patch_job187f00_override.py [workspace-dir]   (default ~/tools/cc-recomp)
"""
import shutil
import sys
from pathlib import Path

MARKER = "JOB187F00 OVERRIDE"


def main() -> int:
    ws = Path(sys.argv[1] if len(sys.argv) > 1 else "~/tools/cc-recomp").expanduser()
    job_c = ws / "spu" / "spu_job_00187F00" / "spu_recomp.c"
    cmake = ws / "CMakeLists.txt"
    glue_src = Path(__file__).resolve().parent.parent / "job187f00_glue.cpp"
    glue_dst = ws / "job187f00_glue.cpp"

    src = job_c.read_text()
    if MARKER in src:
        print(f"{job_c}: already patched")
        new_src = src
    else:
        call = "spu_job_00187F00_spu_func_00000160(ctx);"
        assert src.count(call) == 1, f"call anchor count {src.count(call)} != 1"
        inc = '#include "spu_recomp.h"'
        assert src.count(inc) == 1, f"include anchor count {src.count(inc)} != 1"
        new_src = src.replace(
            inc,
            inc + "\nextern void cc_job187f00_compute(spu_context* ctx);"
                  " /* " + MARKER + " */",
            1,
        )
        new_src = new_src.replace(call, "cc_job187f00_compute(ctx);", 1)
        assert new_src.count("cc_job187f00_compute(ctx);") == 1
        print(f"{job_c}: call site redirected")

    cm = cmake.read_text()
    if "job187f00_glue.cpp" in cm:
        print(f"{cmake}: glue already listed")
        new_cm = cm
    else:
        anchor = "    main.cpp\n    stubs.cpp\n"
        assert cm.count(anchor) == 1, f"cmake anchor count {cm.count(anchor)} != 1"
        new_cm = cm.replace(anchor, anchor + "    job187f00_glue.cpp\n", 1)
        print(f"{cmake}: glue TU added")

    if not glue_dst.exists():
        assert glue_src.exists(), f"missing {glue_src}"
        shutil.copyfile(glue_src, glue_dst)
        print(f"{glue_dst}: copied")
    if new_src != src:
        job_c.write_text(new_src)
    if new_cm != cm:
        cmake.write_text(new_cm)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
