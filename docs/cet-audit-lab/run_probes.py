#!/usr/bin/env python3
"""Historical pre-fix defect probes; these are not tests of the fixed CET.

Some probes extract the named source methods; others isolate C++ lifetime
semantics used by CET. Generated sources/results stay under build/cet-audit.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'sources/cyber-engine-tweaks/src'
OUT = ROOT / 'build/cet-audit/probes'


def method(path, signature):
    text = (SOURCE / path).read_text()
    start = text.index(signature)
    opening = text.index('{', start)
    level = 1
    end = opening + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def probes():
    queue = '\n'.join(method('scripting/GameHooks.cpp', signature).replace('GameMainThread::RepeatedTaskQueue::', 'Queue::')
                      for signature in ['void GameMainThread::RepeatedTaskQueue::AddTask(', 'void GameMainThread::RepeatedTaskQueue::Drain('])
    lookup = '\n'.join(method('scripting/LuaVM.cpp', signature).replace('LuaVM::', 'Lookup::')
                       for signature in ['std::string LuaVM::GetTDBIDString(', 'void LuaVM::RegisterTDBIDString('])
    reset = method('scripting/LuaSandbox.cpp', 'void LuaSandbox::ResetState()')
    return {
        'override_capture': ('capture-semantics model of WrapNextOverride pointer parameters', r'''
#include <functional>
#include <iostream>
__attribute__((noinline)) std::function<int()> wrap(int* apRealFunction) {
    return [&] { return *apRealFunction; };
}
int main() { int value=42; auto closure=wrap(&value); std::cout << closure() << '\n'; }
''', [], 'stack-use-after-return'),
        'queue_reentrant': ('extracted RepeatedTaskQueue AddTask/Drain; std::vector storage matches TiltedCore', r'''
#include <functional>
#include <mutex>
#include <vector>
struct Queue {
    std::recursive_mutex m_mutex;
    std::vector<std::function<bool()>> m_tasks;
    void AddTask(const std::function<bool()>&);
    void Drain();
};
''' + queue + r'''
int main() { Queue queue; queue.AddTask([&] {queue.AddTask([]{return true;}); return true;}); queue.Drain(); }
''', ['-D_GLIBCXX_DEBUG'], 'singular'),
        'short_kark': ('metadata header read expression from InitializeTweakDBMetadata, before Oodle', r'''
#include <cstdint>
#include <iostream>
#include <vector>
int main() { std::vector<char> encodedBytes(3, 'x');
 volatile auto decodedSize=reinterpret_cast<uint32_t*>(encodedBytes.data())[1];
 std::cout << decodedSize << '\n'; }
''', [], 'heap-buffer-overflow'),
        'tdbid_cycle': ('extracted lookup/registration methods; self-parent from empty derived suffix', r'''
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
struct TDBID {uint64_t value;};
struct TDBIDLookupEntry {uint64_t base; std::string name;};
struct Lookup {
 std::mutex m_tdbidLock;
 std::map<uint64_t,TDBIDLookupEntry> m_tdbidLookup;
 std::map<uint64_t,std::set<uint64_t>> m_tdbidDerivedLookup;
 std::string GetTDBIDString(uint64_t,bool);
 void RegisterTDBIDString(uint64_t,uint64_t,const std::string&);
 std::string GetTDBDIDDebugString(TDBID) {return "<unknown>";}
};
''' + lookup + r'''
int main() {Lookup lookup; lookup.RegisterTDBIDString(123,0,"Record");
 lookup.RegisterTDBIDString(123,123,""); lookup.GetTDBIDString(123,false);}
''', [], 'TIMEOUT'),
        'binding_dot': ('VKBindings Load schema-dispatch branch with actual pinned nlohmann/json', r'''
#include <map>
#include <string>
#include <iostream>
#include <nlohmann/json.hpp>
int main() {
 std::map<std::string,std::map<std::string,uint64_t>> m_modIdToBinds;
 auto config=nlohmann::json::parse(R"({"WalkByDefault2.0":{"walk":123}})");
 try {
  for (auto& mod : config.items()) {
   const auto& modName=mod.key(); auto concatPoint=modName.find('.');
   if (concatPoint < modName.length()) {
    auto modBindName=modName.substr(0,concatPoint); auto modBindId=modName.substr(concatPoint+1);
    m_modIdToBinds[modBindName][modBindId]=mod.value();
   } else {for(auto& bind:mod.value().items()) m_modIdToBinds[modName][bind.key()]=bind.value();}
  }
 } catch(const std::exception& e) {std::cout << "REPRODUCED: " << e.what() << '\n'; return 0;}
 return 1;
}
''', ['-I' + str(ROOT / 'build/deps/cet-json/include')], 'type must be number'),
        'sandbox_reset': ('extracted ResetState with fake storage, no Lua engine', r'''
#include <iostream>
#include <map>
#include <vector>
struct FakeLua {void collect_garbage(){}};
struct Locked {FakeLua state; FakeLua& Get(){return state;}};
struct Scripting {Locked GetLockedState(){return {};}};
struct LuaSandbox {
 std::map<int,int> m_modules;
 std::vector<int> m_sandboxes{0,1,2};
 Scripting script; Scripting* m_pScripting=&script; int closed=0;
 void CloseDBForSandbox(int){++closed;}
 void ResetState();
};
''' + reset + r'''
int main() {LuaSandbox sandbox; sandbox.ResetState();
 if(sandbox.m_sandboxes.size()==3 && sandbox.closed==0) {
 std::cout<<"REPRODUCED: three sandboxes retained, zero databases closed\n"; return 0;} return 1;}
''', [], 'three sandboxes retained'),
    }


def run_windows_comptr():
    directory = OUT / 'windows'
    directory.mkdir(exist_ok=True)
    source = directory / 'comptr_probe.cpp'
    source.write_text(r'''
#include <Windows.h>
#include <wrl/client.h>
#include <cstdio>
struct Probe final : IUnknown {
 long refs=1; int releases=0;
 HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override {return E_NOINTERFACE;}
 ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
 ULONG STDMETHODCALLTYPE Release() override {++releases;return --refs;}
};
int main() {Probe probe; {Microsoft::WRL::ComPtr<IUnknown> ptr; ptr.Attach(&probe); ptr->Release();}
 std::printf("refs=%ld releases=%d\n",probe.refs,probe.releases);
 return probe.refs==-1 && probe.releases==2 ? 0:1;}
''')
    (directory / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.26)
project(CetComPtrProbe LANGUAGES CXX)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
add_executable(comptr-probe comptr_probe.cpp)
target_compile_definitions(comptr-probe PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
''')
    build = directory / 'build-make'
    commands = [['cmake', '-S', str(directory), '-B', str(build), '-G', 'Unix Makefiles',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/toolchains/clang-cl-linux.cmake'),
                 '-DXWIN_ROOT=' + str(ROOT / 'build/toolchains/xwin'), '-DCMAKE_BUILD_TYPE=Release'],
                ['cmake', '--build', str(build)]]
    with (directory / 'build.log').open('w') as log:
        for command in commands:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    env = dict(os.environ, WINEPREFIX=str(OUT / 'wine-prefix'), WINEDEBUG='-all',
               WINEDLLOVERRIDES='winemenubuilder.exe=d', XDG_CACHE_HOME=str(OUT / 'cache'))
    result = subprocess.run(['wine', str(build / 'comptr-probe.exe')], env=env,
                            capture_output=True, text=True, timeout=45)
    (directory / 'run.log').write_text(result.stdout + result.stderr)
    return {'scope': 'actual Microsoft WRL ComPtr with a fake COM object; no game/GPU',
            'returncode': result.returncode, 'stdout': result.stdout,
            'reproduced': result.returncode == 0 and 'releases=2' in result.stdout}


def main():
    global SOURCE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-src', type=Path, required=True,
                        help='src directory reconstructed with 001-essentials.patch only')
    parser.add_argument('--wine', action='store_true', help='also test actual WRL with a fresh isolated Wine prefix')
    args = parser.parse_args()
    SOURCE = args.baseline_src.resolve()
    evidence = json.loads((ROOT / 'docs/cet-audit-lab/evidence.json').read_text())
    prefix = 'sources/cyber-engine-tweaks/src/'
    for name, expected in evidence['cet_source_sha256'].items():
        path = SOURCE / name.removeprefix(prefix)
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            parser.error('Baseline differs from the recorded audit: ' + name +
                         '. Use tests/test_cet_runtime.py to test the fixed sources.')
    OUT.mkdir(parents=True, exist_ok=True)
    results = {}
    for name, (scope, code, flags, marker) in probes().items():
        path = OUT / (name + '.cpp')
        path.write_text(code)
        executable = OUT / name
        command = ['clang++', '-std=c++20', '-O0', '-g', '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', *flags, str(path), '-pthread', '-o', str(executable)]
        compile_result = subprocess.run(command, capture_output=True, text=True)
        (OUT / (name + '-build.log')).write_text(compile_result.stdout + compile_result.stderr)
        if compile_result.returncode:
            raise RuntimeError('Probe compilation failed: ' + name)
        env = dict(os.environ, ASAN_OPTIONS='detect_stack_use_after_return=1:detect_leaks=0',
                   UBSAN_OPTIONS='halt_on_error=1')
        try:
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=3, env=env)
            output = result.stdout + result.stderr
            returncode = result.returncode
        except subprocess.TimeoutExpired:
            output = 'TIMEOUT'
            returncode = None
        (OUT / (name + '.log')).write_text(output)
        results[name] = {'scope': scope, 'source_sha256': hashlib.sha256(code.encode()).hexdigest(),
                         'returncode': returncode, 'reproduced': marker in output, 'marker': marker}
        print(name, 'reproduced=' + str(results[name]['reproduced']), flush=True)
    (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if args.wine:
        results['comptr_release'] = run_windows_comptr()
        print('comptr_release', 'reproduced=' + str(results['comptr_release']['reproduced']), flush=True)
    (OUT / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    if not all(result['reproduced'] for result in results.values()):
        raise SystemExit('One or more probes did not reproduce their expected defect; inspect the logs.')


if __name__ == '__main__':
    main()
