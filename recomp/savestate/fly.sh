# NOTE: file-based PAD_STICK does NOT work on this fork build - PAD_STICK is an
# env VALUE parsed once by cellPad.c (see notes/051). Kept for the record only.
#!/bin/bash
# Host-side: hold the LEFT stick deflected for N seconds by refreshing
# PAD_STICK with a sliding [from,until] window (seconds since pad init,
# estimated as now - launch_epoch - FUDGE). args: <tag> <lx> <ly> <secs> [fudge]
W=$HOME/cc-savestate-work; TAG=$1; LX=$2; LY=$3; SECS=$4; FUDGE=${5:-6}
EPOCH=$(cat $W/$TAG.epoch)
END=$(( $(date +%s) + SECS ))
while [ $(date +%s) -lt $END ]; do
  EL=$(( $(date +%s) - EPOCH - FUDGE ))
  FROM=$(( EL - 10 )); [ $FROM -lt 0 ] && FROM=0
  UNTIL=$(( EL + 30 ))
  printf "%s,%s,128,128,%s,%s" "$LX" "$LY" "$FROM" "$UNTIL" > $W/stick_$TAG.txt
  sleep 4
done
: > $W/stick_$TAG.txt
echo "fly done tag=$TAG lx=$LX ly=$LY"
