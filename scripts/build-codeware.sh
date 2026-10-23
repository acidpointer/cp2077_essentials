#!/usr/bin/env bash
set -euo pipefail
workspace="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$workspace/build-linux.sh" --only codeware "$@"
