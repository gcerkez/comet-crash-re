#!/bin/bash
python3 /home/gcerkez/comet-crash-re/recomp/savestate/obs.py $1 2>/dev/null | python3 -c "import json,sys;d=json.load(sys.stdin);print('st=%s mode=%s mission=%s life=%s'%(d['state'],d['mode'],d['mission'],[s['life'] for s in d['slots']]))" 2>/dev/null || echo st=?
