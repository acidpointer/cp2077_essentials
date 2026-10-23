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
