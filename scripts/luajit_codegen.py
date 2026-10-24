#!/usr/bin/env python3
"""Run LuaJIT's native build tools to emit the Windows x64 GC64 VM and tables.

Mirrors src/msvcbuild.bat; only the generators run on Linux. The VM object
and all runtime C sources are linked with clang-cl and the Microsoft CRT.
"""
import argparse
import pathlib
import shutil
import subprocess


def generate(source, output, compiler):
    output.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source / 'src', output / 'src', dirs_exist_ok=True)
    shutil.copytree(source / 'dynasm', output / 'dynasm', dirs_exist_ok=True)
    work = output / 'src'

    def run(args):
        subprocess.run([str(arg) for arg in args], cwd=work, check=True)

    run([compiler, '-O2', 'host/minilua.c', '-lm', '-o', 'minilua'])
    run([work / 'minilua', '../dynasm/dynasm.lua', '-LN', '-D', 'WIN',
         '-D', 'JIT', '-D', 'FFI', '-D', 'P64', '-o', 'host/buildvm_arch.h', 'vm_x64.dasc'])
    run([compiler, '-O2', '-I.', '-I../dynasm', '-DLUAJIT_TARGET=LUAJIT_ARCH_X64',
         '-DLUAJIT_OS=LUAJIT_OS_WINDOWS', *sorted(work.glob('host/buildvm*.c')),
         '-lm', '-o', 'buildvm'])
    libraries = ['lib_' + name + '.c' for name in
                 ['base', 'math', 'bit', 'string', 'table', 'io', 'os', 'package',
                  'debug', 'jit', 'ffi', 'buffer']]
    run([work / 'buildvm', '-m', 'peobj', '-o', 'lj_vm.obj'])
    for mode in ['bcdef', 'ffdef', 'libdef', 'recdef', 'vmdef', 'folddef']:
        dest = 'jit/vmdef.lua' if mode == 'vmdef' else 'lj_' + mode + '.h'
        run([work / 'buildvm', '-m', mode, '-o', dest,
             *(['lj_opt_fold.c'] if mode == 'folddef' else libraries)])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    parser.add_argument('compiler')
    args = parser.parse_args()
    generate(args.source.resolve(), args.output.resolve(), args.compiler)
