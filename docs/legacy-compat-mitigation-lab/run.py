"""Build and run an isolated experiment; no Git metadata or game files are written."""
from pathlib import Path
import argparse
import hashlib
import io
import json
import os
import signal
import subprocess
import tarfile

OLD_SDK = "f9ad5334362a8a0815baf58f47615697485feb52"
parser = argparse.ArgumentParser()
parser.add_argument("--out", type=Path, default=Path("build/compat-mitigation"))
parser.add_argument("--compile-only", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
src = Path(__file__).resolve().parent
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
xwin = root / "build/toolchains/xwin"
old = out / "old-sdk"
old.mkdir(exist_ok=True)
# git archive is read-only: no temporary index, object writes, fetch, or checkout.
archive = subprocess.check_output([
    "git", "-C", str(root / "sources/red4ext-sdk"), "archive", OLD_SDK, "include/RED4ext"
])
with tarfile.open(fileobj=io.BytesIO(archive)) as package:
    package.extractall(old, filter="data")
# Historical headers use mixed-case RTTI include paths. Model the Windows
# filesystem with Clang's VFS; do not edit headers or create alias directories.
overlay = out / "old-sdk-vfs.json"
overlay.write_text(json.dumps({"version": 0, "case-sensitive": False, "roots": [
    {"type": "file", "name": str(path), "external-contents": str(path)}
    for path in sorted((old / "include").rglob("*")) if path.is_file()
]}))

def portable(text):
    return str(text).replace(str(out), "<out>").replace(str(root), "<repo>")

record = {"old_sdk": OLD_SDK, "compiler": subprocess.check_output(["clang-cl", "--version"], text=True),
          "sources_lock_sha256": hashlib.sha256((root / "sources.lock.json").read_bytes()).hexdigest(),
          "sources": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(src.glob("*.cpp"))},
          "builds": []}
record_path = out / "results.json"

def save():
    record_path.write_text(portable(json.dumps(record, indent=2)) + "\n")

base = ["clang-cl", "-fuse-ld=lld", "/nologo", "/std:c++20", "/EHsc", "/MT", "/O2", "/Gy"]
base += ["/imsvc" + str(xwin / part) for part in ["crt/include", "sdk/include/ucrt", "sdk/include/shared", "sdk/include/um"]]
libs = ["/libpath:" + str(xwin / part) for part in ["crt/lib/x86_64", "sdk/lib/ucrt/x86_64", "sdk/lib/um/x86_64"]]
for name, include in [("guest", old / "include"), ("host", root / "sources/red4ext-sdk/include")]:
    command = base + ["/I" + str(include), str(src / (name + ".cpp")), "/Fo:" + name + ".obj",
                      "/Fe:" + name + (".dll" if name == "guest" else ".exe")]
    if name == "guest":
        command += ["/LD", "/clang:-ivfsoverlay", "/clang:" + str(overlay)]
    command += ["/link", "/OPT:REF"] + libs
    result = subprocess.run(command, cwd=out, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    record["builds"].append({"name": name, "command": command, "exit_code": result.returncode, "log": result.stdout})
    save()
    print(name, result.returncode, result.stdout, flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)
record["binaries"] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in [out / "guest.dll", out / "host.exe"]}
save()
if args.compile_only:
    raise SystemExit(0)

env = os.environ.copy()
env.update(WINEPREFIX=str(out / "wine-prefix"), WINEDEBUG="-all", DISPLAY="",
           WINEDLLOVERRIDES="winemenubuilder.exe=d", XDG_CACHE_HOME=str(out / "cache"))
record["wine"] = subprocess.check_output(["wine", "--version"], text=True)
with (out / "wine.log").open("w") as log:
    process = subprocess.Popen(["wine", str(out / "host.exe")], cwd=out, env=env,
                               stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    try:
        code = process.wait(timeout=45)
        record["run"] = {"returncode": code}
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
        record["run"] = {"timeout_seconds": 45}
record["run"]["output"] = (out / "wine.log").read_text(errors="replace")
save()
print(portable(json.dumps(record["run"], indent=2)), flush=True)
raise SystemExit(record["run"].get("returncode", 124))
