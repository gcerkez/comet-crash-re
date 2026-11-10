#!/bin/bash
# inside iloader: build harness against a mutated copy of the native TU
set -e
H=/tmp/harness187f; N=$HOME/tools/ps3recomp/runtime/spu/spu_job187f00.c
D=$(dirname "$(readlink -f "$0")")
run() { # name sed-expr
  sed "$2" $N > $H/mut.c
  cmp -s $N $H/mut.c && { echo "$1: MUTATION DID NOT APPLY"; return; }
  gcc -O2 -I $HOME/tools/ps3recomp/runtime/spu -I $HOME/tools/ps3recomp/include \
    $H/spu/spu_job_00187F00/spu_recomp.c $H/mut.c -x c++ $HOME/tools/cc-recomp/job187f00_glue.cpp -x c $D/harness.c -o $H/hmut -lm -lpthread -lstdc++
  out=$($H/hmut | tail -1); echo "$1: $out"
}
run "cap 10240->10000"       's/cnt = 10240/cnt = 10000/'
run "swap > becomes >=(via swapped operands)" 's/spu_fcgt(ka, kb)/spu_fcgt(kb, ka)/'
run "wrong halfword (i<<16 -> i<<15)" 's/(i << 16) | (orig/(i << 15) | (orig/'
run "gather index off by one"  's/ls + T + 32u \* src/ls + T + 32u * ((src + 1) \& 1023u)/'
run "keep orig w1 low half cleared" 's/(orig._u32\[1\] \& 0xFFFFu)/0/'
run "z from wrong row" 's/R2z, pos._u32\[2\]/R2z, pos._u32[1]/'
