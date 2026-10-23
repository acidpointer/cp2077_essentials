from pathlib import Path
import subprocess
import os
import json

lab = Path(__file__).resolve().parent
env = os.environ.copy()
env.update(WINEPREFIX=str(lab/'wine-prefix'), WINEDEBUG='-all',
           WINEDLLOVERRIDES='winemenubuilder.exe=d', DISPLAY='',
           XDG_CACHE_HOME=str(lab/'cache'))
results = []
for exe in ['callback_bridge.exe','load_order_host.exe']:
    try:
        r = subprocess.run(['wine', str(lab/exe)], cwd=lab, env=env,
                           text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        item = {'target':exe, 'returncode':r.returncode, 'output':r.stdout}
    except subprocess.TimeoutExpired as e:
        item = {'target':exe, 'timeout':45, 'output':str(e.output)}
    results.append(item)
    (lab/'wine-baseline-results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(item), flush=True)
