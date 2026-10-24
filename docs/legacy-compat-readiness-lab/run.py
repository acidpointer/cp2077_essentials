"""Isolated Win64 mechanism probes. No downloads, Git writes, or game launches."""
from pathlib import Path
import argparse
import hashlib
import io
import json
import os
import signal
import subprocess
import tarfile

OLD = "f9ad5334362a8a0815baf58f47615697485feb52"
parser = argparse.ArgumentParser()
parser.add_argument("--out", type=Path, default=Path("build/compat-readiness/lab"))
parser.add_argument("--compile-only", action="store_true")
parser.add_argument("--run-only", action="store_true")
parser.add_argument("--wine", default="wine")
parser.add_argument("--runtime", type=Path)
parser.add_argument("--only", action="append", help="Run only this executable; compilation still builds the bundle")
args = parser.parse_args()
assert not (args.compile_only and args.run_only)
root = Path(__file__).resolve().parents[2]
src = Path(__file__).resolve().parent
out = args.out.resolve()
out.mkdir(parents=True, exist_ok=True)
xwin = root / "build/toolchains/xwin"
old = out / "old-sdk"
result_path = out / "results.json"

def portable(text):
    return str(text).replace(str(out), "<out>").replace(str(root), "<repo>")

def current_headers_digest():
    include = root / "sources/red4ext-sdk/include"
    digest = hashlib.sha256()
    for path in sorted(include.rglob("*.hpp")):
        digest.update(path.relative_to(include).as_posix().encode() + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()

if args.run_only:
    record = json.loads(result_path.read_text())
    for name, digest in record["binaries"].items():
        assert hashlib.sha256((out / name).read_bytes()).hexdigest() == digest, name
    for name, digest in record["sources"].items():
        assert hashlib.sha256((src / name).read_bytes()).hexdigest() == digest, name
else:
    archive = subprocess.check_output(["git", "-C", str(root / "sources/red4ext-sdk"), "archive", OLD, "include/RED4ext"])
    with tarfile.open(fileobj=io.BytesIO(archive)) as package:
        package.extractall(old, filter="data")
    overlay = out / "old-vfs.json"
    overlay.write_text(json.dumps({"version": 0, "case-sensitive": False, "roots": [
        {"type": "file", "name": str(path), "external-contents": str(path)}
        for path in sorted((old / "include").rglob("*")) if path.is_file()
    ]}))
    record = {"old_sdk": OLD, "compiler": subprocess.check_output(["clang-cl", "--version"], text=True),
              "source_lock_sha256": hashlib.sha256((root / "sources.lock.json").read_bytes()).hexdigest(),
              "current_sdk_headers_sha256": current_headers_digest(),
              "sources": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(src.glob("*.cpp"))},
              "builds": [], "runs": []}

def save():
    result_path.write_text(portable(json.dumps(record, indent=2)) + "\n")

targets = [
    ("dependency.dll", "init_dll.cpp", ["/LD", "/DMODULE_ID=1"], []),
    ("initializer.dll", "init_dll.cpp", ["/LD", "/DMODULE_ID=2"], ["dependency.lib"]),
    ("platform.exe", "platform.cpp", [], []),
    ("layouts-old.exe", "layouts.cpp", ["/I" + str(old / "include"), "/clang:-ivfsoverlay", "/clang:" + str(out / "old-vfs.json")], []),
    ("layouts-current.exe", "layouts.cpp", ["/I" + str(root / "sources/red4ext-sdk/include")], []),
    ("chain_guest.dll", "chain_guest.cpp", ["/LD", "/I" + str(old / "include"), "/clang:-ivfsoverlay", "/clang:" + str(out / "old-vfs.json")], []),
    ("chain_host.exe", "chain_host.cpp", ["/I" + str(root / "sources/red4ext-sdk/include")], []),
]
if not args.run_only:
    base = ["clang-cl", "-fuse-ld=lld", "/nologo", "/std:c++20", "/EHa", "/MT", "/O2", "/Oi", "/Gy"]
    base += ["/imsvc" + str(xwin / part) for part in ["crt/include", "sdk/include/ucrt", "sdk/include/shared", "sdk/include/um"]]
    libs = ["/libpath:" + str(xwin / part) for part in ["crt/lib/x86_64", "sdk/lib/ucrt/x86_64", "sdk/lib/um/x86_64"]]
    for target, source, flags, link in targets:
        command = base + flags + [str(src / source), "/Fe:" + target, "/Fo:" + target + ".obj", "/link", "/OPT:REF"] + libs + link
        result = subprocess.run(command, cwd=out, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        record["builds"].append({"target": target, "command": command, "exit_code": result.returncode, "log": result.stdout})
        save()
        print(target, result.returncode, flush=True)
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
    record["binaries"] = {target: hashlib.sha256((out / target).read_bytes()).hexdigest() for target, *_ in targets}
    assert current_headers_digest() == record["current_sdk_headers_sha256"], "SDK headers changed during compilation"
    assert hashlib.sha256((root / "sources.lock.json").read_bytes()).hexdigest() == record["source_lock_sha256"], "Lockfile changed during compilation"
    save()
if args.compile_only:
    raise SystemExit(0)

command = [args.wine]
env = os.environ.copy()
env.update(WINEPREFIX=str(out / ("proton-prefix" if args.runtime else "wine-prefix")), WINEDEBUG="-all", DISPLAY="",
           WINEDLLOVERRIDES="winemenubuilder.exe=d", XDG_CACHE_HOME=str(out / "cache"))
if args.runtime:
    command = [str(args.runtime.resolve()), "--verb=run", "--", args.wine]
    env.update(PATH=str(Path(args.wine).parent) + os.pathsep + env.get("PATH", ""),
               STEAM_COMPAT_APP_ID="0", SteamAppId="0", SteamGameId="0", XDG_CONFIG_HOME=str(out / "config"))
record["runtime"] = subprocess.check_output(command + ["--version"], env=env, text=True)
record["runs"] = []
for target, *_ in targets:
    if not target.endswith(".exe"):
        continue
    if args.only and target not in args.only:
        continue
    log_path = out / (target + ".log")
    with log_path.open("w") as log:
        process = subprocess.Popen(command + [str(out / target)], cwd=out, env=env,
                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=45)
            result = {"target": target, "returncode": code}
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            result = {"target": target, "timeout_seconds": 45}
    result["output"] = log_path.read_text(errors="replace")
    record["runs"].append(result)
    save()
    print(portable(json.dumps(result)), flush=True)
    if result.get("returncode", 124):
        raise SystemExit(result.get("returncode", 124))
