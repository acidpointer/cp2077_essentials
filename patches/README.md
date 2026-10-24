# Compatibility patches

Each directory contains an ordered patch stack against the official upstream
commit recorded by its root Gitlink. `sources.lock.json` records application
order and checksums. The initial `001-essentials.patch` is cumulative against
upstream; later patches apply after all preceding layers.

Codeware, TweakXL and ArchiveXL additionally use `002-sdk-api-cleanup.patch`
to replace deprecated `CBaseRTTIType`/`ERTTIType` aliases with `rtti::IType` and
`rtti::ERTTIType`. These name changes preserve type identity and ABI. Codeware
explicitly retains its public redscript enum name `ERTTIType` and checks the
name hash at compile time. Its cleanup also replaces deprecated codecvt-based
conversion with checked Windows UTF APIs. TweakXL fixes explicit-specialization
syntax and explicitly handles no-op null/undefined YAML nodes. Deliberate
polymorphic layout assertions are locally annotated, with every assertion
still active. Original compatibility patches remain unchanged.

`python3 scripts/patches.py export <project>` updates only the final layer.
Use `--new-patch 003-description.patch` to append a separate layer. Generate
through this exporter so preceding layers, binary files, modes and source
identity stay consistent; do not create cumulative diffs over later layers.

| Project | Imported compatibility work | Local additions |
| --- | --- | --- |
| red4ext | Official base includes [PR 154](https://github.com/wopss/RED4ext/pull/154) | Clang portability: wide default literal, scalar return qualifiers, unused parameters and initialization order |
| archive-xl | Alexey-shishkin4's [PR 31](https://github.com/psiberx/cp2077-archive-xl/pull/31), including contributed fixes | Header include spelling for case-sensitive hosts |
| codeware | KelpHect's [PR 63](https://github.com/psiberx/cp2077-codeware/pull/63), including contributed fixes | Explicit base-handle conversion under Clang |
| tweak-xl | KelpHect's [PR 22](https://github.com/psiberx/cp2077-tweak-xl/pull/22) | Root build supplies the shared SDK |
| input-loader | Pinned official upstream plus defensive behavior researched in Keen's InputLoaderReborn binary | Shared SDK v1 API adaptation; `002-re-input-hardening.patch` retains newer upstream startup/cache and action/id fixes; [binary evidence](../docs/input-loader-research.txt). `003-durable-input-pipeline.patch` implements transactional generation publication, deterministic snapshot merging and startup lifecycle fixes from the [2.5 audit](../docs/input-loader-2.5-audit.txt). |
| cyber-engine-tweaks | Official v1.38.0 includes game 2.5 support | Loads through RED4ext and its shared SDK resolver; keeps upstream Lua paths; Clang/header casing fixes. The bundled ASI loader is excluded. Native integration needs in-game validation. |
| red4ext-sdk | KelpHect's [retail layout update](https://github.com/wopss/RED4ext.SDK/pull/228), then [ArchiveScope::Mod correction](https://github.com/wopss/RED4ext.SDK/pull/229) | Correct RTTI include casing |

Exact imported source revisions are retained in `sources.lock.json` provenance.
These changes are not all original work by this project; preserve contributor
credit and the [upstream license notices](../licenses/README.md).

The plugins' community vendor-SDK gitlink bumps are deliberately excluded:
all builds use the separate patched official SDK submodule. Build scripts,
toolchains and documentation belong to the root repository rather than being
injected into upstream source patches.

The SDK's `003-transaction-system-abi.patch` corrects the retail transaction
system's listener and visual-tag vtable slots and names its already-locked slot
lookup. ArchiveXL's `003-attachment-lock-reentrancy.patch` uses owner-specific
scopes around three verified slot writers, copies item handles under the slot
lock, and fixes listener ownership across removal. The save-load deadlock lead
was corroborated by [Selentia-IX's published port notes](https://github.com/Selentia-IX/Cyberpunk_2077_2.5_FIX_RED4ext_ArchiveXL_TweakXL_Codeware/releases/tag/r1).
The extra writer paths, SDK corrections and listener lifetime fix were checked
independently against this repository and the retail executable. See the
[investigation](../docs/load-freeze-investigation.txt) and
[change plan and validation](../docs/load-freeze-change-plan.txt). Native tests
establish the corrected ABI and lock/lifetime behavior, not game playtesting.
