# Compatibility patches

Each directory contains an ordered patch stack against the official upstream
commit recorded by its root Gitlink. `sources.lock.json` records application
order and checksums. The initial `001-essentials.patch` is cumulative against
upstream; later patches apply after all preceding layers.

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
| input-loader | Pinned official upstream plus defensive behavior researched in Keen's InputLoaderReborn binary | Shared SDK v1 API adaptation; `002-re-input-hardening.patch` retains newer upstream startup/cache and action/id fixes; [evidence](../docs/input-loader-research.txt) |
| red4ext-sdk | KelpHect's [retail layout update](https://github.com/wopss/RED4ext.SDK/pull/228), then [ArchiveScope::Mod correction](https://github.com/wopss/RED4ext.SDK/pull/229) | Correct RTTI include casing |

Exact imported source revisions are retained in `sources.lock.json` provenance.
These changes are not all original work by this project; preserve contributor
credit and the [upstream license notices](../licenses/README.md).

The plugins' community vendor-SDK gitlink bumps are deliberately excluded:
all builds use the separate patched official SDK submodule. Build scripts,
toolchains and documentation belong to the root repository rather than being
injected into upstream source patches.
