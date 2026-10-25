#!/usr/bin/env python3
"""Reproduce the installed Native Settings rendering failure without loading the game."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
LAB = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-settings', type=Path, required=True,
                        help='Installed nativeSettings directory containing init.lua and Cron.lua (read only)')
    parser.add_argument('--converter-header', type=Path,
                        default=ROOT / 'sources/cyber-engine-tweaks/src/reverse/Converter.h',
                        help='Baseline Converter.h; can use the preserved evidence copy after a production fix')
    parser.add_argument('--baseline', action='store_true',
                        help='Expect the pre-fix failure and compare candidate ancestry semantics')
    args = parser.parse_args()
    out = ROOT / 'build/native-settings-empty-pages'
    inputs = out / 'nativeSettings'
    inputs.mkdir(parents=True, exist_ok=True)
    identities = {}
    for name in ['init.lua', 'Cron.lua']:
        data = (args.native_settings / name).read_bytes()
        (inputs / name).write_bytes(data)
        identities[name] = hashlib.sha256(data).hexdigest()
    source = args.converter_header
    body = source.read_text().split('struct ClassConverter :', 1)[1]
    start = body.index('RED4ext::CStackType ToRED(')
    opening = body.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (body[end] == '{') - (body[end] == '}')
        end += 1
    method = body[start:end] + '\n'
    (out / 'converter.inc').write_text(method)
    identities['Converter.h'] = hashlib.sha256(source.read_bytes()).hexdigest()
    identities['extracted_ToRED'] = hashlib.sha256(method.encode()).hexdigest()
    flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'luajit'], text=True))
    command = ['clang++', '-std=c++20', '-O2', '-DSOL_ALL_SAFETIES_ON', '-DSOL_LUAJIT=1',
               '-DSOL_EXCEPTIONS_SAFE_PROPAGATION=0', '-I' + str(out),
               '-I' + str(ROOT / 'build/deps/cet-sol2/include'), str(LAB / 'probe.cpp'),
               '-o', str(out / 'probe'), *flags]
    with (out / 'build.log').open('w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    for mode in ['strict', 'ancestry'] if args.baseline else ['production']:
        result = subprocess.run([str(out / 'probe'), str(inputs), str(LAB / 'probe.lua'), mode],
                                capture_output=True, text=True, timeout=30)
        (out / (mode + '.log')).write_text(result.stdout + result.stderr)
        print(result.stdout + result.stderr)
        if result.returncode:
            raise SystemExit(result.returncode)
    (out / 'identities.json').write_text(json.dumps(identities, indent=2) + '\n')


if __name__ == '__main__':
    main()
