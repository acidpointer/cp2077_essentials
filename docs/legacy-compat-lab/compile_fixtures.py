from pathlib import Path
import subprocess
import json
import sys

root = Path(sys.argv[1]).resolve()
lab = Path(__file__).resolve().parent
xwin = root / 'build/toolchains/xwin'
includes = [xwin/'crt/include', xwin/'sdk/include/ucrt',
            xwin/'sdk/include/shared', xwin/'sdk/include/um']
libs = [xwin/'crt/lib/x86_64', xwin/'sdk/lib/ucrt/x86_64',
        xwin/'sdk/lib/um/x86_64']
base = ['clang-cl', '-fuse-ld=lld', '/nologo', '/std:c++20', '/EHsc', '/MT', '/O2']
base += ['/imsvc'+str(i) for i in includes]
base += ['/I'+str(root/'sources/red4ext-sdk/include')]
results = []
for name, source, dll in [('callback_bridge.exe','callback_bridge.cpp',False),
                          ('load_order.dll','load_order_dll.cpp',True),
                          ('load_order_host.exe','load_order_host.cpp',False),
                          ('guest_fields.dll','guest_fields.cpp',True),
                          ('gum_probe.exe','gum_probe.cpp',False),
                          ('veh_probe.exe','veh_probe.cpp',False)]:
    if len(sys.argv)>2 and name not in sys.argv[2:]: continue
    cmd = base + (['/LD'] if dll else []) + [source, '/Fe:'+name,
           '/Fo:'+source+'.obj', '/link'] + ['/libpath:'+str(p) for p in libs]
    if source in ['gum_probe.cpp','veh_probe.cpp']:
        cmd += ['/libpath:'+str(lab/'frida'), 'user32.lib','advapi32.lib','bcrypt.lib',
                'crypt32.lib','shell32.lib','ole32.lib','secur32.lib']
    if source=='veh_probe.cpp': cmd.insert(cmd.index('/link'),'/EHa')
    r = subprocess.run(cmd, cwd=lab, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    results.append({'output':name,'command':cmd,'exit_code':r.returncode,'log':r.stdout})
    print(name, r.returncode, r.stdout, flush=True)
    if r.returncode:
        (lab/'compile-results.json').write_text(json.dumps(results,indent=2))
        raise SystemExit(r.returncode)
(lab/'compile-results.json').write_text(json.dumps(results,indent=2))
