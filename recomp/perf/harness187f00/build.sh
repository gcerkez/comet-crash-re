#!/bin/bash
# Run inside the build box (iloader). Builds the harness against the workspace lift
# (~/tools/cc-recomp) and the fork (~/tools/ps3recomp), runs it in both call modes, then benches.
set -e
H=/tmp/harness187f; rm -rf $H; mkdir -p $H/spu/spu_job_00187F00
ln -sfn $HOME/tools/ps3recomp/runtime $H/runtime
cp $HOME/tools/cc-recomp/spu/spu_job_00187F00/{spu_recomp.c,spu_recomp.h} $H/spu/spu_job_00187F00/
D=$(dirname "$(readlink -f "$0")")
gcc -O2 -g -I $HOME/tools/ps3recomp/runtime/spu -I $HOME/tools/ps3recomp/include \
  $H/spu/spu_job_00187F00/spu_recomp.c $HOME/tools/ps3recomp/runtime/spu/spu_job187f00.c \
  -x c++ $HOME/tools/cc-recomp/job187f00_glue.cpp -x c $D/harness.c -o $H/harness -lm -lpthread -lstdc++
$H/harness
HARNESS_DIRECT=1 $H/harness
echo "--- bench (trampoline)"; $H/harness bench
echo "--- bench (direct)"; HARNESS_DIRECT=1 $H/harness bench
