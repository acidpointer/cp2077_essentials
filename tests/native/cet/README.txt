CET native regression checks

These checks use patched production code without loading the game. They do
not establish game startup, real D3D12 correctness or save compatibility.

Portable checks (Clang, sanitizer runtime and bootstrapped pinned dependencies):
  CP2077_REQUIRE_CET_TESTS=1 python3 -m unittest discover -s tests -p test_cet_runtime.py -v

The general tooling suite runs before patch application on a fresh CI checkout.
Runtime tests skip there until the saved CET patches supply their production
helpers. The required post-build invocation above rejects missing patches or
dependencies. The TDBID regression keeps its five-second execution deadline;
it must exercise the patched methods, not upstream's unbounded cycle traversal.

Three tests compile/run with ASan and UBSan:
  portable.cpp: actual Lua lock, task queue, metadata and JSON helpers;
    expired owner, lock move ownership, recursive/reentrant drain, concurrent
    close, rejected admissions, retry after error, short KARK header, invalid
    varint/count, field ranges and bindings containing dotted mod names.
  values.cpp: actual per-call allocator/allocator guard/NativeValue with fake
    RTTI and retail allocation; nested arena isolation, exhaustion, TLS
    restoration, retained native snapshot/default out argument and exception
    cleanup. It does not execute the engine's RTTI implementations.
  generated TDBID test: actual registration/lookup methods extracted from
    LuaVM.cpp with minimal map/SDK storage; self/indirect/corrupt cycles,
    root preservation and replacement reverse edges.

LeakSanitizer is disabled because its ptrace support is unavailable in the
restricted runner. AddressSanitizer and UndefinedBehaviorSanitizer remain on.

Windows checks (after the ordinary Release build):
  python3 scripts/test-cet-runtime.py
  python3 scripts/test-cet-runtime.py --runner wine

The first command only builds four Windows executables. The tag workflow
requires this build and the three portable tests; Wine is optional. The
second command executes them in a separate build/cet-runtime/wine-prefix;
it needs wineserver sockets permitted by the local environment. It does not
access a game prefix. --native-build and --output support isolated build trees.

  cet-lua: actual WrapNextOverride extracted from production plus the actual
    FunctionOverride.h invocation owner; real pinned sol2/Windows GC64 LuaJIT
    and lsqlite3/SQLite. Only the engine function call/storage is replaced.
    Multiple/chained callbacks, escaped next, format-token errors, JIT on/off,
    GC, FFI, active statements/backup during DB close, Unicode files/loadfile,
    the actual SandboxIO factory's separate mod streams and unchanged CWD.
    This does not run the whole override descriptor hook or engine proxy ABI.
  cet-lifecycle: actual Runtime.cpp, fake MinHook; callback quiescence,
    stop-from-callback, owned hook transaction and injected hook failures.
    It does not simulate every CET constructor or window/metadata schedule.
  cet-graphics: actual Graphics::Wait with a fake ID3D12Fence; success,
    HRESULT/event/wait failures, timeout/device removal and registered-event
    lifetime. No real GPU, swapchain, font upload or driver reset is tested.
  cet-config: actual Config::Write and Windows filesystem; atomic replacement,
    last-good backup, permanent invalid-file backup and failed-write recovery.

All Windows checks use clang-cl/lld and Microsoft's STL/CRT, matching the DLL.
SOL_EXCEPTIONS_SAFE_PROPAGATION=0 is intentional: the Windows Lua harness
exposed loss of a C++ exception's error message with =1. sol2 must translate
C++ exceptions in its own trampoline before crossing this LuaJIT VM boundary.

Implementation and outstanding game-level acceptance:
  docs/cet-2.5-fix-status.txt
