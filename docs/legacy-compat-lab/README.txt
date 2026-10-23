Legacy compatibility laboratory evidence
========================================

Historical observations: 2026-10-24. This is a research artifact, not a
production compatibility layer or a production test suite. Read the evidence
ledger and implementation plan before interpreting a passing experiment:
  docs/legacy-plugin-compat-evidence.txt
  docs/legacy-plugin-compat-plan.txt

Contents and record handling
---------------------------

The six C++ fixture sources are unchanged copies of the final local laboratory
sources. Python drivers were made portable before preservation. The DynamoRIO
download fallback now selects the recorded tag rather than the latest release.
The two release JSON files retain only release identity and the selected asset.
They contain publisher metadata, not independently verified archive contents.

results/ preserves the observed result JSON, selected-file SHA256 inventory
and the DynamoRIO diagnostic log. Duplicate per-mode logs are omitted where
their complete output is already in result JSON. Absolute checkout and lab
paths are replaced by <repo>, <lab> and <lab-win>; diagnostic line endings are
normalized to LF. No diagnostic lines or result values are otherwise removed.
SHA256SUMS covers the preserved files after those substitutions, including
this README. It does not authenticate the original experiment environment.

Important distinctions:
  - backend-incomplete-extraction-results.json records a setup failure before
    the missing loader files were restored. backend-results.json records the
    subsequent failures; backend-diagnostic.log records the debug investigation.
  - compile-initial-failure.json predates the final callback source, which uses
    handler-level copying. It does not describe the final fixture's compilation.
  - compile-results.json contains only the last compilation, veh_probe.exe;
    the driver overwrites this record on each invocation.
  - veh-results.json contains final /EHa results. Earlier /EHsc hardware-SEH
    attempts are described in the evidence ledger, without a retained raw log.
  - address-summary.json contains aggregate counts, not the game's address data.

No downloaded third-party code, binary, Wine prefix, game data or release
payload is included. Current source/dependency revisions can differ from the
historical experiment; record sources.lock.json and tool versions for new runs.
The historical SDK base is 0f5eba4 with the local retail compatibility patch.
The evidence ledger records compiler and runtime versions and tested limits.

Optional reconstruction
-----------------------

Run from the repository root. Copy this bundle to generated storage first;
the drivers write downloads, binaries, logs, results and prefixes beside
themselves. Use a fresh destination to keep historical results separate.

  mkdir -p build/compat-evidence
  cp -R docs/legacy-compat-lab/. build/compat-evidence/
  python3 build/compat-evidence/download_dynamorio.py
  python3 build/compat-evidence/fetch_frida.py
  python3 build/compat-evidence/compile_fixtures.py .

Prerequisites: Python 3.12 or newer for tar extraction filtering, clang-cl and
lld, the existing Microsoft x64 SDK/CRT at build/toolchains/xwin, the checked-out
shared SDK, and system Wine. These scripts do not initialize submodules or
change Git metadata. Download commands access GitHub. Frida's complete archive
digest is checked against release metadata. DynamoRIO uses selected ZIP ranges
and member CRCs on extraction; the full archive SHA256 is not verified. Use a
fresh extraction: previously cached files are checked only for size.

System Wine, isolated prefix in the copied directory:
  python3 build/compat-evidence/run_wine_fixtures.py
  python3 build/compat-evidence/run_backend.py
  python3 build/compat-evidence/run_backend_diag.py
  python3 build/compat-evidence/run_gum.py
  python3 build/compat-evidence/run_veh.py

GE's Wine binary within Steam Runtime 4, with explicit local paths:
  python3 build/compat-evidence/run_ge.py --wine PATH_TO_GE_WINE --runtime PATH_TO_RUNTIME_ENTRY
  python3 build/compat-evidence/run_veh.py --wine PATH_TO_GE_WINE --runtime PATH_TO_RUNTIME_ENTRY

The runtime entry is SteamLinuxRuntime_4/_v2-entry-point. These commands do not
run the Proton launcher, MO2 or the game. Some negative tests intentionally
time out. Stalker and process-launch failures do not establish their internal
root cause or incompatibility on every platform. Protected-view passes cover
only selected synthetic operations, not arbitrary plugins or object lifetimes.
The raw-pointer bypass demonstrates an undetected incorrect field access.

Re-runs create fresh top-level result JSON; results/ remains the historical
snapshot. Verify the original bundle from within docs/legacy-compat-lab with:
  sha256sum --check SHA256SUMS
