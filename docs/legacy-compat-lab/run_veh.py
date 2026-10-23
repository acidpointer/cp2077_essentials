from pathlib import Path
import subprocess
import os
import signal
import json
import hashlib
import argparse

lab=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument('--wine',type=Path)
parser.add_argument('--runtime',type=Path)
options=parser.parse_args()
if bool(options.wine)!=bool(options.runtime): parser.error('supply both --wine and --runtime')
ge=options.wine.resolve() if options.wine else None
runtime=options.runtime.resolve() if options.runtime else None
results=[]
configurations=[('system-wine',lab/'wine-prefix',['wine'])]
if ge: configurations.append(('ge-steamrt4',lab/'ge-wine-prefix',[str(runtime),'--verb=run','--',str(ge)]))
for name,prefix,command in configurations:
    env=os.environ.copy()
    env.update(WINEPREFIX=str(prefix),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d',
               DISPLAY='',XDG_CACHE_HOME=str(lab/'cache'),XDG_CONFIG_HOME=str(lab/'ge-config'))
    if name=='ge-steamrt4':
        env.update(PATH=str(ge.parent)+os.pathsep+env.get('PATH',''),
                   STEAM_COMPAT_APP_ID='0',SteamAppId='0',SteamGameId='0')
    path=lab/('veh-'+name+'.log')
    with path.open('w') as log:
        proc=subprocess.Popen(command+[str(lab/'veh_probe.exe')],cwd=lab,env=env,
            stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        try:
            code=proc.wait(timeout=30)
            item={'runtime':name,'returncode':code}
        except subprocess.TimeoutExpired:
            os.killpg(proc.pid,signal.SIGKILL);proc.wait()
            item={'runtime':name,'timeout':30}
    item['output']=path.read_text(errors='replace')
    item['guest_sha256']=hashlib.sha256((lab/'guest_fields.dll').read_bytes()).hexdigest()
    results.append(item)
    (lab/'veh-results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(item),flush=True)
