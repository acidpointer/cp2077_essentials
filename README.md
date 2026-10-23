# Cyberpunk 2077 Essentials · Patch 2.5

**Unofficial, vibe-coded compatibility builds of RED4ext, ArchiveXL, Codeware,
TweakXL and Input Loader for Cyberpunk 2077 2.5.**

This project combines community 2.5 ports, local AI-assisted fixes and a native
Linux build pipeline. “Vibe-coded” describes our compatibility work and tooling,
not the original mods. Credit belongs to their authors and the contributors
whose ports we build on.

The root repository stores **official upstream submodule references, patches
and build tools**. It does not vendor the upstream source trees.

| Target / check | Status |
| --- | --- |
| Game | **2.5**, executable **3.0.88.46235** |
| Release builds | All five build successfully with the shared patched SDK |
| Static checks | ABI assertions, DLL exports, matching symbols, payloads and checksums pass |
| Runtime | The previous four-mod essentials set starts the game; rebuilt packages and Input Loader need a startup test |
| Gameplay / save compatibility | **Not playtested** |

## Included projects

| Project | Official source | Compatibility work |
| --- | --- | --- |
| RED4ext | [wopss/RED4ext](https://github.com/wopss/RED4ext) | [Merged 2.5 support](https://github.com/wopss/RED4ext/pull/154), local Clang fixes |
| ArchiveXL | [psiberx/cp2077-archive-xl](https://github.com/psiberx/cp2077-archive-xl) | [Community 2.5 port](https://github.com/psiberx/cp2077-archive-xl/pull/31) |
| Codeware | [psiberx/cp2077-codeware](https://github.com/psiberx/cp2077-codeware) | [Community 2.5 port](https://github.com/psiberx/cp2077-codeware/pull/63), explicit handle conversion |
| TweakXL | [psiberx/cp2077-tweak-xl](https://github.com/psiberx/cp2077-tweak-xl) | [Community 2.5 port](https://github.com/psiberx/cp2077-tweak-xl/pull/22) |
| Input Loader | [jackhumbert/cyberpunk2077-input-loader](https://github.com/jackhumbert/cyberpunk2077-input-loader) | Shared SDK; audited input/cache fixes; game untested |
| Shared SDK | [wopss/RED4ext.SDK](https://github.com/wopss/RED4ext.SDK) | [Retail layout fixes](https://github.com/wopss/RED4ext.SDK/pull/228), [archive scope fix](https://github.com/wopss/RED4ext.SDK/pull/229), include casing |

The Gitlinks record exact official upstream commits. Compatibility changes are
stored in [patches/](patches/README.md), with provenance in
[sources.lock.json](sources.lock.json). Original credits and
[license notices](licenses/README.md) are retained.

## Install with Mod Organizer 2

Install the five runtime ZIPs listed in `dist/release/manifest.json` or supplied
with a release. Disable the older versions of these five mods first.

- Use `*-mo2-rootbuilder.zip` for RED4ext with Root Builder; keep its top-level
  `Root/`. The alternative ZIP under `standard/` uses game-root paths.
  Install only one RED4ext variant.
- Keep ArchiveXL, Codeware and TweakXL in their supplied
  `red4ext/plugins/<name>/` layout. Their scripts, metadata and assets are
  required alongside the DLLs.
- Keep Input Loader at `red4ext/plugins/input_loader/` and preserve its
  neutral `engine/config/platform/pc/input_loader.ini`. At runtime a complete
  merged XML pair is written below `r6/cache/input_loader/` before activating
  its selector; no vanilla XMLs are shipped. When disabling after a successful
  run, close the game and remove the managed selector as well as the DLL.
  Check Root Builder/MO2's generated output when changing profiles.
- On Proton, preserve the native `winmm` override and launch through MO2.
- Use Release builds and a disposable save for testing. PDB archives are optional.

### Known limitations

ArchiveXL's journal-index extension and resource hot reload remain disabled:
their 2.5 addresses have not been identified. Element-level edits of cooked
instanced-mesh and collision nodes are limited; whole-node edits remain available.
Input Loader has portable merge/publication regressions and an isolated Windows
DLL test harness. Its generation paths, first-launch timing and input behavior
still need a 2.5 game test. It records the baseline snapshot identity rather than
classifying unverified 2.5 XML hashes as modified. The
[compatibility audit](docs/input-loader-2.5-audit.txt) records the defects,
implementation and remaining runtime checks.
Defensive checks observed in Keen's InputLoaderReborn binary are backported
in a separate patch; see the [research evidence](docs/input-loader-research.txt).
The reported 2.5 crash of that binary remains undiagnosed.
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
are initialized selectively, including Input Loader's pinned `deps/pugixml`.
Its private SDK, Detours, spdlog and CMake helper submodules remain unused;
the root target uses our pinned spdlog in header-only mode with wide-character
support. Input Loader retains version 0.2.3 from its latest upstream tag;
its Gitlink pins the subsequent upstream startup and XML merge fixes.

```bash
./build-linux.sh --only codeware --jobs 8
./scripts/build-archivexl.sh --jobs 8
./scripts/build-tweakxl.sh --jobs 8
./scripts/build-red4ext.sh --jobs 8
./scripts/build-inputloader.sh --jobs 8
./build-linux.sh --only input-loader --jobs 8
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

Patches apply in the order recorded by `sources.lock.json`. The initial
`001-essentials.patch` is relative to the pinned upstream commit; each later
layer is relative to all preceding patches. Codeware, TweakXL and ArchiveXL
also have `002-sdk-api-cleanup.patch`, which replaces deprecated SDK aliases
without rewriting the original compatibility work.

Normal `export <project>` updates only the final layer and preserves earlier
patches. To append a separate layer after editing sources:

```bash
python3 scripts/patches.py export codeware --new-patch 003-description.patch
```

Export includes staged and unstaged edits, new files, deletions, renames,
binary changes and executable permissions. It preserves the submodule's real
index and refuses empty layers, duplicate names or corrupted preceding patches.
The three affected plugin targets treat Clang warnings as errors to prevent
warning floods from returning. Codeware's public redscript enum remains named `ERTTIType`.

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

### VS Code and submodule changes

Open this repository's root folder in VS Code. The shared
[editor settings](.vscode/settings.json) disable automatic discovery of
submodule and nested build/backup repositories. Run **Developer: Reload Window**
once if those repositories are already open in Source Control.

Each entry in `.gitmodules` uses `ignore = dirty`: root status suppresses the
expected patched file changes but still reports a changed upstream commit.
`.gitignore` covers generated files; it cannot hide edits to tracked source.
Other `.vscode/` files remain ignored as personal settings.

Inspect source edits directly before exporting a patch:

```bash
git --no-optional-locks -C sources/codeware status --short
git --no-optional-locks status --short --ignore-submodules=none
```

The patch exporter and build validation still inspect actual source contents.
No source files or changes are removed by these display settings.

Before deliberately changing an upstream pin, export edits, use
`python3 scripts/patches.py unapply <project>` to remove only saved changes, then
review the new upstream commit and its Gitlink. Patch conflicts against a new
base require a deliberate port; they are not automatically resolved.

## Packages and release checks

`dist/release/` contains the five runtime ZIPs, individual checksums,
`SHA256SUMS` and `manifest.json`. Alternative RED4ext layout and PDBs are under
`standard/` and `symbols/`.

Archive names contain the original mod version, target game version and any
installation layout. Release filenames have no commit, patch fingerprint,
timestamp or `dirty` suffix:

| Archive | Purpose |
| --- | --- |
| `RED4ext-1.30.0-game2.5-mo2-rootbuilder.zip` | MO2 with Root Builder |
| `RED4ext-1.30.0-game2.5-game-root.zip` | Manual game-root installation; alternative to Root Builder |
| `ArchiveXL-1.27.5-game2.5.zip` | ArchiveXL runtime |
| `Codeware-1.20.6-game2.5.zip` | Codeware runtime |
| `TweakXL-1.11.5-game2.5.zip` | TweakXL runtime |
| `InputLoader-0.2.3-game2.5.zip` | Input Loader runtime |
| `<mod>-<version>-game2.5-symbols.zip` | Optional debugging symbols |

Debug builds additionally include `-debug` to distinguish them from installable
Release packages. Exact upstream revisions, patch checksums and shared SDK
identity remain in the manifest. A distribution is verified before it replaces
the previous one; packages with incompatible provenance cannot be mixed.
`SHA256SUMS` covers all eleven archives, including standard layout and symbols.
After updating older tooling, run the full build once to refresh package names
and metadata; old cached package records are rejected.

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

## GitHub releases

[The release workflow](.github/workflows/release.yml) runs when a tag matching
`vX.Y.Z` is pushed. Tags `vX.Y.Z-rc` and `vX.Y.Z-rc.N` publish prereleases.
The essentials tag identifies the complete set; archive filenames retain each
mod's original version.

On an Ubuntu-hosted runner, an Arch Linux container installs LLVM, CMake and
the other Linux dependencies, then checks out the root and its six official
submodules at the recorded commits. It runs the source audit and regression
tests, applies our saved patches and builds all five Release packages. Nested
dependencies are initialized selectively by the existing bootstrap; no copied
upstream trees or prebuilt plugin DLLs are committed or used as build inputs.
The unchanged, checksum-pinned ArchiveXL resource asset is the documented
exception for packaged resources.

Only after verification succeeds does the workflow publish a GitHub release
with eleven ZIPs, their individual checksums, `SHA256SUMS` and `manifest.json`.
GitHub assets are flat: the release manifest uses matching flat filenames,
while local `dist/release/` retains its `standard/` and `symbols/` directories.
The automatically generated release notes include installation choices,
original mod versions, attribution and the current runtime limitations.

The workflow uses the repository's `GITHUB_TOKEN` with `contents: write`;
no personal access token is needed. Publication creates a missing release or
attaches the prepared assets to an existing mutable release, replacing only
matching asset filenames. Existing titles, notes and prerelease settings are
preserved. Drafts are published only after successful uploads. A rerun whose
assets already match GitHub's SHA256 digests succeeds without uploading again.
Immutable releases with different assets require a new version tag.
The tag must include the committed workflow and tooling. Creating commits,
tags and pushes remains a maintainer action unless explicitly requested.

```bash
# Read-only check of tracked source boundaries and official submodule pins.
python3 scripts/check-sources.py

# Prepare GitHub assets locally after a complete Release build; no upload.
python3 scripts/release.py v1.0.0
```

Release preparation verifies source identities through the existing patch
helper, which writes temporary Git indexes/objects. Agents must obtain explicit
authorization for those Git mutations under [AGENTS.md](AGENTS.md).
On failure, build logs and any prepared ZIPs, checksums and manifest are
retained as separate workflow artifacts for seven days. A publication failure
does not require losing the completed build outputs.
CI-built binaries still require in-game testing; automation does not establish
gameplay or save compatibility.

See [AGENTS.md](AGENTS.md) for the file map and maintenance rules, and
[the investigation](docs/PATCH_2_5.md) for the original 2.5 crash evidence.
