#!/bin/bash
# Runs INSIDE the iloader distrobox: Xvfb + game. args: <display> <tag>
DISP=$1; TAG=$2
W=$HOME/cc-savestate-work
export DISPLAY=:$DISP
Xvfb :$DISP -screen 0 1920x1080x24 > $W/xvfb_$TAG.log 2>&1 &
sleep 2
: > $W/pad_$TAG.txt
: > $W/stick_$TAG.txt
date +%s > $W/$TAG.epoch
cd $HOME/tools/cc-recomp
PS3_VERBOSE=0 VK_DRAW_BATCH=1 PS3RECOMP_VK_GUEST_PROGRAMS=1 PS3RECOMP_SPIRV_CACHE=/tmp/spirv_cache PS3RECOMP_VK_WINDOW=1 MSGDIALOG_ANSWER=no PAD_DEBUG_LOG=0 CC_CAM_DIAG=0 CC_LOCK_ZOOM=0 CC_FORCE_OVERVIEW=0 CC_FORCE_FOLLOW=0 PS3RECOMP_VP_DIAG=1 PS3_VIDEO_RES=1080 PAD_STICK_BOOST=1.25 PS3_VFS_ROOT=$HOME/tools/cc-recomp/vfs VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json PAD_FILE=$W/pad_$TAG.txt PAD_STICK=${SS_STICK:-128,128,128,128} AUDIO_WAV=$W/$TAG.wav ./build-batch/CometCrashRecomp $HOME/Downloads/comet_crash_extracted/EBOOT.ELF > $W/$TAG.log 2>&1 &
echo $! > $W/$TAG.pid
sleep 1
echo "launched tag=$TAG pid=$(cat $W/$TAG.pid)"
