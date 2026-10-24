"""Read-only target identity/address audit. Outputs facts, never game bytes."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct

parser = argparse.ArgumentParser()
parser.add_argument("--exe", type=Path, required=True)
parser.add_argument("--addresses", type=Path, required=True)
parser.add_argument("--out", type=Path, default=Path("build/compat-readiness/target.json"))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
binary = args.exe.read_bytes()
pe = struct.unpack_from("<I", binary, 0x3C)[0]
assert binary[pe:pe + 4] == b"PE\0\0"
opt = pe + 24
assert struct.unpack_from("<H", binary, opt)[0] == 0x20B
base = struct.unpack_from("<Q", binary, opt + 24)[0]
table = opt + struct.unpack_from("<H", binary, pe + 20)[0]
sections = {}
for i in range(struct.unpack_from("<H", binary, pe + 6)[0]):
    pos = table + 40 * i
    size, rva, raw_size, raw = struct.unpack_from("<IIII", binary, pos + 8)
    sections[binary[pos:pos + 8].rstrip(b"\0").decode()] = (rva, size, raw, raw_size)

def file_offset(rva):
    for start, _, raw, size in sections.values():
        if start <= rva < start + size:
            return raw + rva - start
    raise ValueError(hex(rva))

names = ["CClassFunction_ctor", "CClassStaticFunction_ctor", "CGlobalFunction_ctor", "CStackFrame_dtor",
         "CRTTISystem_Get", "Memory_Vault_Alloc", "Memory_Vault_Free", "CBaseFunction_ExecuteNative",
         "CBaseFunction_ExecuteScripted", "OpcodeHandlers"]
header = (root / "sources/red4ext-sdk/include/RED4ext/Detail/AddressHashes.hpp").read_text()
hashes = {}
for name in names:
    match = re.search(r"\b" + name + r"\s*=\s*(0x[0-9A-Fa-f]+|[0-9]+)", header)
    hashes[name] = int(match.group(1), 0)
reverse = {value: name for name, value in hashes.items()}
assert len(reverse) == len(hashes)
address_bytes = args.addresses.read_bytes()
matches = {name: [] for name in names}
for entry in json.loads(address_bytes)["Addresses"]:
    primary = int(entry["hash"], 0)
    if primary not in reverse:
        continue
    section, offset = (int(value, 16) for value in entry["offset"].split(":"))
    rva = sections[{1: ".text", 2: ".rdata", 3: ".data"}[section]][0] + offset
    matches[reverse[primary]].append({"primary": primary, "secondary": entry["secondary hash"],
                                     "rva": hex(rva), "symbol": entry.get("symbol")})
assert all(len(value) == 1 for value in matches.values()), "Missing/ambiguous address; no first-match policy"
# Version resource fixed-info signature; report every distinct candidate.
versions = set()
position = 0
while True:
    position = binary.find(b"\xBD\x04\xEF\xFE", position)
    if position < 0:
        break
    if position + 16 <= len(binary) and struct.unpack_from("<I", binary, position + 4)[0] == 0x10000:
        high, low = struct.unpack_from("<II", binary, position + 8)
        versions.add((high >> 16, high & 0xFFFF, low >> 16, low & 0xFFFF))
    position += 4
record = {"exe_sha256": hashlib.sha256(binary).hexdigest(), "address_db_sha256": hashlib.sha256(address_bytes).hexdigest(),
          "version_resource_candidates": sorted(versions), "image_base": hex(base), "selected": matches}
# These ranges are the bodies examined in this research build, not reusable
# address guesses. Record their digests only when the exact executable matches.
if record["exe_sha256"] == "d36e74c47592e9c656ae250078cb40b2be63c782af5bfbeff2280d43e653fab4":
    reviewed = {"class_static_ctor": (0xB48950, 0xB489C3), "class_ctor": (0x6BECE4, 0x6BED57),
                "global_ctor": (0xB488F8, 0xB48950), "base_ctor": (0x6BD564, 0x6BD64A),
                "function_dtor": (0x2624C3C, 0x2624D68), "class_deleting_dtor": (0x2624E28, 0x2624E5C),
                "frame_dtor_entry": (0x263D304, 0x263D30D), "call_native": (0x2624E5C, 0x2624F3C)}
    record["reviewed_code_ranges"] = {name: {"rva_begin": hex(start), "rva_end": hex(end),
        "sha256": hashlib.sha256(binary[file_offset(start):file_offset(end - 1) + 1]).hexdigest()}
        for name, (start, end) in reviewed.items()}
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps(record, indent=2) + "\n")
print(json.dumps({"exe_sha256": record["exe_sha256"], "version_resource_candidates": record["version_resource_candidates"],
                  "unique_selected_operations": len(matches), "reviewed_ranges": len(record.get("reviewed_code_ranges", {}))}))
