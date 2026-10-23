#!/usr/bin/env bash
set -euo pipefail
workspace="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec python3 "$workspace/scripts/build.py" "$@"
