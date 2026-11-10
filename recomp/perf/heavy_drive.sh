#!/bin/bash
# Heavy Battle drive + fps log + perf sample.
# Usage: heavy_drive.sh <build-dir-name> <tag> [match_seconds] [KEY=VAL env for the game...]
# Env: PERF_AT=<s> wraps the game in perf record (cpu-clock) starting <s> seconds into the match;
#      STICK_FROM=<s> when the held right stick starts; VKWIN=1 opens a window (Xvfb -> llvmpipe: do not measure);
#      CC_PERF_DIR output root (default ~/cc-perf-work). Results: runs/<tag>/{fps.txt,match.txt,game.log,perf.data}.
# fps.txt columns: seconds, last RSX_PRESENT frame, process CPU jiffies, guest state, draws.
# Runs on the host; game runs in the iloader box under Xvfb.
BUILD=${1:?build dir}; TAG=${2:?tag}; MSECS=${3:-200}; shift 3 2>/dev/null
EXTRA="$*"
HERE=$(dirname "$(readlink -f "$0")")
S=${CC_PERF_DIR:-$HOME/cc-perf-work}
OUT=$S/runs/$TAG; mkdir -p $OUT
D=${DISP:-151}
LOG=$OUT/game.log
PAD1=$OUT/pad1.txt; PAD2=$OUT/pad2.txt
: > $PAD1; : > $PAD2; : > $LOG; rm -f $OUT/pid $OUT/done $OUT/fps.txt $OUT/match.txt

PERFWRAP=""; [ -n "$PERF_AT" ] && PERFWRAP="perf record -e cpu-clock -F ${PERF_HZ:-299} ${PERF_G:-} -D $(( (PERF_AT+75)*1000 )) -o $OUT/perf.data --"
cat > $OUT/launch.sh <<EOF
#!/bin/bash
pkill -f "Xvfb :$D " 2>/dev/null; sleep 1
Xvfb :$D -screen 0 1920x1080x24 > $OUT/xvfb.log 2>&1 &
sleep 2
cd ~/tools/cc-recomp
export DISPLAY=:$D PS3_VERBOSE=0 VK_DRAW_BATCH=1 PS3RECOMP_VK_GUEST_PROGRAMS=1 \
 PS3RECOMP_SPIRV_CACHE=/tmp/spirv_cache PS3RECOMP_VK_WINDOW=${VKWIN:-0} MSGDIALOG_ANSWER=no \
 PAD_DEBUG_LOG=0 PAD_FILE=$PAD1 PAD_FILE2=$PAD2 CC_CAM_DIAG=0 PS3RECOMP_VP_DIAG=0 \
 PS3_VIDEO_RES=1080 PAD_STICK_BOOST=1.25 PS3_VFS_ROOT=\$HOME/tools/cc-recomp/vfs \
 CC_CHEAT_THORIUM=1 CC_CHEAT_STOCKPILES=1 CC_CHEAT_HEALTH=1 \
 PAD_STICK="128,128,255,128,${STICK_FROM:-100},$((MSECS + 400))" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11
for kv in $EXTRA; do export "\$kv"; done
setsid $PERFWRAP ./$BUILD/CometCrashRecomp \$HOME/Downloads/comet_crash_extracted/EBOOT.ELF > $LOG 2>&1 &
sleep 3; pgrep -n -x CometCrashRecom > $OUT/pid
EOF
setsid distrobox enter iloader -- bash $OUT/launch.sh > $OUT/launch.out 2>&1 &
for i in $(seq 1 60); do [ -s $OUT/pid ] && break; sleep 1; done
PID=$(cat $OUT/pid)
ST=$HERE/st.sh
for i in $(seq 1 60); do case "$($ST $PID)" in st=11*) break;; esac; sleep 2; done
sleep 8
press() { printf "%s 2" "$2" > "$1"; sleep 1; printf "" > "$1"; sleep 1.5; }
DOWN=0x0040; CROSS=0x4000; START=0x0008
press $PAD1 $DOWN; press $PAD1 $DOWN
press $PAD1 $CROSS; press $PAD1 $CROSS; press $PAD1 $CROSS; sleep 1
press $PAD1 $START; press $PAD2 $START
press $PAD1 $CROSS; press $PAD2 $CROSS
for i in $(seq 1 15); do case "$($ST $PID)" in st=13\ mode=2*) break;; esac; sleep 2; done
echo "match: $($ST $PID)" > $OUT/match.txt
PID=$(cat $OUT/pid 2>/dev/null)
echo "pid=$PID" > $OUT/summary.txt
# fps samples: every 10 s, latest RSX_PRESENT frame number + process CPU jiffies
t0=$(date +%s)
frame() { grep -a "RSX_PRESENT\] frame" $LOG | tail -1 | sed -E 's/.*frame ([0-9]+):.*/\1/'; }
obsline() { python3 /home/gcerkez/comet-crash-re/recomp/savestate/obs.py $PID 2>/dev/null | python3 -c "import json,sys;d=json.load(sys.stdin);print('st=%s mode=%s ents=%s'%(d.get('state'),d.get('mode'),sum(b.get('total_typed',0) for b in d['banks'])))" 2>/dev/null || echo st=?; }
cpu() { awk '{print $14+$15}' /proc/$PID/stat 2>/dev/null; }
while [ $(( $(date +%s) - t0 )) -lt $MSECS ]; do
  echo "$(( $(date +%s) - t0 )) $(frame) $(cpu) $(obsline) $(grep -a "RSX_PRESENT\] frame" $LOG | tail -1 | grep -o "guest_draws=[0-9]*")" >> $OUT/fps.txt
  el=$(( $(date +%s) - t0 ))
  sleep 10
done
DISPLAY=:$D import -window root $OUT/shot.png 2>/dev/null
wait
kill -INT $PID 2>/dev/null; sleep 8; kill -9 $PID 2>/dev/null; sleep 3
for P in $(pgrep -x Xvfb); do
  case "$(tr '\0' ' ' < /proc/$P/cmdline)" in *":$D "*) kill -9 $P;; esac
done
touch $OUT/done
