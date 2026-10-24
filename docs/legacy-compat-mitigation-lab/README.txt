Legacy shim mitigation experiments
=================================

Observed: 2026-10-24. Companion: docs/legacy-plugin-compat-mitigations.txt.
This is an isolated experiment, not a production shim or integration test.
The earlier docs/legacy-compat-lab/ historical bundle is unchanged.

Inputs and boundaries
---------------------

guest.cpp compiles against unchanged historical SDK headers at
f9ad5334362a8a0815baf58f47615697485feb52. It executes the actual inline
CClassStaticFunction::Create, allocator wrapper and DynArray::PushBack.
Vault/name-pool/resolver services are fakes. host.cpp checks current SDK sizes
and offsets but models native functions using byte storage; it does not call
the real engine constructor or invoke registered script functions.

The protected view handles only the exact compiled MOV forms checked before
installing its handler. Export addresses identify laboratory instructions,
not production mod recognition rules. Parent views are two identity tokens;
their own objects, inheritance, lifetime and concurrent mutation are untested.
No full decoder, module-initialization interception, general object registry,
atomic emulator, unwind translator or compatibility profile was implemented.

The matched two-fault read mode is only a benchmark comparator. It does not
validate debugger coexistence or interrupted-remap recovery. No C++/SEH controls
were repeated with this new host; earlier backend passes do not transfer to it.

Results
-------

results.json preserves the final compilation and system-Wine run. Both compile
steps and the executable returned zero. The historical headers emitted their
existing C++23-extension and mixed-case-include warnings. Clang's case-insensitive
VFS models Windows path lookup without changing header bytes or adding aliases.

RETURN_ONLY: old allocation kept; replacement constructor return ignored.
PROMOTION: original address protected; shifted fields and pointer values bridged.
UNTYPED: another same-size allocation stays writable and is not inferred typed.
BYPASS: accessible raw native pointer still produces an unobserved stale read.
ARRAY_ESCAPE: actual old PushBack publishes a guest pointer into a writable
  native-format pointer array without faulting. This is a negative control.
READ_BENCH: five trials, 20,000 reads per mode; medians 0.040 / 55.407 / 110.843 ms
  for native / one-fault / two-fault. Fixed order, no thread-start cost, no FPS
  implication. PASS checks 200,007 access violations and 100,000 single steps.

Environment: clang-cl 23.1.3, lld, Microsoft headers/libraries from the existing
xwin toolchain, /MT /O2 /EHsc, system Wine 11.20 (Staging), isolated prefix.
An initial sandbox run failed because wineserver could not bind its local
socket. The recorded successful run used authorized execution outside that
sandbox. No GE/Proton, native Windows, MO2, game, graphics integration or save
test was performed. Wine prefix setup output is not evidence of graphics testing.

Reproduction from repository root
---------------------------------

Prerequisites: Python 3.12+, clang-cl, lld, Wine, the existing
build/toolchains/xwin installation, shared SDK checkout, and the historical
Git object above. No dependency downloads or source initialization occur.

  python3 docs/legacy-compat-mitigation-lab/run.py --compile-only
  python3 docs/legacy-compat-mitigation-lab/run.py

The driver rebuilds and writes only to build/compat-mitigation by default.
Use --out build/compat-mitigation-another-run to preserve previous generated
results. It runs git archive to read historical headers, with no checkout,
temporary Git index or object writes. If the old object is absent it fails;
it never fetches it automatically. Runtime execution has a 45-second timeout.
Wine may require permission to create its local socket outside a sandbox.

results.json is a preserved snapshot; subsequent runs write their results in
the generated output directory, not here. Paths in the record use <repo> and
<out>. SHA256SUMS covers this bundle after path substitutions and does not
authenticate the environment. Verify from this directory with:

  sha256sum --check SHA256SUMS

DLL hashes identify experimental inputs, never a mod compatibility allowlist.
Compiled DLLs, historical header copies and Wine prefixes are not tracked or
included in any runtime package.
