#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v uv >/dev/null 2>&1; then
    echo "uv is required to run kenney pipeline" >&2
    exit 1
fi

cd "${repo_root}"
uv run greatbadbeyond manifest
