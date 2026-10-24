"""Shared paths, Git subprocesses, and atomic metadata writes."""
import hashlib
import json
import os
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(os.environ.get('CP2077_ROOT', pathlib.Path(__file__).resolve().parents[1])).resolve()
ALIASES = {'archivexl': 'archive-xl', 'tweakxl': 'tweak-xl', 'inputloader': 'input-loader',
           'cet': 'cyber-engine-tweaks', 'cyberenginetweaks': 'cyber-engine-tweaks'}
NAMES = {'red4ext': 'RED4ext', 'archive-xl': 'ArchiveXL', 'codeware': 'Codeware', 'tweak-xl': 'TweakXL', 'red4ext-sdk': 'RED4ext.SDK', 'input-loader': 'InputLoader', 'cyber-engine-tweaks': 'CyberEngineTweaks'}
ROOT_LAYOUT = {'red4ext', 'cyber-engine-tweaks'}


def binary_name(project):
    return 'input_loader' if project == 'input-loader' else NAMES[project]


def run(args, cwd=None, env=None, check=True):
    result = subprocess.run([str(a) for a in args], cwd=cwd or ROOT, env=env, capture_output=True)
    if check and result.returncode:
        raise RuntimeError(f'Command failed: {" ".join(map(str, args))}\n' + result.stderr.decode(errors='replace'))
    return result


def git(path, *args, **kwargs):
    return run(['git', '-C', path, *args], **kwargs)


def config():
    return json.loads((ROOT / 'sources.lock.json').read_text())


def select(value):
    projects = config()['projects']
    if value == 'all':
        return list(projects)
    value = ALIASES.get(value, value)
    if value not in projects:
        raise ValueError(f'Unknown project: {value}')
    return [value]


def atomic_bytes(path, data):
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = pathlib.Path(stream.name)
        stream.write(data)
    temporary.chmod(0o644)
    temporary.replace(path)


def atomic_json(path, value):
    atomic_bytes(path, (json.dumps(value, indent=2) + '\n').encode())


def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def fingerprint(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def entry(main):
    try:
        main()
    except (RuntimeError, ValueError, OSError) as error:
        raise SystemExit(str(error)) from None
