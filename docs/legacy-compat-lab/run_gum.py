from pathlib import Path
import os
import subprocess
import json
import signal
import hashlib

lab=Path(__file__).resolve().parent
env=os.environ.copy()
env.update(WINEPREFIX=str(lab/'wine-prefix'), WINEDEBUG='-all',
           WINEDLLOVERRIDES='winemenubuilder.exe=d', DISPLAY='',
           XDG_CACHE_HOME=str(lab/'cache'))
guest=lab/'guest_fields.dll'
before=hashlib.sha256(guest.read_bytes()).hexdigest()
results=[]
for mode in ['--init','--stalk','--translate','--seh-native','--seh','--threads']:
    cmd=['wine',str(lab/'gum_probe.exe'),mode]
    path=lab/('gum'+mode+'.log')
    with path.open('w') as log:
        proc=subprocess.Popen(cmd,cwd=lab,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            code=proc.wait(timeout=20)
            item={'mode':mode,'returncode':code}
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid,signal.SIGKILL)
            proc.wait()
            item={'mode':mode,'timeout':20}
    item['output']=path.read_text(errors='replace')
    results.append(item)
    (lab/'gum-results.json').write_text(json.dumps({'guest_sha256_before':before,
        'guest_sha256_after':hashlib.sha256(guest.read_bytes()).hexdigest(),
        'tests':results},indent=2))
    print(json.dumps(item),flush=True)
