# Cyberpunk 2077 patch 2.5 investigation

Investigated on 2026-10-23 against game file version **3.0.88.46235** and RED4ext
commit **47ac01cd1d04a33f1748d1ebf8d8bf64814ee6c6**.

## Fixes already in this checkout

[RED4ext PR #154](https://github.com/wopss/RED4ext/pull/154) is already merged at
HEAD. The official [build for that commit](https://github.com/wopss/RED4ext/actions/runs/39823025406)
passed. This is newer code than the v1.30.0 release tag even though its base
version remains 1.30.0; Git metadata distinguishes the development build.

The merged changes:

* Require game 2.5 and pin RED4ext.SDK to
  `0f5eba42a4cf3a1161ad3de0b4ed6f1901ec169e`.
* Use the SDK's updated array API and engine layouts.
* Keep the engine's original script blob path for the official mod menu's
  filename matching, and redirect loads to redscript's compiled blob with
  `ScriptBundle_LoadFromFile`. Otherwise an in-game mod reload can discard
  community scripts.

[SDK PR #227](https://github.com/wopss/RED4ext.SDK/pull/227) explains the ABI
changes: script functions grew by 0x40 bytes, stack frames grew to 0xA8, engine
members and RTTI vtable slots moved, and the default fixed callback buffer grew
from 32 to 64 bytes. Old code can therefore call the wrong virtual function,
write to a different member, or pass a callback with the wrong layout.

## Local failure evidence

The existing MO2 overwrite logs from 18:33:47 show
`v1.30.0+master.1.47ac01c.20261022T172857Z`, product version 2.50, file version
3.0.88.46235, 1,032,136 loaded addresses, and successful RED4ext initialization.
The last RED4ext entry is loading `ArchiveXL.dll`. ArchiveXL's corresponding
18:33:49 log contains only `ArchiveXL 1.27.5 is starting...`.

These logs establish that the 2.5 RED4ext patch was already deployed and that
execution reached ArchiveXL initialization. They do not establish a precise
crash instruction without a dump. Replacing the same RED4ext revision alone
is unlikely to fix this plugin startup failure.

All nine hashes declared in `src/dll/Detail/AddressHashes.hpp` were checked
against the installed 2.5 `cyberpunk2077_addresses.json`: each has exactly one
entry, including the new script blob loader hash `3339589204`. Address presence
is a static check; it does not prove hook signatures or behavior at runtime.

## Native plugins require their own ports

The [ArchiveXL 2.5 port PR #31](https://github.com/psiberx/cp2077-archive-xl/pull/31)
reports the old fixed callback layout as an early crash cause, and updates
ArchiveXL's own signatures, hashes, resource and world layouts. It depends on
additional SDK layout changes from
[SDK PR #228](https://github.com/wopss/RED4ext.SDK/pull/228), beyond RED4ext's
original SDK pin. It also links the corresponding
[Codeware port](https://github.com/psiberx/cp2077-codeware/pull/63) and
[TweakXL port](https://github.com/psiberx/cp2077-tweak-xl/pull/22).

The later essentials workspace incorporates these ports and upgrades the shared
SDK to `12e0c33337f8612850854fb9ed5ad9628796afca` (see `../README.md`).

Those were open ports at investigation time, with limitations documented by
their authors. RED4ext statically links the SDK; replacing its DLL cannot change
the SDK layouts already compiled into ArchiveXL, Codeware or TweakXL. Each
plugin must be ported and rebuilt separately. The loader also allows plugins to
declare a runtime-independent version, so passing its version check does not
guarantee binary compatibility after an engine ABI change.

The useful installation test is to start with the new RED4ext package and no
native plugins, confirm the initialization/startup logs, then add 2.5-compatible
plugins one at a time. A full in-game test is still required for the Linux-built
artifacts; successful compilation and static checks alone are insufficient.

## Confirmed crash after the first standalone build

The 19:09 Proton log (`~/steam-11815380638201020416.log`, game process 03a0)
loads the Linux-built RED4ext, then ArchiveXL 1.27.5. The fatal execute fault at
`0x1e80090140` has this call chain:

```text
RED4ext!PluginSystem::Load -> PluginBase::Main
ArchiveXL.dll+0x134ed6 -> +0xb419 -> +0xdb25
Cyberpunk2077.exe+0x2017c81 -> +0x201802c -> invalid callback
```

The game instruction at RVA `0x2018029` calls the copy handler through
`[rax+8]` after loading the callback handler from offset 0x40. The installed old
ArchiveXL constructs its callback handler at offset 0x20 (`ArchiveXL.dll`
RVA 0xd9a8). This establishes the old/new callback layout mismatch; the later
shared-SDK build has an explicit static assertion for handler offset 0x40.

## Initial standalone Linux build results

Both Release and Debug were built and installed successfully on Arch Linux with
Clang/LLVM 23.1.3, CMake 4.4.4, GNU Make, xwin 0.10.0, Microsoft CRT package
14.44.17.14 and Windows SDK 10.0.26100. Both builds used CI's extra warnings and
warnings-as-errors options. Debug was built from a fresh CMake directory.

The Linux build blockers fixed here are the SDK's `rtti`/`RTTI` include casing,
an invalid narrow default literal for `std::wstring_view`, ignored `const`
qualifiers on scalar return types, unused-parameter expressions, and constructor
initializer order. These are build portability fixes. The upstream 2.5 engine
hooks are preserved. The later essentials workspace upgrades the SDK pin for all
four projects to include the additional plugin layout fixes.

Artifact checks verified Windows AMD64 DLL headers, Microsoft runtime imports,
all 181 winmm proxy exports against `src/loader/Main.def`, the
`RED4ext_ResolveAddress` export, matching Release DLL/PDB identifiers, and the
runtime/symbol ZIP contents, CRCs and SHA256 checksums. The runtime archives
exclude the playground plugin and PDBs. No game or MO2 installation files were
changed by the build scripts. The initial standalone build was subsequently
tested by the user and reached the ArchiveXL callback mismatch described above.
The maintainer subsequently confirmed that the newer complete essentials set
starts the game. Gameplay, save/load behavior and save compatibility remain
untested.

## Official-submodule migration

The root now records official upstream Gitlinks. The shared SDK uses official
base 0f5eba4 plus a cumulative patch representing the later 12e0c3333 layouts
and scope correction, with include-case fixes. Mod source content was compared
with the startup-tested trees before the old checkouts were retired. Rebuilt
packages carry upstream commits and patch fingerprints; another startup test
is required after the migration. Gameplay remains untested.


## Exit-crash candidate: SDK array relocation

The retained 2026-10-24 01:09 session faults on `redDispatcher13` at
`Cyberpunk2077.exe+0xB0C78A`, reading address `0xD0` through a null vtable.
The installed executable identifies this thunk as the last-reference handle
`CanBeDestructed` check. Its surrounding destructor follows the same reference
counter layout and virtual slot as the SDK. The retained minidump has neither
an exception stream nor usable thread-stack payloads, so it cannot identify
the object or component that invalidated it. This remains an unresolved
runtime failure, not a proven RED4ext shutdown-hook mismatch.

The static audit found a separate, concrete lifetime defect in shared SDK
`DynArray::MoveEntries`: it used move assignment for fresh allocation storage.
Retail `ResizeBuffer` zero-fills that storage before calling the relocation
callback, but zero-filling does not construct nontrivial C++ objects. The fix
uses `std::uninitialized_move`, then destroys the moved-from source elements.
It preserves container layout, allocator selection, element count, relocation
ABI and all enabled functionality. All SDK consumers require rebuilding.

`tests/native/sdk_dynarray_lifetime.cpp` exercises the real SDK against a
zero-filling allocator boundary modeled on the inspected retail function.
It rejects assignment into objects whose lifetimes have not begun, checks
resource/value preservation and exact destruction, and covers growth with
spare capacity, repeated growth, shrinking and empty relocation. The optimized
Windows test fails against the previous header with exit code 42 and passes
after the fix in an isolated Wine prefix. It does not load any game/mod DLLs.
This establishes the SDK defect and correction; it does not establish that
this defect caused the reported game exit crash or that the game now exits.
No diagnostic game build, exception interception, force-exit path, null-vtable
skip or feature disablement is part of this change.

### Save/new-game loading investigation (2026-10-24)

Static inspection of the installed game and ArchiveXL found an attachment-slot
self-deadlock: a suffix override called the locking `FindSlotData` while its
thread already held the same slot lock exclusively. Three retail writer paths
can reach this override. The shared SDK also placed listener registration and
visual-tag methods at incorrect vtable slots; ArchiveXL's detach path kept a
reference to a map value after erasing it. Full evidence and addresses are in
[the investigation](load-freeze-investigation.txt) and
[the change plan](load-freeze-change-plan.txt).

The new SDK layer corrects those methods and exposes the retail non-locking
lookup at `0x450`. ArchiveXL tracks the owner in thread-local scopes around all
three verified writers and uses that lookup only for a matching owned lock.
Ordinary and other-owner queries retain the locking API. Item handles are copied
inside the search predicate before the engine releases its lock. Extra-slot
maps are snapshotted before engine calls. Listener registration/removal is
serialized independently of state readers, and an owning handle survives erase
until native removal finishes. Hook rollback retains protection if a consumer
hook cannot be detached.

The callback audit found the completion notifications at retail RVAs `0x2E4B45`
and `0x2E4D2D`, with virtual slots `0x120` and `0x148`. The notifier copies its
listener list and releases lock `+0x101` before invoking them. Its update-stage
caller takes no attachment lock; normal callback queries therefore keep the
locking API. The owner scopes still cover reentrant queries within the three
verified writer paths. This is targeted static coverage, not a proof about every
possible indirect engine caller.

The five optional `tests/test_load_freeze.py` tests passed in an isolated Wine
prefix using clang-cl/Microsoft-ABI fixtures. They cover actual SDK dispatch,
by-value handle ownership, the production slot-query/scope code, three hook
argument shapes, nested owners, thread isolation, unwinding, protected snapshots,
hook failures, and the production listener registry with `Core::Map`. All four
incorrect method calls fail with exit 42 against the previous SDK header; a
separate old-path control rejects same-owner lock reacquisition. The fixtures
load no game/mod DLLs. They do not validate clothing appearance in-game.

The working tree, including the separate concurrent CET integration, built all
six runtime projects in Release. Independent patch reconstruction and the
repository suite passed (63 tests, 15 optional skips;
the five native tests ran separately). Packages in
`build/validation/load-freeze-dist/` passed source/SDK and payload verification,
Microsoft ABI/import checks, exports/PDB matching, and checksums for all fourteen
archives. Address checks passed: ArchiveXL 132/134 with only its two documented
disabled-feature hashes absent, Codeware 107/107, TweakXL 11/11 and CET 32/32.

The source-level defects and regressions are established. No thread stack was
captured from the reported frozen session, and this change has not been tested
in the game. Save loading, new game, first save/reload, clothing and shutdown
remain runtime validation tasks. The separate CET 1.38.0 first-autosave job-wait
report remains an isolation lead, not a demonstrated consequence of these fixes.
