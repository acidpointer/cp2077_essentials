CET 2.5 audit reproductions
Recorded: 2026-10-24

These tools inspect local sources/game data read-only and run isolated native
defect probes. They do not load CET into the game, alter game/MO2 files, call
patch helpers, change Git metadata or establish gameplay/save compatibility.
Generated inputs, executables, a disposable Wine prefix and logs are placed
under build/cet-audit/. Review probes before running them: several deliberately
exercise invalid memory access under sanitizers, abort, or time out.

Reports:
  docs/cet-2.5-audit.txt
  docs/cet-2.5-fix-plan.txt
  docs/cet-2.5-fix-status.txt

Current positive regressions use actual CET helpers and the production
continuation method. See tests/native/cet/README.txt. These historical negative
probes must use a separate pre-fix src tree containing 001-essentials.patch
only. The runner checks all recorded source fingerprints before extracting
methods; it rejects the fixed source rather than claiming a broken model
verifies the fix. Never reset the active submodule to run a baseline probe.

CPU defect probes (requires clang++ and existing build/deps/cet-json/include):
  python3 docs/cet-audit-lab/run_probes.py --baseline-src /path/to/baseline/src

Optional actual Microsoft WRL probe (also requires configured Microsoft SDK at
build/toolchains/xwin, root clang-cl toolchain, CMake, Make and Wine):
  python3 docs/cet-audit-lab/run_probes.py --baseline-src /path/to/baseline/src --wine

Wine uses its own build/cet-audit/probes/wine-prefix. It needs local wineserver
sockets; a restricted sandbox may block it. No game/GPU object is instantiated
by the probe. Expected WRL evidence for the OLD ownership pattern:
  refs=-1 releases=2
The object intentionally remains allocated so both Release calls are counted.
This is a reference-count demonstration, not a real-driver use-after-free test.

Expected baseline results (all reproduced in the recorded audit):
  override_capture: ASan stack-use-after-return; capture-semantics model of
    WrapNextOverride pointer parameters, not the complete sol/CET call path.
  queue_reentrant: debug std::vector singular iterator; extracted production
    RepeatedTaskQueue methods with storage matching TiltedCore's vector alias.
  short_kark: ASan heap-buffer-overflow; the metadata header expression with
    a three-byte vector, before Oodle; not the complete decompression routine.
  tdbid_cycle: timeout; extracted production registration/lookup methods with
    minimal replacement storage/types and a self-parent record.
  binding_dot: nlohmann type_error; actual pinned JSON and the schema dispatch
    branch copied from Load; not the full VKBindings owner or game startup.
  sandbox_reset: three sandboxes retained/zero databases closed; extracted
    ResetState with fake sandbox/DB storage, no Lua/SQLite engine.
  comptr_release: two Releases for one attached reference; actual WRL headers
    and a fake IUnknown, compiled using the root Microsoft toolchain.

The probe program succeeds when these old defects REPRODUCE. It is preserved
as baseline evidence; tests/test_cet_runtime.py and scripts/test-cet-runtime.py
provide separate positive checks against current production code.
Scope and generated-source fingerprints are in probes/results.json. Results
without --wine contain six probes, with --wine seven; do not confuse those runs.

Read-only hash/PE/SDK inspection:
  python3 docs/cet-audit-lab/inspect_retail.py \
    --exe '/path/to/Cyberpunk 2077/bin/x64/Cyberpunk2077.exe' \
    --addresses '/path/to/Cyberpunk 2077/bin/x64/cyberpunk2077_addresses.json' \
    --sdk-reference build/cet-audit/upstream-sdk/include/RED4ext

The optional SDK reference must be the official CET SDK pin
fd1867297719e26c6fbd54734c7ce82448d32c6f from:
https://github.com/maximegmd/RED4ext.SDK/tree/fd1867297719e26c6fbd54734c7ce82448d32c6f
Inspect CET's .gitmodules when reconstructing this evidence. The audit obtained
an official source archive and extracted only include/RED4ext under build/;
it did not initialize/change any submodule. Do not silently compare a new HEAD.
Archive URL:
https://codeload.github.com/maximegmd/RED4ext.SDK/tar.gz/fd1867297719e26c6fbd54734c7ce82448d32c6f

Output is build/cet-audit/retail.json. It reports source-used flags, unique-match
counts, PE identity, calculated RVAs, included-header closure, missing headers
and text differences. A match is not proof of a function signature/layout.
The report describes additional read-only instruction inspection; this script
does not disassemble or prove those observations automatically. Instruction
RVAs/interpretations are recorded in evidence.json without distributing game
bytes. The executable and address database remain outside tracked files.

evidence.json preserves compact measured results, audited source fingerprints,
SDK/reference identities and probe scope. Re-running regenerates build outputs,
not that reviewed record. Source changes can invalidate specific findings;
compare fingerprints and investigate changed methods before reusing conclusions.

Additional shared-SDK hash references in CET's included-header closure:
  python3 docs/cet-audit-lab/inspect_sdk_hashes.py \
    --addresses '/path/to/Cyberpunk 2077/bin/x64/cyberpunk2077_addresses.json'

This scan includes unused inline methods and is not a complete dynamic call
graph. Unique matches establish address availability, not calling conventions.
