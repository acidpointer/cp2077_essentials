"""Run the actual patched XML helpers with the upstream-pinned pugixml on Linux."""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


class InputLoaderXml(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = pathlib.Path(__file__).resolve().parents[1]
        source = root / 'sources/input-loader'
        pugixml = source / 'deps/pugixml/src/pugixml.cpp'
        compiler = shutil.which('c++')
        if not compiler or not pugixml.exists() or not (source / 'src/Merge.hpp').exists():
            message = 'Initialize dependencies and apply patches to run native Input Loader XML tests.'
            if os.environ.get('CP2077_REQUIRE_INPUT_TESTS') == '1':
                raise RuntimeError(message)
            raise unittest.SkipTest(message)
        cls.temporary = tempfile.TemporaryDirectory(prefix='cp2077-input-xml-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder = pathlib.Path(cls.temporary.name)
        cls.program = cls.folder / 'input-loader-tests'
        command = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-I', str(source / 'src'), '-I', str(pugixml.parent),
                   str(root / 'tests/input_loader.cpp'), str(pugixml), '-o', str(cls.program)]
        subprocess.run(command, check=True, capture_output=True, text=True)
        for name in ['valid.xml', 'unicode-інпут.xml', 'upper.XML', 'mixed.XmL', 'text.xml.txt']:
            (cls.folder / name).write_text('<bindings><context name="test"/></bindings>')
        (cls.folder / 'malformed.xml').write_text('<bindings><context name="partial"/><broken></bindings>')
        (cls.folder / 'wrong-root.xml').write_text('<other/>')
        (cls.folder / 'empty.xml').write_text('')
        (cls.folder / 'directory.xml').mkdir()

    def run_case(self, name):
        result = subprocess.run([self.program, name, self.folder], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_malformed_partial_and_missing_root_documents(self):
        self.run_case('parse')

    def test_filters_regular_xml_files_case_insensitively(self):
        self.run_case('filter')

    def test_preserves_action_and_id_matching(self):
        self.run_case('keyed')

    def test_append_preserves_children_and_replace_replaces_them(self):
        self.run_case('append_replace')

    def test_routes_supported_nodes_and_rejects_missing_destination(self):
        self.run_case('routes')

    def test_empty_keys_do_not_replace_unrelated_nodes(self):
        self.run_case('unkeyed')


if __name__ == '__main__':
    unittest.main()
