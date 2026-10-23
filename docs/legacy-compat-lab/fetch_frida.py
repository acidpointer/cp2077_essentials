from pathlib import Path
import urllib.request
import json
import tarfile
import hashlib
import shutil

lab=Path(__file__).resolve().parent
def get(url):
    return urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'cp2077-compat-research'}),timeout=45)
release_file=lab/'frida-release.json'
if release_file.exists():
    release=json.loads(release_file.read_text())
else:
    with get('https://api.github.com/repos/frida/frida/releases/tags/17.24.3') as r:
        release=json.load(r)
assets=[a for a in release['assets'] if 'gum-devkit' in a['name'] and 'windows-x86_64' in a['name'] and a['name'].endswith('.tar.xz')]
(lab/'frida-release.json').write_text(json.dumps(release,indent=2))
print(json.dumps({'tag':release['tag_name'],'published':release['published_at'],'assets':[{k:a.get(k) for k in ['name','size','browser_download_url','digest']} for a in assets]}),flush=True)
if len(assets)!=1: raise RuntimeError('unexpected devkit selection')
a=assets[0]
archive=lab/a['name']
with get(a['browser_download_url']) as r, archive.open('wb') as f: shutil.copyfileobj(r,f)
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
if a.get('digest') and a['digest']!='sha256:'+digest: raise RuntimeError('digest mismatch')
out=lab/'frida'
out.mkdir(exist_ok=True)
with tarfile.open(archive) as t: t.extractall(out,filter='data')
print(json.dumps({'sha256':digest,'files':[p.name for p in out.iterdir()]}),flush=True)
