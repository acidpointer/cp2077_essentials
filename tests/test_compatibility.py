"""Core profile rejection and deterministic generation; no Git writes/builds."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('compat_schema', ROOT / 'compat/tools/generate_schema.py')
schema = importlib.util.module_from_spec(spec)
spec.loader.exec_module(schema)


class CompatibilitySchemaTests(unittest.TestCase):
    def setUp(self):
        self.profile = schema.load(ROOT / 'compat/schema/2.31-to-2.50.json')

    def reject(self, mutation):
        value = copy.deepcopy(self.profile)
        mutation(value)
        with self.assertRaises(schema.SchemaError):
            schema.validate(value)

    def test_generation_is_deterministic_and_explicit(self):
        generated = schema.generate(self.profile)
        self.assertEqual(generated, schema.generate(copy.deepcopy(self.profile)))
        self.assertIn('FieldKind::Pointer', generated)
        self.assertIn('FieldKind::Zero', generated)
        self.assertIn('AddressKind::Table', generated)
        self.assertNotIn('0xb48950', generated)

    def test_unknown_or_missing_policy_is_rejected(self):
        self.reject(lambda p: p['types'][0].update(policy='identity'))
        self.reject(lambda p: p['types'][0]['fields'][0].update(kind='assume_pointer'))
        self.reject(lambda p: p.update(mod_allowlist=[]))

    def test_unknown_pointers_and_implicit_nullability_are_rejected(self):
        self.reject(lambda p: p['types'][0]['fields'][0].update(pointee=999))
        self.reject(lambda p: p['types'][0]['fields'][0].pop('nullable'))
        self.reject(lambda p: p['types'][0]['fields'][0].update(width=4))

    def test_overlaps_and_unbounded_ranges_are_rejected(self):
        self.reject(lambda p: p['types'][0]['fields'][1].update(guest=0))
        self.reject(lambda p: p['types'][0]['fields'][1].update(native=0))
        self.reject(lambda p: p['types'][0]['fields'][0].update(width=2048))
        self.reject(lambda p: p['types'][0]['fields'][0].update(guest=-1))

    def test_value_requires_every_guest_byte_classified(self):
        self.reject(lambda p: p['types'][0]['fields'].pop())
        self.reject(lambda p: p['types'][0]['fields'][-1].update(native=100))

    def test_equal_sizes_do_not_enable_opaque_copy(self):
        self.reject(lambda p: p['types'][3]['fields'].append(copy.deepcopy(p['types'][0]['fields'][0])))

    def test_array_allocator_union_requires_reviewed_discriminator(self):
        self.reject(lambda p: p['types'][1]['fields'][4].update(empty_pointee=2))
        self.reject(lambda p: p['types'][1]['fields'][4].update(capacity_native=8))
        self.reject(lambda p: p['types'][1]['fields'][4].pop('empty_pointee'))
        self.reject(lambda p: p['types'][0]['fields'][0].update(empty_pointee=11,capacity_native=96))

    def test_primary_collisions_and_wrong_address_kinds_are_rejected(self):
        self.reject(lambda p: p['operations'][1].update(primary=p['operations'][0]['primary']))
        self.reject(lambda p: p['operations'][2].update(kind='function'))
        self.reject(lambda p: p['operations'][2].update(secondary=''))
        self.reject(lambda p: p['operations'][0].update(primary=True))

    def test_duplicate_json_keys_are_not_silently_overwritten(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'invalid.json'
            path.write_text('{"version":1,"version":2}')
            with self.assertRaises(schema.SchemaError):
                schema.load(path)

    def test_profile_matches_preserved_layout_evidence(self):
        measured = json.loads((ROOT / 'docs/legacy-compat-readiness-lab/layout-comparison.json').read_text())['types']
        frame = self.profile['types'][0]
        self.assertEqual(frame['guest_size'], measured['CStackFrame']['old']['size'])
        self.assertEqual(frame['native_size'], measured['CStackFrame']['current']['size'])
        for field in frame['fields']:
            if field['name'] in measured['CStackFrame']['old']['fields']:
                self.assertEqual(field['guest'], measured['CStackFrame']['old']['fields'][field['name']]['offset'])
                self.assertEqual(field['native'], measured['CStackFrame']['current']['fields'][field['name']]['offset'])


if __name__ == '__main__':
    unittest.main()
