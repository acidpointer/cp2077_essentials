Implementation-readiness laboratory, 2026-10-24
==============================================

Research conclusions: docs/legacy-plugin-compat-readiness.txt.
Evidence entries: E22-E28 in docs/legacy-plugin-compat-evidence.txt.
This is a standalone Win64 laboratory, not a loader patch or game test.

Contents
--------

init_dll.cpp/platform.cpp:
  Dependency/parent DLL notification ordering; compiled typed slot adapters;
  a generated bridge with explicit unwind information and C++/SEH controls.
  The notification callback performs no external calls or allocation. Separate
  fixture initialization/DllMain callbacks record their later events.

chain_guest.cpp/chain_host.cpp:
  Actual historical SDK callback, allocation/Create, inline class insertion/
  lookup, GetParameter and opcode table lookup against fake native services.
  Two adapted opcode table entries, separate pointer arrays, nested frame
  identities, exception cleanup and callback destruction are exercised.
  Negative control: a retained metadata change is stale until an explicit
  publication barrier. The fixture does not discover those barriers in CP2077.
  Native frame/class/function storage is an ABI byte-storage model. It does
  not execute real engine constructors, destructors, allocators or registration.
  Fake teardown explicitly detaches the registry; real engine detach is unknown.
  Old SDK operations retain their header implementations. RED4EXT_INLINE is
  set to inline for selected definitions to avoid linking unused SDK vtables.
  No SDK file is edited. Only the tested opcode entries are populated; this
  is not a general opcode table or a production unsupported-operation policy.

layouts.cpp/derive_layouts.py/layout-comparison.json:
  Compiler-derived sizes/alignments and selected fields from actual historical
  and current headers: 11 types, 33 fields. Policies remain UNREVIEWED.

lifetime_model.py/lifetime-model.json:
  Two-actor schedule model for admission/unload, plus retained-owner and guest
  address-reuse invariants. One bad check/increment schedule among 15; zero bad
  atomic-admission schedules among 10. No weak-memory or runtime stress proof.

inspect_target.py/target.json:
  Read-only executable/version/address evidence. Ten selected identities are
  unique in the inspected database. Recorded secondary identities and code-range
  hashes supplement primary hashes. Version extraction reports fixed-info
  candidates; it is not a complete resource-tree parser. The reviewed ranges
  are fingerprint-specific facts and are not reusable resolver guesses.
  No executable bytes, game data or host-specific installation path is saved.

run.py/results-system-wine.json/results-ge-proton.json:
  Compiler commands/logs, actual C++ source and binary hashes, SDK header-tree
  fingerprint, root lockfile fingerprint, runtime version and fixture output.
  The header fingerprint hashes sorted include-relative .hpp names, NUL, and
  each file's SHA256 digest bytes. Headers/lockfile are checked again after
  compiling, so a concurrent change rejects the bundle. This matters because
  independent work may update the workspace while research is in progress.
  The historical SDK is exactly f9ad5334362a8a0815baf58f47615697485feb52.
  Current headers are the patched working contents, not the bare base commit.
  SHA256SUMS covers this bundle except itself.

Recorded results
----------------

Compiler: clang-cl 23.1.3, Microsoft x64 ABI, lld-link, /O2 /EHa /MT.
Both runs execute the same seven compiled artifacts (three DLLs, four EXEs).
System Wine: 11.20 (Staging), separate prefix.
GE-Proton11-11: Wine 11.0 (Staging), SteamLinuxRuntime_4, separate prefix;
ntsync reported active. This does not invoke the Proton launcher or the game.

Each runtime returned zero for platform.exe, layouts-old.exe,
layouts-current.exe and chain_host.exe. Platform checks printed:
  LOADER events=1:1,1:2,1:3,2:1,2:2,2:3,2:4,2:5,1:5
  ADAPTER PASS sret mixed_arguments nested_domains worker CXX_cleanup
  UNWIND PASS registered_generated_code CXX SEH virtual_stack_walk
Chain checks printed:
  CHAIN PASS callbacks=1 copies=2 live=0 native_frames_destroyed=3 nested=1 barriers=2
  MIRROR_NEGATIVE PASS retained mutation stale until explicit barrier

Limits: no native Windows, CFG/CET configuration matrix, concurrent engine
writers, real registration/destruction, general instruction decoder/emulator,
arbitrary translated exception scopes, game performance, gameplay or save tests.
The generated bridge is only 11 bytes with a matching small unwind record.
Do not generalize it to arbitrary code relocation. The frame adapters populate
only the fields listed in the fixture; untested pointer fields need schemas.

An initial chain build linked unused SDK definitions and failed on unresolved
IType methods. Giving the selected definitions ordinary inline linkage avoided
emitting the unrelated vtables; the final build succeeds. This setup failure
was not a runtime backend result. Earlier DBI failure evidence remains in the
separate legacy-compat-lab bundle and has not been replaced by these successes.

Reproduction from repository root
---------------------------------

Requires Python 3.12+, clang-cl/lld-link, the already provisioned Microsoft
SDK/STL tree under build/toolchains/xwin, and the historical SDK object already
available in sources/red4ext-sdk. No downloads or Git mutations are performed.
git archive reads that historical object into generated storage. A Clang VFS
overlay resolves historical include-case differences without changing bytes.

  python3 docs/legacy-compat-readiness-lab/run.py --compile-only
  python3 docs/legacy-compat-readiness-lab/run.py --run-only

Default output is build/compat-readiness/lab. Specify --out for a separate run.
--run-only validates saved C++ source/binary hashes; do not edit a source and
reuse an old result as if it represented the new source. Each executable has a
45-second timeout. The wrapper terminates only its own process group on timeout.
It never invokes a global wineserver shutdown. GUI/display output is disabled.
Wine's IPC and Steam Runtime may require permission outside a restricted sandbox.

For GE, substitute the actual tool locations (placeholders below):
  python3 docs/legacy-compat-readiness-lab/run.py --run-only \
    --wine '<GE-Proton-root>/files/bin/wine' \
    --runtime '<SteamLinuxRuntime_4-root>/_v2-entry-point'

Save build/compat-readiness/lab/results.json after each runtime if both results
are needed; runtime execution replaces its runs section. Prefixes are separate
(wine-prefix and proton-prefix). --only chain_host.exe runs just that executable;
compilation still builds the full bundle. Later fixture checks are not a reason
to rerun unchanged production essentials builds.

  python3 docs/legacy-compat-readiness-lab/lifetime_model.py
  python3 docs/legacy-compat-readiness-lab/derive_layouts.py \
    build/compat-readiness/lab/results.json \
    --out build/compat-readiness/layout-comparison.json

Optional read-only target inspection, with user-supplied paths:
  python3 docs/legacy-compat-readiness-lab/inspect_target.py \
    --exe '<game-root>/bin/x64/Cyberpunk2077.exe' \
    --addresses '<game-root>/bin/x64/cyberpunk2077_addresses.json'

Generated results do not overwrite the preserved research snapshots. To verify
those snapshots from root:
  python3 - <<'PY'
  from pathlib import Path
  import hashlib
  base = Path('docs/legacy-compat-readiness-lab')
  for line in (base / 'SHA256SUMS').read_text().splitlines():
      digest, name = line.split('  ', 1)
      assert hashlib.sha256((base / name).read_bytes()).hexdigest() == digest, name
  print('readiness bundle checksums pass')
  PY

No game/MO2 modification, source patch export, package build, staging, commit or
publication is part of these commands. Local independent changes are preserved.
