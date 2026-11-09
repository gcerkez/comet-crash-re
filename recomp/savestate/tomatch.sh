#!/bin/bash
# Host-side: drive pad file through the established llvmpipe Campaign cadence.
W=$HOME/cc-savestate-work; TAG=$1; PADF=$W/pad_$TAG.txt
press(){ printf "%s 8" "$1" > "$PADF"; sleep 2; printf "" > "$PADF"; sleep 1; }
sleep 40
press 0x0040; sleep 2
press 0x4000; sleep 5
press 0x4000; sleep 4
press 0x4000; sleep 4
press 0x4000; sleep 30
press 0x4000; sleep 25
touch $W/tomatch_$TAG.done
echo "tomatch done tag=$TAG"
