# Cyberpunk 2077 Essentials · Patch 2.5

**Unofficial, vibe-coded compatibility builds of RED4ext, ArchiveXL, Codeware
and TweakXL for Cyberpunk 2077 2.5.**

This project combines community 2.5 ports, local AI-assisted fixes and a native
Linux build pipeline. “Vibe-coded” describes our compatibility work and tooling,
not the original mods. Credit belongs to their authors and the contributors
whose ports we build on.

The root repository stores **official upstream submodule references, patches
and build tools**. It does not vendor the upstream source trees.

| Target / check | Status |
| --- | --- |
| Game | **2.5**, executable **3.0.88.46235** |
| Release builds | All four build successfully with the shared patched SDK |
| Static checks | ABI assertions, DLL exports, matching symbols, payloads and checksums pass |
| Runtime | The previous essentials set starts the game; packages rebuilt after this source-management migration need another startup test |
| Gameplay / save compatibility | **Not playtested** |

## Included projects

| Project | Official source | Compatibility work |
| --- | --- | --- |
| RED4ext | [wopss/RED4ext](https://github.com/wopss/RED4ext) | [Merged 2.5 support](https://github.com/wopss/RED4ext/pull/154), local Clang fixes |
| ArchiveXL | [psiberx/cp2077-archive-xl](https://github.com/psiberx/cp2077-archive-xl) | [Community 2.5 port](https://github.com/psiberx/cp2077-archive-xl/pull/31) |
| Codeware | [psiberx/cp2077-codeware](https://github.com/psiberx/cp2077-codeware) | [Community 2.5 port](https://github.com/psiberx/cp2077-codeware/pull/63), explicit handle conversion |
| TweakXL | [psiberx/cp2077-tweak-xl](https://github.com/psiberx/cp2077-tweak-xl) | [Community 2.5 port](https://github.com/psiberx/cp2077-tweak-xl/pull/22) |
| Shared SDK | [wopss/RED4ext.SDK](https://github.com/wopss/RED4ext.SDK) | [Retail layout fixes](https://github.com/wopss/RED4ext.SDK/pull/228), [archive scope fix](https://github.com/wopss/RED4ext.SDK/pull/229), include casing |

The Gitlinks record exact official upstream commits. Compatibility changes are
stored in [patches/](patches/README.md), with provenance in
[sources.lock.json](sources.lock.json). Original credits and
[license notices](licenses/README.md) are retained.

## Install with Mod Organizer 2

Install the four runtime ZIPs listed in `dist/release/manifest.json` or supplied
with a release. Disable the older versions of these four mods first.

- Use `*-mo2-rootbuilder.zip` for RED4ext with Root Builder; keep its top-level
  `Root/`. The alternative ZIP under `standard/` uses game-root paths.
  Install only one RED4ext variant.
- Keep ArchiveXL, Codeware and TweakXL in their supplied
  `red4ext/plugins/<name>/` layout. Their scripts, metadata and assets are
  required alongside the DLLs.
- On Proton, preserve the native `winmm` override and launch through MO2.
- Use Release builds and a disposable save for testing. PDB archives are optional.

### Known limitations

ArchiveXL's journal-index extension and resource hot reload remain disabled:
their 2.5 addresses have not been identified. Element-level edits of cooked
instanced-mesh and collision nodes are limited; whole-node edits remain available.
Other native plugins outside this set may require their own 2.5 updates.

## Build on Linux

Use a **Git clone**, not GitHub's source ZIP: Git metadata supplies the submodule
revisions. An x86_64 Linux host is supported out of the box.

Install CMake 3.26+, a recent Clang/LLVM toolchain, Git, Python 3.9+, curl, tar,
coreutils, and Ninja or GNU Make. LLVM must supply `clang-cl`, `lld-link`,
`llvm-lib`, `llvm-mt`, `llvm-rc`, `llvm-ml`, `llvm-readobj` and
`llvm-pdbutil`. The tested toolchain is LLVM 23.1.3 with CMake 4.4.4.

```bash
# Arch Linux dependencies
sudo pacman -S --needed cmake clang lld llvm git python curl tar coreutils ninja

# From this repository
git submodule update --init
./build-linux.sh --jobs 8
```

The build initializes missing submodules, applies saved patches, prepares pinned
dependencies and builds Windows x64 DLLs with the Microsoft C++ ABI, STL and CRT.
Wine and a Windows build installation are not required. Microsoft SDK bootstrap
uses checksum-pinned [xwin](https://github.com/Jake-Shadle/xwin) with
`--accept-license`.

All projects use `sources/red4ext-sdk`. The unused SDK submodules inside the
plugins are not initialized by our scripts. Other required nested dependencies
are initialized selectively.

```bash
./build-linux.sh --only codeware --jobs 8
./scripts/build-archivexl.sh --jobs 8
./scripts/build-tweakxl.sh --jobs 8
./scripts/build-red4ext.sh --jobs 8
./build-linux.sh --config Debug --jobs 8
```

The complete Release set is validated; a complete Debug set is not a recorded
validation. Logs, toolchains, native build trees and per-project packages stay
under `build/`. Builds do not modify the game or MO2 installation.

## Edit → export patch → build → commit

```bash
python3 scripts/patches.py apply all

# Edit files in sources/codeware/...

python3 scripts/patches.py export codeware
./build-linux.sh --only codeware --jobs 8
git add patches/codeware sources.lock.json
git commit
```

Each project initially has one cumulative `001-essentials.patch`, always
exported relative to its pinned upstream commit. Export includes staged and
unstaged edits, new files, deletions, renames, binary changes and executable
permissions. It preserves the submodule's real index.

```bash
python3 scripts/patches.py status
python3 scripts/patches.py check all
```

`apply` recognizes saved patches and preserves later local edits. Release
packaging requires those edits to be exported. `check` reconstructs sources in
temporary independent clones. Conflicts or a changed submodule HEAD stop the
operation without a forced checkout.

Modified submodule content is **expected** after applying patches. Commit in
the root repository; do not commit patched source inside a submodule or stage
an upstream revision update by accident. The initial root commit includes
`.gitmodules`, Gitlinks and tooling. Later compatibility commits normally
contain patches and metadata.

Before deliberately changing an upstream pin, export edits, use
`python3 scripts/patches.py unapply <project>` to remove only saved changes, then
review the new upstream commit and its Gitlink. Patch conflicts against a new
base require a deliberate port; they are not automatically resolved.

## Packages and release checks

`dist/release/` contains the four runtime ZIPs, individual checksums,
`SHA256SUMS` and `manifest.json`. Alternative RED4ext layout and PDBs are under
`standard/` and `symbols/`.

Package names and manifests identify both the upstream source and the saved
patch set, including the shared SDK. A distribution is verified before it
replaces the previous one; packages with incompatible provenance cannot be mixed.

```bash
python3 scripts/verify-build.py
(cd dist/release && sha256sum --check SHA256SUMS)
python3 scripts/verify-build.py --addresses '/path/to/Cyberpunk 2077/bin/x64/cyberpunk2077_addresses.json'
python3 -m unittest discover -s tests -v
```

ArchiveXL's unchanged packed resource comes from checksum-pinned official
ArchiveXL 1.27.5. Every DLL is compiled locally. Upload only the runtime ZIPs,
checksums and manifest for a release; standard-layout and symbol archives are
optional extras. Preserve the startup-only test status and known limitations.

See [AGENTS.md](AGENTS.md) for the file map and maintenance rules, and
[the investigation](docs/PATCH_2_5.md) for the original 2.5 crash evidence.
