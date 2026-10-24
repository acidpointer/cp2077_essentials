# Agent instructions

## Objective and evidence

Maintain the unofficial, vibe-coded essentials set for **CP2077 2.5 /
3.0.88.46235**. The previous set starts the game. The migrated source layout has
passed Release builds and static checks; its rebuilt packages need another
startup test. Gameplay, save/load behavior and save compatibility are untested.
Never report static checks as playtesting.

Do not publish or modify game/MO2 files unless the user requests that action.
Use relative paths; avoid host-specific home/workspace paths in tracked files.

## Git permissions and commit rules

Change Git state only on an explicit, unambiguous user request for that
action. A request to edit, fix, test or build does not authorize staging,
committing or other Git mutations. Requested file edits may remain unstaged.

This restriction covers the index/staging area, commits/history, branches,
tags, stashes, remotes/configuration, Git object databases and submodule
registration/revisions. Do not run mutating commands such as `git add`,
`commit`, `reset`, `restore`, `checkout`, `switch`, `stash`, `merge`,
`rebase`, `fetch`, `pull`, `push` or submodule initialization/update without
authorization for that action. A commit request permits staging the intended
changes for that commit; it does not authorize a push, tag or unrelated work.

Read-only inspection is allowed. Use `git --no-optional-locks` for status
and diff commands to avoid incidental index writes. Patch/build helpers can
write temporary indexes, Git objects or initialize submodules; inspect their
effects and require explicit authorization before such Git mutations.

When explicitly asked to commit, read and strictly follow
[docs/commit-rules.md](docs/commit-rules.md): use the prescribed type, an
optional scope, an imperative lowercase subject of at most 50 characters
without a final period, and the documented body/footer rules when used.
Review the intended diff and commit only in the root repository. Never
commit inside an upstream submodule without a separate explicit request.

## Read only what the task needs

1. Inspect root `git --no-optional-locks status --short`, `sources.lock.json`
   and the relevant file.
2. If editing native source, inspect that submodule's status too.
3. Use the map below; do not dump the SDK tree or full Proton/build logs.
4. Make the smallest change and run the matching validation.

When asked to research a mod or its source, first check the optional
`.research/` directory for matching source trees or mod builds. Use relevant
local material as research evidence. The directory may be absent; if it is
absent or has no matching material, continue with other available sources.

| Task | File / directory |
| --- | --- |
| Mod research sources and builds | `.research/` (optional) |
| Official upstream URLs and commits | `.gitmodules`, root Gitlinks at `sources/*` |
| Patches, provenance, dependency/asset pins | `sources.lock.json`, `patches/<project>/` |
| Apply/export/status/reconstruct patches | `scripts/patches.py` |
| Source paths, Git commands, atomic metadata | `scripts/common.py` |
| Initialize missing sources/dependencies | `scripts/bootstrap.py` |
| Root orchestration / per-project build flags | `build-linux.sh`, `scripts/build.py` |
| Native LLVM / Microsoft SDK bootstrap | `scripts/toolchain.py`, `cmake/toolchains/clang-cl-linux.cmake` |
| Plugin targets, generated headers and link libs | `cmake/Plugins.cmake` |
| Allocator/native dependencies | `cmake/Dependencies.cmake` |
| Loader header-only dependency integration | `cmake/LoaderDependencies.cmake` |
| Required ABI assertions | `cmake/AbiChecks.cpp` |
| Payload composition, original versions and ZIP names | `scripts/package.py` |
| Tag-triggered release build and publication | `.github/workflows/release.yml` |
| Flat GitHub assets, checksums and release notes | `scripts/release.py` |
| Retryable GitHub publication / mocked CLI regression tests | `scripts/publishing.py`, `tests/test_publishing.py` |
| Read-only official submodule/source boundary audit | `scripts/check-sources.py` |
| Release naming, payload and preservation tests | `tests/test_release.py` |
| Atomic distribution collection | `scripts/distribution.py` |
| Artifact checks / CLI | `scripts/verification.py`, `scripts/verify-build.py` |
| Patch workflow regression tests | `tests/test_patches.py` |
| Original startup-crash evidence | `docs/PATCH_2_5.md` |

Source projects: `sources/red4ext`, `sources/archive-xl`, `sources/codeware`,
`sources/tweak-xl`, `sources/input-loader`, `sources/red4ext-sdk`. These are
official upstream submodules. Their patched working contents are expected to be modified;
their HEADs must stay at the commits recorded by the root Gitlinks.

## Normal edit workflow

All commands run from the root. The examples below do not grant Git
permissions: initialization, patch helpers and builds that mutate Git
metadata require the explicit authorization described above.

```bash
git submodule update --init
python3 scripts/patches.py apply all
# Edit sources/codeware/...
python3 scripts/patches.py export codeware
./build-linux.sh --only codeware --jobs 8
git diff -- patches/codeware sources.lock.json
```

Commit only in the root repository when a commit is requested. Follow
[docs/commit-rules.md](docs/commit-rules.md) for commit messages. Root commits
store Gitlinks and patches, never upstream source files. Do not use child
commits as a substitute for export. Community ports must keep attribution in
`patches/README.md` and lockfile provenance.

Patch lists are ordered stacks. `001-essentials.patch` is relative to the
upstream base; later layers are relative to every preceding layer. Default
`export <project>` updates only the final layer, preserving earlier bytes and
checksums. Append an intentional layer with:

```bash
python3 scripts/patches.py export codeware --new-patch 003-description.patch
```

Codeware, TweakXL and ArchiveXL have `002-sdk-api-cleanup.patch` after their
compatibility patches. Never collapse these layers or hand-edit their hashes.
Export includes staged/unstaged, new, deleted, renamed, binary and executable
files and verifies reconstruction. It rejects empty layers, duplicate names
and corrupted prefixes. It uses a temporary index; do not stage submodule
files merely to make export find them.

```bash
python3 scripts/patches.py status
python3 scripts/patches.py check all
```

`apply` preflights all requested projects. Saved content is unchanged; local
edits are preserved and must be exported before packaging. State files under
`build/state/` are auxiliary: actual content and patch checksums are checked.
Never delete state files or force-reset sources to bypass a conflict.

The SDK is now a patchable first-class project:

```bash
python3 scripts/patches.py export red4ext-sdk
./build-linux.sh --jobs 8
```

Do not edit vendor SDK copies. Nested dependency edits/pin changes are rejected
rather than silently omitted from parent patches. Other dependency changes
need a reviewed pin or explicit dependency-patch mechanism.

Only with explicit authorization to change an upstream pin: export, safely
`unapply` the saved patch,
fetch/check out the reviewed upstream commit, stage that Gitlink in the root,
and port/re-export the cumulative patch. `unapply` refuses unexported edits.
Never use `submodule update --remote` as a build step.

## Build and locate failures

```bash
./build-linux.sh --jobs 8
./build-linux.sh --only red4ext --jobs 8
./build-linux.sh --only codeware --jobs 8
./build-linux.sh --only tweakxl --jobs 8
./build-linux.sh --only archivexl --jobs 8
./build-linux.sh --only input-loader --jobs 8
```

`--only` accepts `tweakxl/archivexl/inputloader` and
`tweak-xl/archive-xl/input-loader`. Per-project wrappers are
`scripts/build-<red4ext|codeware|tweakxl|archivexl|inputloader>.sh`.
Use Release for installation. Shared SDK changes require all five builds.
Do not repeat unchanged native builds after documentation-only edits.

| Output | Path |
| --- | --- |
| Loader build/install | `build/red4ext/release/` |
| Shared plugin build/DLLs | `build/plugins/release/bin/` |
| Per-project packages | `build/packages/release/<project>/` |
| Build logs | `build/logs/<project>-release.log` |
| Microsoft SDK/cache | `build/toolchains/` |
| Runtime distribution | `dist/release/` |

Find the first real error, e.g.:

```bash
rg -n -m 20 'fatal error:|error:|CMake Error|Traceback' build/logs/codeware-release.log
```

| Failure | Required response |
| --- | --- |
| Dubious ownership in container CI | Keep the post-checkout trust step in the release workflow: shell Git needs safe.directory entries for GITHUB_WORKSPACE and its descendants. Do not change host Git config or trust every repository. |
| Missing registered Gitlink | Use a Git clone with submodules; a source ZIP lacks revision metadata. |
| Submodule HEAD differs | Preserve edits and inspect the intended root Gitlink. Bootstrap must not check it out forcibly. |
| Source edits are unexported | Export that project, then rebuild. |
| Patch checksum/conflict | Inspect files, lock and patch; never bypass checksum or reset edits. |
| Stale package source/SDK provenance | Rebuild affected projects; SDK changes require the full set. |
| CMake cache points at an old location | Use a fresh generated build tree, not search/replace in CMake caches. |
| Import libs such as Version.lib missing | Linux paths are case-sensitive; use lowercase `version`, `user32`, etc. |
| SDK rtti includes missing | The SDK patch corrects them to `RTTI`; do not create untracked compatibility aliases. |
| Ambiguous Codeware handle conversion | Preserve the explicit `const Handle<IScriptable>&` conversion. |
| CMake 4 rejects old dependency minimums | Preserve root `CMAKE_POLICY_VERSION_MINIMUM 3.10`. |

## ABI and package invariants

Use clang-cl + lld-link with Microsoft's STL/CRT and the single patched
`sources/red4ext-sdk`. Do not substitute MinGW C++ runtimes or weaken ABI checks.
The upstream SDK base is 0f5eba4; the saved patch supplies the later retail
layout/scope fixes. Its effective identity includes the patch checksum.

| Fact | Required 2.5 value |
| --- | --- |
| Fixed callback size / handler offset | 0x48 / 0x40 |
| CGameEngine size / framework offset | 0x390 / 0x348 |
| CStackFrame size | 0xA8 |
| ArchiveScope::Mod | 6 |

Keep explicit `cmake/AbiChecks.cpp` assertions: the SDK's general offset macro
is disabled under Clang. Keep mimalloc 2.2.4 for this TiltedCore; 3.x removes
the `mi_malloc_size` API it uses.
SDK `DynArray` relocation must construct elements in fresh storage with
`std::uninitialized_move`, then destroy the moved-from source elements.
Zero-filled allocator memory does not construct nontrivial objects. Preserve
the native regression in `tests/native/sdk_dynarray_lifetime.cpp`; it exercises
the actual SDK against a fake retail allocator, without loading game DLLs.
Its old-header failure and fixed-header pass establish this container defect,
not a confirmed cause or fix of the game exit crash. See `docs/PATCH_2_5.md`.

The loader injects `cmake/LoaderDependencies.cmake` to consume the unchanged
pinned toml11 headers without its obsolete CMP0127 OLD build configuration.
Keep header-only mode and upstream compile definitions/options; do not edit
dependency sources or globally silence CMake deprecation warnings.

Use `rtti::IType` and `rtti::ERTTIType` in plugin source, not the deprecated
SDK aliases. Keep the warnings-as-errors compiler check on the three targets. Codeware's enum
registration explicitly retains the public redscript name `ERTTIType`, with
a compile-time hash assertion; do not change this name during native cleanup.
Codeware's UTF helper uses checked Win32 conversions. The focused Windows test
is `tests/native/codeware_unicode.cpp`; it has passed in an isolated Wine prefix
for Unicode, embedded NULs and invalid sequences. Wine is optional for this
runtime check, never a build dependency. Keep narrowly scoped Clang annotations
around deliberate polymorphic offsetof checks; never remove the ABI assertions.

Preserve script bundles, metadata, config/hints/assets and licenses. PDBs and
the playground plugin do not belong in runtime ZIPs. Reuse only the pinned
ArchiveXL resource asset, never the old plugin DLL.

Input Loader uses the shared SDK, header-only pinned spdlog with wide-character
support and its upstream-pinned `deps/pugixml`; leave its other dependencies
uninitialized. Keep `input_loader.dll`/`input_loader.pdb` names, upstream readme,
license and `engine/config/platform/pc/input_loader.ini` in packages, plus XML
parser/logging license notices. The packaged INI must remain neutral, with no
generated-cache overrides. Runtime publication uses immutable, version-scoped
generation pairs and a single selector switch; keep the portable failure tests
and optional Windows DLL harness in `tests/test_input_loader*.py`. Never overwrite
a selected generation in place or remove it while another process may read it.
Registrations are startup-only, sealed at BaseInitialization OnEnter; keep the
legacy Add ABI alongside TryAdd/Remove. Read the packaged readme for cancellation,
recovery and Root Builder cleanup rules. Generation paths and first-launch input
behavior still need an actual 2.5 game test. No address hooks are used.

ArchiveXL still lacks `JournalTree_ProcessJournalIndex` and
`ResourceLoader_OnUpdate`: journal indexing and resource hot reload remain
disabled. Cooked instanced-mesh/collision-node element edits remain limited.
Do not guess addresses or claim these limitations are fixed.

## Repository hygiene

Root `.gitignore` excludes generated output, migration backups, local settings
and caches. Keep upstream housekeeping inside its official submodule; do not
add ignore-only patches. `.gitattributes` preserves checksum-pinned patch bytes.
Never normalize or trim patch whitespace by hand; use the exporter.

Shared `.vscode/settings.json` keeps Source Control focused on the root;
other editor settings remain ignored. `.gitmodules` uses `ignore = dirty`
to hide expected patched working contents in parent status while preserving
visibility of changed submodule HEADs. Never infer saved patch state from a
clean parent status: inspect the submodule directly with
`git --no-optional-locks -C sources/<project> status --short`, or use root
`git --no-optional-locks status --short --ignore-submodules=none`.
Do not alter upstream ignore files, reset source edits or use assume-unchanged
or skip-worktree flags to hide this expected patch state.

Only when explicitly asked to stage or create an initial commit, stage root
files and the six `sources/*` Gitlinks. Check
`git --no-optional-locks diff --cached --check` and inspect
`git --no-optional-locks diff --cached --stat`.
No upstream files, build caches or distribution ZIPs belong in that commit.
Dirty submodule contents after patch application are expected.

## Validation and release

- Docs: check paths, commands, links and evidence.
- Patch tooling: run `python3 -m unittest discover -s tests -v` and independent
  `patches.py check`; test preservation, conflicts and real submodule Gitfiles.
- Native source: build affected project. Shared SDK/dependencies: build all.
- Packaging: verify complete payloads and source/SDK identity.

```bash
python3 scripts/verify-build.py
(cd dist/release && sha256sum --check SHA256SUMS)
python3 scripts/verify-build.py --addresses '/path/to/Cyberpunk 2077/bin/x64/cyberpunk2077_addresses.json'
```

Expected unique hash matches: Codeware 107/107, TweakXL 11/11, ArchiveXL 132/134
with only the two disabled-feature hashes missing.

A release uses the five runtime ZIPs, SHA256SUMS and manifest.json. Standard
RED4ext layout and symbols are separate optional extras. The collector verifies
a candidate before replacing the old distribution and rejects mixed provenance.
Release archive names contain original mod versions and meaningful layout
suffixes only; provenance belongs in manifests. Do not add commit, fingerprint,
timestamp or `dirty` suffixes. Debug archives retain `-debug` for clarity.
`SHA256SUMS` covers runtime, standard-layout and symbol archives.

The tag workflow checks official source boundaries, tests and builds the full
Release set in an Arch container, then publishes eleven ZIPs and checksums using
the repository token. Stable tags use `vX.Y.Z`; RC tags produce prereleases.
Publication creates absent releases or uploads prepared filenames with
`--clobber` to existing mutable releases. Preserve existing title/notes and
prerelease choices; finish drafts only after uploads succeed. Matching asset
SHA256 digests make completed reruns a no-op; differing immutable releases
require a new tag. API/auth/network failures must not be treated as missing
releases. Failed CI retains both build logs and prepared package artifacts.
`scripts/publishing.py` performs live GitHub writes; never run it locally
without an explicit publication request. Local `scripts/release.py <tag>` only
prepares flat assets, but its verifier writes Git objects via patch helpers;
do not run it without explicit permission for those mutations. A request to
edit release automation does not authorize a commit, tag, push or live release.

Do not upload toolchain downloads, caches, game data or migration backups.
Failed CI build logs may be retained as workflow diagnostic artifacts.
Report actual validation and remaining runtime limits; do not equate a rebuild
with an in-game test.
