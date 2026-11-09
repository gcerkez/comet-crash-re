#!/bin/bash
# ss_drive14.sh -- v2 battery, T1 (leg-4 in-glue) + T4 (refusals):
# A: match, fly, pause, SAVE (v2 build). Kill.
# B: fresh boot, own-flow match (no flight), pause.
#    T4a: load with NO file -> refused, process responsive.
#    T4b: load with CORRUPT file -> refused pre-stop, responsive.
#    T1: load A's save -> fidelity vs S, START admission advances,
#        state 13, ~55 s census/byte-sum evolution, presents grow.
PAD=/tmp/pad_ss.txt
SAVES=$HOME/tools/cc-recomp/saves
RES=/tmp/ss_t14_result.txt
press() { printf "%s 8" "$1" > $PAD; sleep 2; printf "" > $PAD; sleep 1; }
state_of() { python3 /tmp/obs.py $PID 2>/dev/null | python3 -c "import json,sys; print(json.load(sys.stdin)['state'])" 2>/dev/null || echo -1; }
acount() { grep -ac "\[cellPad\] PAD_FILE press" $1; }
find_pid() {
  PID=""
  for p in $(pgrep -x CometCrashRecom); do
    if tr "\0" "\n" < /proc/$p/environ 2>/dev/null | grep -q "^PAD_FILE=/tmp/pad_ss.txt"; then PID=$p; fi
  done
}
to_match() {
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
}
: > $RES
# ---------- session A ----------
export PAD_STICK="96,0,128,128,100,3000"
export CC_CHEAT_HEALTH=1
export CC_CHEAT_SHIP=1
bash /tmp/cc_launch_ss.sh 99 /tmp/ss_t14a.log
sleep 6; find_pid
echo "A PID=$PID" >> $RES
[ -z "$PID" ] && { echo "A NO PID" >> $RES; touch /tmp/ss_t14_done.txt; exit 1; }
to_match
echo "A match state=$S" >> $RES
sleep 20
press 0x0008
sleep 2
echo "A paused state=$(state_of)" >> $RES
python3 /tmp/obs.py $PID > /tmp/ss14_obs_S.json 2>/dev/null
echo save > $SAVES/trigger
sleep 4
grep "\[SAVE\]" /tmp/ss_t14a.log | tail -1 >> $RES
kill -9 $PID 2>/dev/null
sleep 2
cp $SAVES/slot1.ccsave /tmp/ss14_slot1_keep.ccsave
# ---------- session B ----------
export PAD_STICK="96,0,128,128,400,3000"
export AUDIO_WAV=/tmp/ss_t14.wav
export PS3_CODEC_LOG=1
rm -f /tmp/ss_t14.wav
bash /tmp/cc_launch_ss.sh 99 /tmp/ss_t14b.log
sleep 6; find_pid
echo "B PID=$PID" >> $RES
[ -z "$PID" ] && { echo "B NO PID" >> $RES; touch /tmp/ss_t14_done.txt; exit 1; }
to_match
echo "B match state=$S" >> $RES
press 0x0008
sleep 2
echo "B paused state=$(state_of)" >> $RES
python3 /tmp/obs.py $PID > /tmp/ss14_obs_B0.json 2>/dev/null
# ---- T4a: no save file ----
mv $SAVES/slot1.ccsave $SAVES/slot1.hidden
echo load > $SAVES/trigger
sleep 3
echo "T4a line: $(grep '\[LOAD\]' /tmp/ss_t14b.log | tail -1)" >> $RES
N1=$(acount /tmp/ss_t14b.log)
press 0x0040
N2=$(acount /tmp/ss_t14b.log)
echo "T4a responsive: admissions $N1 -> $N2, state=$(state_of)" >> $RES
# ---- T4b: corrupt save file ----
head -c 100000 /tmp/ss14_slot1_keep.ccsave > $SAVES/slot1.ccsave
echo load > $SAVES/trigger
sleep 3
echo "T4b line: $(grep '\[LOAD\]' /tmp/ss_t14b.log | tail -1)" >> $RES
N1=$(acount /tmp/ss_t14b.log)
press 0x0040
N2=$(acount /tmp/ss_t14b.log)
echo "T4b responsive: admissions $N1 -> $N2, state=$(state_of)" >> $RES
# ---- T1: the real load ----
mv $SAVES/slot1.hidden $SAVES/slot1.ccsave
echo load > $SAVES/trigger
sleep 5
grep "\[LOAD\]\|savestate resync\|stream table reset" /tmp/ss_t14b.log | tail -4 >> $RES
python3 /tmp/obs.py $PID > /tmp/ss14_obs_L1.json 2>/dev/null
echo "T1 post-load state=$(state_of)" >> $RES
N1=$(acount /tmp/ss_t14b.log)
press 0x0008
sleep 2
N2=$(acount /tmp/ss_t14b.log)
echo "T1 admissions before/after START: $N1 -> $N2" >> $RES
echo "T1 state after START=$(state_of) (13 = resumed)" >> $RES
P1=$(grep -ac RSX_PRESENT /tmp/ss_t14b.log)
sleep 55
python3 /tmp/obs.py $PID > /tmp/ss14_obs_L2.json 2>/dev/null
P2=$(grep -ac RSX_PRESENT /tmp/ss_t14b.log)
echo "T1 final state=$(state_of), presents $P1 -> $P2" >> $RES
echo "T1 wav bytes final: $(stat -c %s /tmp/ss_t14.wav 2>/dev/null || echo 0)" >> $RES
echo "alive: $(kill -0 $PID 2>/dev/null && echo yes || echo NO)" >> $RES
kill -9 $PID 2>/dev/null
touch /tmp/ss_t14_done.txt
