from pathlib import Path
import subprocess
import os
import signal
import json
import argparse

lab=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--wine',type=Path,required=True)
parser.add_argument('--runtime',type=Path,required=True)
options=parser.parse_args()
wine=options.wine.resolve()
runtime=options.runtime.resolve()
env=os.environ.copy()
env.update(WINEPREFIX=str(lab/'ge-wine-prefix'), WINEDEBUG='-all',
           WINEDLLOVERRIDES='winemenubuilder.exe=d',DISPLAY='',
           XDG_CACHE_HOME=str(lab/'ge-cache'),XDG_CONFIG_HOME=str(lab/'ge-config'),
           PATH=str(wine.parent)+os.pathsep+env.get('PATH',''),
           STEAM_COMPAT_APP_ID='0',SteamAppId='0',SteamGameId='0')
prefix=[str(runtime),'--verb=run','--',str(wine)]
tests=[('callback', [str(lab/'callback_bridge.exe')]),
       ('translate',[str(lab/'gum_probe.exe'),'--translate']),
       ('threads',[str(lab/'gum_probe.exe'),'--threads']),
       ('seh-native',[str(lab/'gum_probe.exe'),'--seh-native']),
       ('seh',[str(lab/'gum_probe.exe'),'--seh'])]
results=[]
for name,args in tests:
    path=lab/('ge-'+name+'.log')
    with path.open('w') as log:
        proc=subprocess.Popen(prefix+args,cwd=lab,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            code=proc.wait(timeout=45 if name=='callback' else 20)
            item={'test':name,'returncode':code}
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid,signal.SIGKILL)
            proc.wait()
            item={'test':name,'timeout':True}
    item['output']=path.read_text(errors='replace')
    results.append(item)
    (lab/'ge-results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(item),flush=True)
