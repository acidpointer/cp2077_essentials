from pathlib import Path
import subprocess
import os
import json
import signal

lab=Path(__file__).resolve().parent
env=os.environ.copy()
env.update(WINEPREFIX=str(lab/'wine-prefix'), WINEDEBUG='-all',
           WINEDLLOVERRIDES='winemenubuilder.exe=d', DISPLAY='',
           XDG_CACHE_HOME=str(lab/'cache'))
def win(p): return 'Z:'+str(p).replace('/','\\')
(lab/'dr-config').mkdir(exist_ok=True)
env['DYNAMORIO_CONFIGDIR']=win(lab/'dr-config')
base=['wine',str(lab/'dynamorio/bin64/drrun.exe'),'-root',win(lab/'dynamorio'),
      '-no_follow_children']
results=[]
for mode,args in [('no-client',[]),('empty-client',['-c',win(lab/'dynamorio/samples/bin64/empty.dll')])]:
    cmd=base+args+['--',win(lab/'callback_bridge.exe')]
    with (lab/('backend-'+mode+'.log')).open('w') as log:
        proc=subprocess.Popen(cmd,cwd=lab,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            code=proc.wait(timeout=30)
            item={'mode':mode,'returncode':code,'command':cmd}
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid,signal.SIGKILL)
            proc.wait()
            item={'mode':mode,'timeout':30,'command':cmd}
    item['output']=(lab/('backend-'+mode+'.log')).read_text(errors='replace')
    results.append(item)
    (lab/'backend-results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(item),flush=True)
