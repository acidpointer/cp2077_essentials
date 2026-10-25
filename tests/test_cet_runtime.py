"""Production CET helper regressions; never load the game or mutate Git metadata."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'sources/cyber-engine-tweaks/src'


class CetRuntime(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # The tooling suite also runs on fresh upstream checkouts, before the
        # build applies our saved patches. The cycle test must not extract the
        # upstream method that these patches deliberately replace.
        if not (SOURCE / 'common/Runtime.h').is_file():
            message = 'Apply the saved CET patches before running native runtime helper tests.'
            if os.environ.get('CP2077_REQUIRE_CET_TESTS') == '1':
                raise RuntimeError(message)
            raise unittest.SkipTest(message)

    def test_portable_helpers(self):
        compiler = shutil.which('clang++')
        json_include = ROOT / 'build/deps/cet-json/include'
        if not compiler or not (json_include / 'nlohmann/json.hpp').exists():
            if os.environ.get('CP2077_REQUIRE_CET_TESTS') == '1':
                self.fail('CET helper tests require clang++ and the pinned JSON dependency')
            self.skipTest('bootstrap pinned CET dependencies and install clang++')
        with tempfile.TemporaryDirectory(prefix='cet-runtime-') as temporary:
            executable = Path(temporary) / 'portable'
            command = [compiler, '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
                       '-fno-omit-frame-pointer', '-D_GLIBCXX_ASSERTIONS', '-pthread',
                       '-I' + str(SOURCE), '-I' + str(json_include),
                       str(ROOT / 'tests/native/cet/portable.cpp'), '-o', str(executable)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30,
                                    env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0:detect_stack_use_after_return=1", UBSAN_OPTIONS="halt_on_error=1"))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('checks passed', result.stdout)

    def test_conversion_ownership(self):
        compiler = shutil.which('clang++')
        headers = ROOT / 'build/deps/tiltedcore/Code/core/include'
        if not compiler or not (headers/'Allocator.hpp').exists():
            if os.environ.get('CP2077_REQUIRE_CET_TESTS') == '1': self.fail('Pinned TiltedCore headers are required')
            self.skipTest('bootstrap pinned TiltedCore and install clang++')
        with tempfile.TemporaryDirectory(prefix='cet-values-') as temporary:
            directory = Path(temporary)
            include = directory/'include'
            include.mkdir()
            (include/'TiltedCore').symlink_to(headers,target_is_directory=True)
            executable = directory/'values'
            result = subprocess.run([compiler,'-std=c++20','-O1','-g','-fsanitize=address,undefined',
                '-I'+str(SOURCE),'-I'+str(include),str(ROOT/'tests/native/cet/values.cpp'),'-o',str(executable)],capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stderr)
            run = subprocess.run([str(executable)],capture_output=True,text=True,timeout=10,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_tdbid_cycles(self):
        compiler = shutil.which('clang++')
        if not compiler:
            if os.environ.get('CP2077_REQUIRE_CET_TESTS') == '1':
                self.fail('CET cycle tests require clang++')
            self.skipTest('clang++ is required')
        def method(signature):
            text = (SOURCE / 'scripting/LuaVM.cpp').read_text()
            start = text.index(signature)
            opening = text.index('{',start)
            depth, end = 1, opening+1
            while depth:
                depth += (text[end] == '{') - (text[end] == '}')
                end += 1
            return text[start:end].replace('LuaVM::','Lookup::')
        extracted = '\n'.join(method(name) for name in ['std::string LuaVM::GetTDBIDString(', 'void LuaVM::RegisterTDBIDString('])
        code = r'''#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <cassert>
struct TDBID { uint64_t value; };
struct Entry { uint64_t base; std::string name; };
struct Lookup {
 std::mutex m_tdbidLock;
 std::map<uint64_t,Entry> m_tdbidLookup;
 std::map<uint64_t,std::set<uint64_t>> m_tdbidDerivedLookup;
 std::string GetTDBIDString(uint64_t,bool);
 void RegisterTDBIDString(uint64_t,uint64_t,const std::string&);
 std::string GetTDBDIDDebugString(TDBID) {return "<unknown>";}
};
''' + extracted + r'''
int main() {
 Lookup names;
 names.RegisterTDBIDString(1,0,"root");
 names.RegisterTDBIDString(1,1,"");
 assert(names.GetTDBIDString(1,false)=="root");
 names.RegisterTDBIDString(2,1,".child");
 names.RegisterTDBIDString(1,2,"cycle");
 assert(names.GetTDBIDString(2,false)=="root.child");
 names.RegisterTDBIDString(3,0,"other");
 names.RegisterTDBIDString(2,3,".new");
 assert(names.GetTDBIDString(2,false)=="other.new");
 assert(!names.m_tdbidDerivedLookup[1].contains(2));
 names.m_tdbidLookup[9]={10,"a"}; names.m_tdbidLookup[10]={9,"b"};
 assert(names.GetTDBIDString(9,false).find("cyclic")!=std::string::npos);
}
'''
        with tempfile.TemporaryDirectory(prefix='cet-tdbid-') as temporary:
            source = Path(temporary)/'lookup.cpp'
            executable = Path(temporary)/'lookup'
            source.write_text(code)
            compiled = subprocess.run([compiler,'-std=c++20','-fsanitize=address,undefined','-g',str(source),'-pthread','-o',str(executable)],capture_output=True,text=True,timeout=60)
            self.assertEqual(compiled.returncode,0,compiled.stderr)
            run = subprocess.run([str(executable)],capture_output=True,text=True,timeout=5,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


class CetRuntimeReadiness(unittest.TestCase):
    def test_fresh_checkout_skips_optional_runtime_suite_without_compiling(self):
        with tempfile.TemporaryDirectory(prefix='cet-unpatched-') as temporary:
            with patch(__name__ + '.SOURCE', Path(temporary)), \
                 patch.dict(os.environ, {'CP2077_REQUIRE_CET_TESTS': '0'}), \
                 patch(__name__ + '.subprocess.run') as run:
                result = unittest.TestResult()
                unittest.defaultTestLoader.loadTestsFromTestCase(CetRuntime).run(result)
        self.assertTrue(result.wasSuccessful(), result.errors + result.failures)
        self.assertEqual(len(result.skipped), 1)
        self.assertIn('Apply the saved CET patches', result.skipped[0][1])
        run.assert_not_called()

    def test_required_runtime_suite_rejects_unpatched_sources(self):
        with tempfile.TemporaryDirectory(prefix='cet-unpatched-') as temporary:
            with patch(__name__ + '.SOURCE', Path(temporary)), \
                 patch.dict(os.environ, {'CP2077_REQUIRE_CET_TESTS': '1'}), \
                 patch(__name__ + '.subprocess.run') as run:
                result = unittest.TestResult()
                unittest.defaultTestLoader.loadTestsFromTestCase(CetRuntime).run(result)
        self.assertEqual(len(result.errors), 1)
        self.assertIn('Apply the saved CET patches', result.errors[0][1])
        self.assertEqual(result.skipped, [])
        run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
