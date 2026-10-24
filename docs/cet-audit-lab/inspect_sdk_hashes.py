#!/usr/bin/env python3
"""Check the SDK hash references in CET's include closure, without loading the game."""
import argparse
import collections
import json
import mmap
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def inspect(addresses):
    sdk = ROOT/'sources/red4ext-sdk/include/RED4ext'
    code = '\n'.join(p.read_text() for p in (ROOT/'sources/cyber-engine-tweaks/src').rglob('*') if p.suffix in {'.cpp','.h'})
    includes = r'#include\s*[<"]RED4ext/([^>"\n]+)'
    todo, seen, contents = re.findall(includes,code), set(), [code]
    while todo:
        name = todo.pop()
        if name in seen: continue
        seen.add(name)
        path = sdk/name
        if path.is_file():
            value = path.read_text(); contents.append(value); todo.extend(re.findall(includes,value))
    refs = set(re.findall(r'(?:RED4ext::)?Detail::AddressHashes::(\w+)','\n'.join(contents)))
    definitions = {k:int(v,0) for k,v in re.findall(r'constexpr std::uint32_t (\w+) = (0x[\da-fA-F]+|\d+)(?:UL)?;', (sdk/'Detail/AddressHashes.hpp').read_text())}
    wanted = {definitions[k] for k in refs if k in definitions}
    matches = collections.Counter()
    with addresses.open('rb') as stream, mmap.mmap(stream.fileno(),0,access=mmap.ACCESS_READ) as data:
        for match in re.finditer(rb'"hash":\s*"(\d+)"',data):
            number = int(match[1])
            if number in wanted: matches[number] += 1
    return {'scope':'SDK hash references in the CET included-header closure; includes unused inline methods, not a complete call graph',
            'header_closure_count':len(seen),
            'referenced_hashes':{k:{'hash':definitions[k],'matches':matches[definitions[k]]} for k in sorted(refs) if k in definitions},
            'unknown_names':sorted(refs-definitions.keys())}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--addresses',type=Path,required=True)
    parser.add_argument('--output',type=Path,default=ROOT/'build/cet-fixes/sdk-hashes.json')
    args = parser.parse_args()
    result = inspect(args.addresses)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    entries = result['referenced_hashes']
    print('SDK hash references:',len(entries),'unique:',sum(v['matches']==1 for v in entries.values()))
    if result['unknown_names'] or any(v['matches']!=1 for v in entries.values()):
        raise SystemExit('Unknown or missing/colliding SDK hash references; inspect output before drawing conclusions.')
