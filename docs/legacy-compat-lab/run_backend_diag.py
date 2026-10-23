from pathlib import Path
import os
import subprocess
import json
lab=Path(__file__).resolve().parent
env=os.environ.copy()
env.update(WINEPREFIX=str(lab/'wine-prefix'),WINEDEBUG='+seh',
           WINEDLLOVERRIDES='winemenubuilder.exe=d',DISPLAY='',
           XDG_CACHE_HOME=str(lab/'cache'))
def win(p): return 'Z:'+str(p).replace('/','\\')
env['DYNAMORIO_CONFIGDIR']=win(lab/'dr-config')
logs=lab/'dr-debug-logs';logs.mkdir(exist_ok=True)
cmd=['wine',str(lab/'dynamorio/bin64/drrun.exe'),'-verbose','-root',win(lab/'dynamorio'),
     '-no_follow_children','-debug','-loglevel','2','-logdir',win(logs),'--',win(lab/'callback_bridge.exe')]
with (lab/'backend-diagnostic.log').open('w') as f:
    r=subprocess.run(cmd,cwd=lab,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=30)
output=(lab/'backend-diagnostic.log').read_text(errors='replace')
(lab/'backend-diagnostic-result.json').write_text(json.dumps({'returncode':r.returncode,'command':cmd,'logfiles':[str(p.relative_to(lab)) for p in logs.rglob('*') if p.is_file()]},indent=2))
print('returncode:',r.returncode)
print('\n'.join(output.splitlines()[-45:]))
