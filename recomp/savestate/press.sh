#!/bin/bash
# args: <tag> <mask> [count]
W=$HOME/cc-savestate-work
printf "%s %s" "$2" "${3:-8}" > $W/pad_$1.txt
sleep 2
printf "" > $W/pad_$1.txt
sleep 1
echo "pressed $2 x${3:-8} tag=$1"
