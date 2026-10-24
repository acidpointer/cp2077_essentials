"""Reconstruct a reviewed RedLib header overlay without editing dependency sources."""
import argparse
import contextlib
import difflib
import json
import pathlib
import shutil
import tempfile

import common as c


def paths():
    item = c.config()['mod_settings_redlib']
    source = c.ROOT / item['path']
    if c.git(source, 'rev-parse', 'HEAD').stdout.decode().strip() != item['commit']:
        raise ValueError('Mod Settings RedLib pin differs from its reviewed dependency.')
    if c.git(source, '--no-optional-locks', 'status', '--porcelain').stdout:
        raise ValueError('Mod Settings RedLib has dependency edits; preserve them outside the overlay.')
    files = [pathlib.Path(p) for p in c.git(source, 'ls-files').stdout.decode().splitlines()
             if p.startswith(('include/', 'vendor/'))]
    destination = c.ROOT / 'build/generated/mod-settings-redlib'
    return item, source, files, destination


@contextlib.contextmanager
def reconstruction():
    item, source, files, destination = paths()
    with tempfile.TemporaryDirectory(prefix='cp2077-redlib-') as temporary:
        stage = pathlib.Path(temporary)
        for path in files:
            target = stage / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / path, target)
        for layer in item['patches']:
            patch = c.ROOT / layer['path']
            if c.sha(patch) != layer['sha256']:
                raise ValueError('Mod Settings RedLib overlay checksum mismatch.')
            c.run(['git', 'apply', '--no-index', '--check', patch], cwd=stage)
            c.run(['git', 'apply', '--no-index', patch], cwd=stage)
        yield item, files, destination, stage


def check():
    with reconstruction() as (item, files, destination, stage):
        actual = {p.relative_to(destination) for p in destination.rglob('*') if p.is_file()}
        if actual != set(files) or any((destination / path).read_bytes() != (stage / path).read_bytes() for path in files):
            raise ValueError("Unexported RedLib overlay edits; export before building or packaging.")
        return c.fingerprint({path.as_posix(): c.sha(stage / path) for path in files})


def prepare():
    with reconstruction() as (item, files, destination, stage):
        # An overlay is generated output. Refuse stale extra files and retain the
        # old generated tree on a failed checksum/apply check.
        if destination.exists():
            actual = {p.relative_to(destination) for p in destination.rglob('*') if p.is_file()}
            if actual != set(files):
                raise ValueError('Unexpected generated RedLib files; inspect the overlay before rebuilding.')
            state = c.ROOT / 'build/state/mod-settings-redlib.json'
            previous = json.loads(state.read_text()) if state.exists() else {}
            changed = any((destination / path).read_bytes() != (stage / path).read_bytes() for path in files)
            saved = {path.as_posix(): c.sha(destination / path) for path in files}
            if changed and saved != previous:
                raise ValueError('Unexported RedLib overlay edits; run scripts/mod_settings.py export before rebuilding.')
        for path in files:
            c.atomic_bytes(destination / path, (stage / path).read_bytes())
        c.atomic_json(c.ROOT / 'build/state/mod-settings-redlib.json',
                      {path.as_posix(): c.sha(destination / path) for path in files})
    return destination


def export():
    item, source, files, destination = paths()
    if len(item['patches']) != 1:
        raise ValueError('RedLib overlay export supports one reviewed layer; refusing to collapse a stack.')
    actual = {p.relative_to(destination) for p in destination.rglob('*') if p.is_file()}
    if actual != set(files):
        raise ValueError('RedLib overlay export supports edits to existing pinned header files only.')
    chunks = []
    for path in files:
        before, after = (source / path).read_text(), (destination / path).read_text()
        if before != after:
            chunks.extend(difflib.unified_diff(before.splitlines(keepends=True), after.splitlines(keepends=True),
                                             fromfile='a/' + path.as_posix(), tofile='b/' + path.as_posix()))
    if not chunks:
        raise ValueError('Refusing an empty RedLib compatibility overlay.')
    patch = c.ROOT / item['patches'][0]['path']
    data = ''.join(chunks).encode()
    # Verify the candidate separately before replacing either saved metadata file.
    with tempfile.TemporaryDirectory(prefix='cp2077-redlib-export-') as temporary:
        stage = pathlib.Path(temporary)
        for path in files:
            target = stage / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / path, target)
        candidate = stage / 'overlay.patch'
        candidate.write_bytes(data)
        c.run(['git', 'apply', '--no-index', candidate], cwd=stage)
        if any((stage / path).read_bytes() != (destination / path).read_bytes() for path in files):
            raise ValueError('RedLib overlay reconstruction differs.')
    c.atomic_bytes(patch, data)
    lock = c.config()
    lock['mod_settings_redlib']['patches'][0]['sha256'] = c.sha(patch)
    c.atomic_json(c.ROOT / 'sources.lock.json', lock)
    print('Exported and reconstructed the Mod Settings RedLib overlay.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare', 'export', 'check'])
    c.entry(lambda: globals()[parser.parse_args().command]())
