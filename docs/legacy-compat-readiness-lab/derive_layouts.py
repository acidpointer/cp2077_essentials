"""Turn the two compiler-built layout probes into a comparison, not an ABI policy."""
import argparse
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("results", type=Path)
parser.add_argument("--out", type=Path, required=True)
args = parser.parse_args()
record = json.loads(args.results.read_text())

def read(target):
    run, = (r for r in record["runs"] if r["target"] == target)
    assert run["returncode"] == 0
    types = {}
    for line in run["output"].splitlines():
        if line.startswith("TYPE "):
            _, name, size, alignment = line.split()
            assert name not in types
            types[name] = {"size": int(size), "alignment": int(alignment), "fields": {}}
        elif line.startswith("FIELD "):
            _, path, offset, size = line.split()
            name, field = path.split(".")
            typ = types[name]
            assert field not in typ["fields"]
            assert 0 <= int(offset) < int(offset) + int(size) <= typ["size"]
            typ["fields"][field] = {"offset": int(offset), "size": int(size)}
    assert len(types) == 11
    return types

old, current = read("layouts-old.exe"), read("layouts-current.exe")
assert old.keys() == current.keys()
assert old["CClassFunction"]["fields"]["parent"]["offset"] == 0xB0
assert current["CClassFunction"]["fields"]["parent"]["offset"] == 0xF0
assert old["CStackFrame"]["size"] == 0x68 and current["CStackFrame"]["size"] == 0xA8
comparison = {}
for name in old:
    assert old[name]["fields"].keys() == current[name]["fields"].keys()
    comparison[name] = {"old": old[name], "current": current[name],
                        "representation_policy": "UNREVIEWED"}
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps({
    "old_sdk": record["old_sdk"],
    "source_lock_sha256": record["source_lock_sha256"],
    "current_sdk_headers_sha256": record.get("current_sdk_headers_sha256"),
    "limits": "Selected fields only. Equal offsets/sizes do not establish pointer, ownership, virtual-slot or call compatibility.",
    "types": comparison
}, indent=2) + "\n")
print(f"Compared {len(comparison)} types and {sum(len(v['old']['fields']) for v in comparison.values())} selected fields")
