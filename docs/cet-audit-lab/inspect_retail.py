#!/usr/bin/env python3
"""Read-only CET hash/PE and SDK comparison; never launches or changes the game."""
import argparse
import collections
import difflib
import hashlib
import json
import mmap
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]


def inspect(exe, addresses, reference):
    source = ROOT / 'sources/cyber-engine-tweaks/src'
    code = '\n'.join(path.read_text() for path in source.rglob('*') if path.suffix in {'.h', '.cpp'})
    names = dict((name, int(number)) for name, number in re.findall(
        r'constexpr uint32_t (\w+)\s*=\s*(\d+)UL;', (source / 'reverse/Addresses.h').read_text()))
    used = set(re.findall(r'CyberEngineTweaks::AddressHashes::(\w+)', code))
    matches = collections.defaultdict(list)
    requested = set(names.values())
    with addresses.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        address_sha = hashlib.sha256(data).hexdigest()
        for obj in re.finditer(rb'\{[^{}]*\}', data):
            entry = obj.group()
            hash_match = re.search(rb'"hash":\s*"(\d+)"', entry)
            if hash_match and int(hash_match[1]) in requested:
                value = json.loads(entry)
                matches[int(hash_match[1])].append(value)
    binary = exe.read_bytes()
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    assert binary[:2] == b'MZ' and binary[pe:pe+4] == b'PE\0\0'
    machine, section_count, timestamp, _, _, opt_size, _ = struct.unpack_from('<HHIIIHH', binary, pe + 4)
    sections = []
    for index in range(section_count):
        offset = pe + 24 + opt_size + index * 40
        name, virtual_size, rva, raw_size, raw_offset = struct.unpack_from('<8sIIII', binary, offset)
        sections.append({'name': name.rstrip(b'\0').decode(), 'rva': rva,
                         'virtual_size': virtual_size, 'raw_size': raw_size, 'raw_offset': raw_offset})
    version_offset = binary.find(struct.pack('<I', 0xFEEF04BD))
    if version_offset < 0:
        raise ValueError('PE fixed file-version information is absent')
    versions = struct.unpack_from('<6I', binary, version_offset)
    file_version = '.'.join(str(value) for value in [versions[2] >> 16, versions[2] & 65535,
                                                    versions[3] >> 16, versions[3] & 65535])
    named_sections = {'.text': 1, '.rdata': 2, '.data': 3}
    segments = {value: section['rva'] for section in sections
                if (value := named_sections.get(section['name'])) is not None}
    for rows in matches.values():
        for row in rows:
            segment, offset = (int(part, 16) for part in row['offset'].split(':'))
            if segment not in segments:
                raise ValueError('Unknown address database segment: ' + str(segment))
            row['rva'] = segments[segment] + offset
    result = {'exe_sha256': hashlib.sha256(binary).hexdigest(), 'file_version': file_version,
              'machine': hex(machine), 'timestamp': timestamp, 'address_sha256': address_sha,
              'sections': sections,
              'hashes': {name: {'hash': number, 'used_in_source': name in used, 'matches': matches[number]}
                         for name, number in sorted(names.items())}}
    if reference:
        root = ROOT / 'sources/red4ext-sdk/include/RED4ext'
        direct = set(re.findall(r'#include\s*[<"]RED4ext/([^>"\n]+)', code))
        seen = set()
        todo = list(direct)
        while todo:
            name = todo.pop()
            if name in seen:
                continue
            seen.add(name)
            path = root / name
            if path.is_file():
                todo.extend(x for x in re.findall(r'#include\s*[<"]RED4ext/([^>"\n]+)', path.read_text()) if x not in seen)
        differences = {}
        for name in sorted(seen):
            local = root / name
            original = reference / name
            if local.exists() and original.exists() and local.read_bytes() != original.read_bytes():
                differences[name] = ''.join(difflib.unified_diff(
                    original.read_text().splitlines(True), local.read_text().splitlines(True),
                    fromfile='CET upstream/' + name, tofile='shared/' + name))
        result['sdk'] = {'direct_headers': sorted(direct), 'closure_count': len(seen),
                         'missing_shared': sorted(name for name in seen if not (root / name).is_file()),
                         'missing_reference': sorted(name for name in seen if not (reference / name).is_file()),
                         'differences': differences}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--addresses', type=Path, required=True)
    parser.add_argument('--sdk-reference', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/cet-audit/retail.json')
    args = parser.parse_args()
    result = inspect(args.exe, args.addresses, args.sdk_reference)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    entries = result['hashes'].values()
    print('Game:', result['file_version'], result['exe_sha256'])
    print('CET hash matches:', sum(len(entry['matches']) == 1 for entry in entries), '/', len(result['hashes']))
    for name, entry in result['hashes'].items():
        print(name, 'used=' + str(entry['used_in_source']), 'matches=' + str(len(entry['matches'])),
              ', '.join(hex(row['rva']) + ' ' + row.get('symbol', '') for row in entry['matches']))
    if result.get('sdk'):
        print('SDK closure/differing headers:', result['sdk']['closure_count'], len(result['sdk']['differences']))
