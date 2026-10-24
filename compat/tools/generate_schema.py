"""Validate the bounded core profile and emit immutable C++ descriptors.

No dependencies/downloads, SDK writes or Git mutation. This is not automatic
semantic inference from equal sizes. Unknown pointer contracts are rejected.
"""
import argparse
import json
from pathlib import Path
import re


class SchemaError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise SchemaError(message)


def keys(value, required, optional=()):
    require(isinstance(value, dict), 'expected object')
    require(set(required) <= value.keys() <= set(required) | set(optional), 'missing or unknown keys')


def integer(value, minimum=0, maximum=1024):
    require(type(value) is int and minimum <= value <= maximum, f'invalid integer: {value!r}')


def name(value):
    require(isinstance(value, str) and re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', value), 'invalid identifier')


def validate(profile):
    keys(profile, ('version', 'scope', 'source_abi', 'target_abi', 'types', 'operations'))
    require(type(profile['version']) is int and profile['version'] == 1 and profile['scope'] == 'isolated-core', 'unsupported profile version/scope')
    for key in ('source_abi', 'target_abi'):
        require(isinstance(profile[key], str) and re.fullmatch(r'[A-Za-z0-9_.-]+', profile[key]), 'invalid ABI')
    require(isinstance(profile['types'], list) and profile['types'], 'types required')
    ids, names = set(), set()
    for typ in profile['types']:
        keys(typ, ('id', 'name', 'policy', 'guest_size', 'native_size', 'alignment', 'fields'))
        integer(typ['id'], 1, 65535)
        name(typ['name'])
        require(typ['id'] not in ids and typ['name'] not in names, 'duplicate type')
        ids.add(typ['id'])
        names.add(typ['name'])
    for typ in profile['types']:
        policy = typ['policy']
        require(policy in ('value', 'live_view', 'opaque'), 'unknown representation policy')
        for key in ('guest_size', 'native_size'):
            integer(typ[key], 0 if policy == 'opaque' else 1)
        require(type(typ['alignment']) is int and typ['alignment'] in (1, 2, 4, 8, 16), 'invalid alignment')
        require(not typ['guest_size'] % typ['alignment'] and not typ['native_size'] % typ['alignment'], 'unaligned size')
        require(isinstance(typ['fields'], list), 'fields must be a list')
        require(policy != 'opaque' or not typ['fields'], 'opaque type cannot supply copy fields')
        guest, native, fields = set(), set(), set()
        for field in typ['fields']:
            keys(field, ('name', 'guest', 'native', 'width', 'kind'), ('pointee', 'nullable', 'empty_pointee', 'capacity_native'))
            name(field['name'])
            require(field['name'] not in fields, 'duplicate field')
            fields.add(field['name'])
            integer(field['guest'])
            integer(field['width'], 1)
            require(field['kind'] in ('scalar', 'pointer', 'zero', 'padding'), 'unknown field policy')
            if field['kind'] == 'pointer':
                integer(field.get('pointee'), 1, 65535)
                require(field['width'] == 8 and field.get('pointee') in ids, 'unbounded/untyped pointer')
                require(type(field.get('nullable')) is bool, 'pointer nullability must be explicit')
            else:
                require('pointee' not in field and 'nullable' not in field, 'non-pointer contract')
            if 'empty_pointee' in field:
                integer(field['empty_pointee'], 1, 65535)
                integer(field.get('capacity_native'))
                require(policy == 'live_view' and field['kind'] == 'pointer', 'allocator union requires live pointer')
                require(any(t['id'] == field['empty_pointee'] and t['policy'] == 'opaque' for t in profile['types']), 'unknown allocator identity')
                require(type(field['native']) is int and field['capacity_native'] == field['native'] + 8 and
                    field['capacity_native'] + 8 <= typ['native_size'] and any(
                    f['kind'] == 'scalar' and f['native'] == field['capacity_native'] and f['guest'] == field['guest'] + 8 and
                    f['width'] == 8 for f in typ['fields']), 'missing capacity field')
            else:
                require('capacity_native' not in field, 'capacity without allocator union')
            extent = set(range(field['guest'], field['guest'] + field['width']))
            require(max(extent) < typ['guest_size'] and not guest & extent, 'guest overlap/range')
            guest |= extent
            if field['kind'] == 'padding':
                require(field['native'] is None, 'padding must not overwrite native state')
            else:
                integer(field['native'])
                extent = set(range(field['native'], field['native'] + field['width']))
                require(max(extent) < typ['native_size'] and not native & extent, 'native overlap/range')
                native |= extent
        if policy == 'value':
            require(guest == set(range(typ['guest_size'])), 'VALUE must classify every guest byte')
    require(isinstance(profile['operations'], list), 'operations must be a list')
    names, primary = set(), set()
    for operation in profile['operations']:
        keys(operation, ('id', 'primary', 'secondary', 'kind', 'signature', 'table_size'))
        name(operation['id'])
        integer(operation['primary'], 1, 0xFFFFFFFF)
        require(operation['id'] not in names and operation['primary'] not in primary, 'ambiguous operation identity')
        names.add(operation['id'])
        primary.add(operation['primary'])
        require(isinstance(operation['secondary'], str) and re.fullmatch(r'[0-9a-f]{1,64}', operation['secondary']), 'invalid secondary identity')
        require(operation['kind'] in ('function', 'table'), 'unknown address kind')
        require(isinstance(operation['signature'], str) and re.fullmatch(r'[A-Za-z0-9_*,()]+', operation['signature']), 'invalid signature')
        integer(operation['table_size'], 0, 256)
        require(bool(operation['table_size']) == (operation['kind'] == 'table'), 'table/function mismatch')
    return profile


def load(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, 'duplicate JSON key')
            result[key] = value
        return result
    return validate(json.loads(Path(path).read_text(), object_pairs_hook=unique))


def generate(profile):
    validate(profile)
    lines = ['// Generated from a validated profile. Do not edit.', '#pragma once',
             '#include <compat/Schema.hpp>', '#include <array>', 'namespace compat::profile {']
    policies = {'value': 'Value', 'live_view': 'LiveView', 'opaque': 'Opaque'}
    for typ in profile['types']:
        lines.append(f'inline constexpr TypeId {typ["name"]} = {typ["id"]};')
        lines.append(f'inline constexpr std::array<Field, {len(typ["fields"])}> {typ["name"]}Fields{{{{')
        for field in typ['fields']:
            lines.append('    {' + ', '.join([json.dumps(field['name']), str(field['guest']), str(field['native'] or 0),
                str(field['width']), 'FieldKind::' + field['kind'].capitalize(), str(field.get('pointee', 0)),
                str(field.get('nullable', False)).lower(), str(field.get('empty_pointee', 0)),
                str(field.get('capacity_native', 0))]) + '},')
        lines.append('}};')
    lines.append(f'inline constexpr std::array<Type, {len(profile["types"])}> Types{{{{')
    for typ in profile['types']:
        lines.append('    {' + ', '.join([str(typ['id']), json.dumps(typ['name']), 'Policy::' + policies[typ['policy']],
            str(typ['guest_size']), str(typ['native_size']), str(typ['alignment']), typ['name'] + 'Fields']) + '},')
    lines.append('}};')
    lines.append(f'inline constexpr std::array<Operation, {len(profile["operations"])}> Operations{{{{')
    for operation in profile['operations']:
        lines.append('    {' + ', '.join([json.dumps(operation['id']), str(operation['primary']) + 'u',
            json.dumps(operation['secondary']), 'AddressKind::' + operation['kind'].capitalize(),
            json.dumps(operation['signature']), str(operation['table_size'])]) + '},')
    lines.extend(['}};', 'inline constexpr Schema Core{' + json.dumps(profile['source_abi']) + ', ' +
                  json.dumps(profile['target_abi']) + ', Types, Operations};', '}'])
    return '\n'.join(lines) + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    output = generate(load(args.profile))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(output)
