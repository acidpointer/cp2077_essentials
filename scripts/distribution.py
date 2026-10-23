"""Collect only coherent packages; verify before atomically replacing a distribution."""
import json
import pathlib
import shutil
import tempfile

import common as c
import patches
import verification


def output(config, override=None):
    path = pathlib.Path(override) if override else c.ROOT / 'dist' / config.lower()
    if not path.is_absolute():
        path = c.ROOT / path
    path = path.resolve()
    try:
        relative = path.relative_to(c.ROOT)
    except ValueError:
        raise ValueError('Distribution output must stay under this repository.') from None
    if not relative.parts or relative.parts[0] not in ['build', 'dist']:
        raise ValueError('Use a distribution directory under build/ or dist/.')
    return path


def collect(config, selected, override=None):
    destination = output(config, override)
    destination.parent.mkdir(parents=True, exist_ok=True)
    records = {}
    old = destination / 'manifest.json'
    if old.exists():
        previous = json.loads(old.read_text())
        if previous.get('schema') == 2:
            records = previous['projects']
    sdk = patches.identity('red4ext-sdk')
    incoming = {}
    for project in selected:
        path = c.ROOT / 'build/packages' / config.lower() / project / 'package.json'
        incoming[project] = json.loads(path.read_text())
    records.update(incoming)
    for project, record in records.items():
        if record['source'] != patches.identity(project) or record['sdk'] != sdk:
            raise ValueError(f'{project}: cached package has different source/SDK provenance. Run the full build.')
    with tempfile.TemporaryDirectory(prefix='.cp2077-dist-', dir=destination.parent) as temporary:
        stage = pathlib.Path(temporary) / 'new'
        stage.mkdir()
        result = {}
        for project, record in records.items():
            result[project] = {key: value for key, value in record.items() if key != 'files'}
            result[project]['files'] = {}
            for kind, item in record['files'].items():
                source = c.ROOT / item['file'] if project in incoming else destination / item['file']
                group = 'symbols' if kind == 'symbols' else 'standard' if kind == 'standard' else ''
                target = stage / group / source.name
                target.parent.mkdir(parents=True, exist_ok=True)
                if c.sha(source) != item['sha256']:
                    raise ValueError(f'{source}: package changed after creation')
                shutil.copy2(source, target)
                c.atomic_bytes(target.with_suffix('.zip.sha256'), f'{item["sha256"]}  {target.name}\n'.encode())
                result[project]['files'][kind] = {'file': target.relative_to(stage).as_posix(), 'sha256': item['sha256']}
        manifest = {'schema': 2, 'game_version': '2.5', 'game_file_version': '3.0.88.46235',
                    'config': config, 'sdk': sdk, 'projects': result}
        c.atomic_json(stage / 'manifest.json', manifest)
        sums = ''.join(f'{item["sha256"]}  {item["file"]}\n'
                       for record in result.values() for item in record['files'].values())
        c.atomic_bytes(stage / 'SHA256SUMS', sums.encode())
        verification.distribution(stage)
        previous = pathlib.Path(temporary) / 'previous'
        if destination.exists():
            destination.rename(previous)
        try:
            stage.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise
    for project, record in result.items():
        print(f'{c.NAMES[project]}: {destination / record["files"]["runtime"]["file"]}')
    return destination
