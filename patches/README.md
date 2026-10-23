# Compatibility patches

Each directory contains one cumulative patch against the official upstream
commit recorded by its root Gitlink. Generate patches with
`python3 scripts/patches.py export <project>`, not a diff against an already
patched source snapshot.

| Project | Imported compatibility work | Local additions |
| --- | --- | --- |
| red4ext | Official base includes [PR 154](https://github.com/wopss/RED4ext/pull/154) | Clang portability: wide default literal, scalar return qualifiers, unused parameters and initialization order |
| archive-xl | Alexey-shishkin4's [PR 31](https://github.com/psiberx/cp2077-archive-xl/pull/31), including contributed fixes | Header include spelling for case-sensitive hosts |
| codeware | KelpHect's [PR 63](https://github.com/psiberx/cp2077-codeware/pull/63), including contributed fixes | Explicit base-handle conversion under Clang |
| tweak-xl | KelpHect's [PR 22](https://github.com/psiberx/cp2077-tweak-xl/pull/22) | Root build supplies the shared SDK |
| red4ext-sdk | KelpHect's [retail layout update](https://github.com/wopss/RED4ext.SDK/pull/228), then [ArchiveScope::Mod correction](https://github.com/wopss/RED4ext.SDK/pull/229) | Correct RTTI include casing |

Exact imported source revisions are retained in `sources.lock.json` provenance.
These changes are not all original work by this project; preserve contributor
credit and the [upstream license notices](../licenses/README.md).

The plugins' community vendor-SDK gitlink bumps are deliberately excluded:
all builds use the separate patched official SDK submodule. Build scripts,
toolchains and documentation belong to the root repository rather than being
injected into upstream source patches.
