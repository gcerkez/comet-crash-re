#!/bin/bash
# ss_drive16.sh -- T1X: phase-1 mechanism (external SIGSTOP restore, NO
# resyncs, NO glue) of the drive-14 in-glue save file, into a fresh
# build-savestate session paused in its own Kreios 1 match.
PAD=/tmp/pad_ss.txt
LOG=/tmp/ss_t16.log
RES=/tmp/ss_t16_result.txt
press() { printf "%s 8" "$1" > $PAD; sleep 2; printf "" > $PAD; sleep 1; }
state_of() { python3 /tmp/obs.py $PID 2>/dev/null | python3 -c "import json,sys; print(json.load(sys.stdin)['state'])" 2>/dev/null || echo -1; }
acount() { grep -ac "\[cellPad\] PAD_FILE press" $LOG; }
export PAD_STICK="96,0,128,128,400,3000"
export CC_CHEAT_HEALTH=1
export CC_CHEAT_SHIP=1
bash /tmp/cc_launch_ss.sh 99 $LOG
sleep 6
PID=""
for p in $(pgrep -x CometCrashRecom); do
  if tr "\0" "\n" < /proc/$p/environ 2>/dev/null | grep -q "^PAD_FILE=/tmp/pad_ss.txt"; then PID=$p; fi
done
echo "PID=$PID" > $RES
[ -z "$PID" ] && { echo "NO PID" >> $RES; touch /tmp/ss_t16_done.txt; exit 1; }
sleep 40
press 0x0040
press 0x4000
sleep 3
press 0x4000
sleep 2
press 0x4000
sleep 2
press 0x4000
sleep 28
press 0x4000
for i in $(seq 1 12); do
  S=$(state_of); [ "$S" = "13" ] && break; press 0x4000; sleep 5
done
echo "match state=$S" >> $RES
press 0x0008
sleep 2
echo "paused state=$(state_of), admissions before=$(acount)" >> $RES
python3 /tmp/load_ccsv.py $PID /tmp/ss14_slot1_keep.ccsave >> $RES 2>&1
sleep 3
python3 /tmp/obs.py $PID > /tmp/ss16_obs_L1.json 2>/dev/null
echo "post-load state=$(state_of)" >> $RES
N1=$(acount)
press 0x0008
sleep 2
N2=$(acount)
echo "admissions before/after START: $N1 -> $N2" >> $RES
echo "state after START=$(state_of) (13 = resumed)" >> $RES
P1=$(grep -ac RSX_PRESENT $LOG)
sleep 55
python3 /tmp/obs.py $PID > /tmp/ss16_obs_L2.json 2>/dev/null
P2=$(grep -ac RSX_PRESENT $LOG)
echo "final state=$(state_of), presents $P1 -> $P2" >> $RES
echo "alive: $(kill -0 $PID 2>/dev/null && echo yes || echo NO)" >> $RES
kill -9 $PID 2>/dev/null
touch /tmp/ss_t16_done.txt
