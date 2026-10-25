#!/usr/bin/env python3
"""Build isolated Windows CET Lua regressions; optional runner, no game or Git writes."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def extract_method(path, signature, after=None):
    text = path.read_text()
    start = text.index(signature, text.index(after) if after else 0)
    opening = text.index('{',start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-build', type=Path, default=ROOT / 'build/plugins/release')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/cet-runtime')
    parser.add_argument('--runner', help='Optional Windows executable runner, such as wine')
    checks = ['cet-lua', 'cet-conversion', 'cet-lifecycle', 'cet-graphics', 'cet-config']
    parser.add_argument('--only', choices=checks, action='append', help='Run only the named check (repeatable)')
    args = parser.parse_args()
    directory = args.output.resolve()
    directory.mkdir(parents=True,exist_ok=True)
    source = ROOT / 'sources/cyber-engine-tweaks/src/scripting/FunctionOverride.cpp'
    extracted = extract_method(source,'sol::function FunctionOverride::WrapNextOverride(')+'\n'
    output = directory/'continuation.inc'
    if not output.exists() or output.read_text() != extracted: output.write_text(extracted)
    extracted = extract_method(source, 'bool FunctionOverride::ExecuteChain(') + '\n'
    output = directory / 'execute-chain.inc'
    if not output.exists() or output.read_text() != extracted: output.write_text(extracted)
    converter = ROOT / 'sources/cyber-engine-tweaks/src/reverse/Converter.h'
    extracted = extract_method(converter, 'RED4ext::CStackType ToRED(', 'struct ClassConverter :') + '\n'
    output = directory / 'class-converter.inc'
    if not output.exists() or output.read_text() != extracted: output.write_text(extracted)
    sdk = ROOT / 'sources/red4ext-sdk/include/RED4ext/RTTITypes-inl.hpp'
    extracted = extract_method(sdk, 'RED4EXT_INLINE bool RED4ext::CClass::IsA(') + '\n'
    output = directory / 'class-is-a.inc'
    if not output.exists() or output.read_text() != extracted: output.write_text(extracted)
    build = directory/'windows'
    command = ['cmake','-S',str(ROOT/'tests/native/cet'),'-B',str(build),
               '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/toolchains/clang-cl-linux.cmake'),
               '-DXWIN_ROOT='+str(ROOT/'build/toolchains/xwin'),'-DCMAKE_BUILD_TYPE=Release',
               '-DCET_NATIVE_BUILD='+str(args.native_build.resolve()),'-DCET_CONTINUATION_DIR='+str(directory)]
    if not (build/'CMakeCache.txt').exists():
        command += ['-G','Ninja' if shutil.which('ninja') else 'Unix Makefiles']
    with (directory/'build.log').open('w') as log:
        for invocation in [command,['cmake','--build',str(build),'--parallel','2']]:
            subprocess.run(invocation,stdout=log,stderr=subprocess.STDOUT,check=True)
    if args.runner:
        env = dict(os.environ)
        if Path(args.runner).name.startswith('wine'):
            env.update(WINEPREFIX=str(directory/'wine-prefix'),WINEDEBUG='-all',WINEDLLOVERRIDES='winemenubuilder.exe=d')
        for name in args.only or checks:
            result = subprocess.run([args.runner,str(build/(name+'.exe'))],env=env,capture_output=True,text=True,timeout=60)
            (directory/(name+'-run.log')).write_text(result.stdout+result.stderr)
            print(result.stdout)
            if result.returncode: raise SystemExit(result.returncode)
    print('CET Windows harness built; this is not a game test.')


if __name__ == '__main__':
    main()
