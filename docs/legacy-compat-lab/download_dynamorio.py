import io
import json
from pathlib import Path
import urllib.request
import zipfile
import hashlib
import sys

OUT = Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parent
OUT.mkdir(exist_ok=True)

def request(url, headers=None):
    return urllib.request.urlopen(urllib.request.Request(url, headers={
        'User-Agent': 'cp2077-compat-research', **(headers or {})}), timeout=45)

def api(path, name):
    with request('https://api.github.com/' + path) as f:
        data = json.load(f)
    (OUT / name).write_text(json.dumps(data, indent=2))
    return data

release_file = OUT / 'backend-release.json'
release = json.loads(release_file.read_text()) if release_file.exists() else api('repos/DynamoRIO/dynamorio/releases/tags/cronbuild-11.91.20741', 'backend-release.json')
asset = next(a for a in release['assets'] if 'Windows' in a['name'])
print(json.dumps({'tag': release['tag_name'], 'published': release['published_at'],
                  'asset': asset['name'], 'size': asset['size'], 'digest': asset.get('digest')}), flush=True)

class RemoteZip(io.RawIOBase):
    def __init__(self, url, size):
        self.url, self.size, self.pos, self.transferred = url, size, 0, 0
    def seekable(self): return True
    def readable(self): return True
    def tell(self): return self.pos
    def seek(self, offset, whence=0):
        self.pos = offset if whence == 0 else (self.pos if whence == 1 else self.size) + offset
        return self.pos
    def read(self, size=-1):
        size = min(self.size-self.pos, size if size >= 0 else self.size)
        if size <= 0: return b''
        if size > 32*1024*1024: raise RuntimeError('oversized range')
        with request(self.url, {'Range': f'bytes={self.pos}-{self.pos+size-1}'}) as f:
            if f.status != 206: raise RuntimeError(f'Range unsupported: HTTP {f.status}')
            data = f.read(size+1)
        if len(data) != size: raise RuntimeError(f'Invalid range length: {len(data)} != {size}')
        self.pos += len(data)
        self.transferred += len(data)
        return data

remote = RemoteZip(asset['browser_download_url'], asset['size'])
manifest = []
with zipfile.ZipFile(remote) as z:
    for member in z.infolist():
        parts = Path(member.filename).parts
        relative = Path(*parts[1:])
        name = relative.as_posix()
        wanted = name.startswith(('bin64/', 'lib64/release/', 'lib64/debug/',
                                  'ext/lib64/release/', 'samples/bin64/')) or name in {
                                      'lib64/drpreinject.dll', 'lib32/drpreinject.dll',
                                      'lib32/release/dynamorio.dll', 'lib32/debug/dynamorio.dll'}
        if not wanted or member.is_dir(): continue
        if relative.is_absolute() or '..' in relative.parts: raise RuntimeError('unsafe member')
        target = OUT / 'dynamorio' / relative
        payload = target.read_bytes() if target.exists() else z.read(member)
        if len(payload) != member.file_size: raise RuntimeError('member size mismatch')
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        manifest.append({'path': name, 'size': len(payload), 'sha256': hashlib.sha256(payload).hexdigest()})
(OUT / 'backend-files.json').write_text(json.dumps(manifest, indent=2))
print(json.dumps({'files': len(manifest), 'downloaded': remote.transferred,
                  'executables': [m['path'] for m in manifest if m['path'].endswith('.exe')]}), flush=True)
