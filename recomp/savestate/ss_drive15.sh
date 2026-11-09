#!/bin/bash
# ss_drive15.sh -- v2 battery, T2 (in-place load, later timeline):
# match, fly, pause, obs S, SAVE. Resume, play ~60 s (diverge),
# pause, obs D (must differ from S). LOAD -> obs L1 (fidelity vs S),
# admissions around START, 30 s freeze-or-evolve check.
PAD=/tmp/pad_ss.txt
SAVES=$HOME/tools/cc-recomp/saves
LOG=/tmp/ss_t15.log
RES=/tmp/ss_t15_result.txt
press() { printf "%s 8" "$1" > $PAD; sleep 2; printf "" > $PAD; sleep 1; }
state_of() { python3 /tmp/obs.py $PID 2>/dev/null | python3 -c "import json,sys; print(json.load(sys.stdin)['state'])" 2>/dev/null || echo -1; }
acount() { grep -ac "\[cellPad\] PAD_FILE press" $LOG; }
export PAD_STICK="96,0,128,128,100,3000"
export CC_CHEAT_HEALTH=1
export CC_CHEAT_SHIP=1
bash /tmp/cc_launch_ss.sh 99 $LOG
sleep 6
PID=""
for p in $(pgrep -x CometCrashRecom); do
  if tr "\0" "\n" < /proc/$p/environ 2>/dev/null | grep -q "^PAD_FILE=/tmp/pad_ss.txt"; then PID=$p; fi
done
echo "PID=$PID" > $RES
[ -z "$PID" ] && { echo "NO PID" >> $RES; touch /tmp/ss_t15_done.txt; exit 1; }
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
sleep 15
press 0x0008
sleep 2
echo "paused state=$(state_of), admissions=$(acount)" >> $RES
python3 /tmp/obs.py $PID > /tmp/ss15_obs_S.json 2>/dev/null
echo save > $SAVES/trigger
sleep 4
grep "\[SAVE\]" $LOG | tail -1 >> $RES
# resume and diverge ~60 s
press 0x0008
sleep 2
echo "resumed state=$(state_of)" >> $RES
sleep 58
press 0x0008
sleep 2
echo "diverged paused state=$(state_of)" >> $RES
python3 /tmp/obs.py $PID > /tmp/ss15_obs_D.json 2>/dev/null
# the load (later timeline, same process)
echo load > $SAVES/trigger
sleep 5
grep "\[LOAD\]\|savestate resync\|stream table reset" $LOG | tail -4 >> $RES
python3 /tmp/obs.py $PID > /tmp/ss15_obs_L1.json 2>/dev/null
echo "after load: state=$(state_of), admissions=$(acount)" >> $RES
N1=$(acount)
press 0x0008
sleep 2
N2=$(acount)
echo "admissions before/after START: $N1 -> $N2, state=$(state_of)" >> $RES
press 0x0040
sleep 2
echo "after DOWN press: admissions=$(acount), state=$(state_of)" >> $RES
sleep 30
python3 /tmp/obs.py $PID > /tmp/ss15_obs_L2.json 2>/dev/null
echo "final: state=$(state_of), admissions=$(acount), presents: $(grep -ac RSX_PRESENT $LOG)" >> $RES
echo "alive: $(kill -0 $PID 2>/dev/null && echo yes || echo NO)" >> $RES
kill -9 $PID 2>/dev/null
touch /tmp/ss_t15_done.txt
