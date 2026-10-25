E33 startup and core implementation records, 2026-10-25
======================================================

See ../legacy-plugin-compat-evidence.txt, E33, and ../../compat/README.txt.

build-*.json preserves input/binary checksums and successful build steps.
suite-*.json preserves 15 Windows runs on each runtime and 2 portable runs.
mod-settings-*.json preserves the unchanged DLL identity, successful bridged
startup checkpoint, and expected-fault unbridged control. These tests stop at
script registration and do not invoke registration callbacks, install hooks,
load the game or test DLL teardown. The test host supplies synthetic services.
sanitizers.txt preserves the successful portable ASan/UBSan/LSan CTest log.

Binary inputs and generated test hosts are not redistributed here. Rebuild and
run with scripts/test-compatibility.py from the repository root. The historical
SDK commit and effective current-header identity are in the build records.

Verify these records from this directory:
  sha256sum --check SHA256SUMS

These records establish the described boundaries only. They do not establish
production loader admission or complete Mod Settings compatibility.
