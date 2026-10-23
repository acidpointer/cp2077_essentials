# Official-submodule migration

The migration preserves the runtime source content of the previous essentials
set, which the maintainer confirmed starts CP2077 2.5. Game/MO2 files were not
changed. Gameplay remains untested; rebuilt packages need a fresh startup test.

## Source representation

The root tracks five official upstream Gitlinks and one cumulative patch per
project. The loader base already includes 2.5 support; the other mod patches
include the community ports as well as local fixes. The separate SDK patch
includes retail layouts, archive scope 6, added headers and RTTI include casing.
Plugin vendor-SDK gitlink changes and local xmake routing are replaced by the
root build's shared SDK configuration. Build tooling and research live in root.

## Validation performed

- Relevant mod source files compared with the startup-tested trees.
- SDK source compared with include-path casing normalized for C++ files.
- All five patches independently reconstructed from official bases.
- A fresh disposable Git clone initialized official submodules and reproduced
  every patch byte-for-byte on export, then applied them repeatedly.
- Eleven regression tests cover patch data/modes/index preservation/conflicts
  and distribution failure preservation.
- All four Release builds succeeded with explicit 2.5 ABI checks.
- DLL exports, matching packaged PDBs, ZIP payloads, CRCs, source/SDK identity
  and checksums passed.
- Installed-game hashes: Codeware 107/107, TweakXL 11/11, ArchiveXL 129/131.
  The two absent ArchiveXL features remain disabled.

## Local recovery material (not published)

Old working repositories and their build outputs are retained at
`build/legacy/`. The original root files, original release output, old flat
patches and pre-migration Git index/config copies are under
`build/migration-backup/`. Source comparison records and verification logs are
also under `build/`. The candidate distribution is retained in
`build/migration-dist/release/`; the verified current set is in `dist/release/`.

These backups preserve the old layout for inspection and recovery; do not
blindly overwrite a later Git index/config or reset source edits with them.
Review current Git state and export any new edits before attempting a rollback.
