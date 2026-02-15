#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
header_out_default="${repo_root}/generated/manifest.h"
blob_out_default="${repo_root}/resources/external/kenney_assets.pack"

header_out="${header_out_default}"
blob_out="${blob_out_default}"
workers=""
inflight=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --header)
            header_out="$2"
            shift 2
            ;;
        --blob)
            blob_out="$2"
            shift 2
            ;;
        --workers)
            workers="$2"
            shift 2
            ;;
        --inflight)
            inflight="$2"
            shift 2
            ;;
        *)
            echo "unknown argument: $1" >&2
            echo "usage: $0 [--header <path>] [--blob <path>] [--workers <n>] [--inflight <n>]" >&2
            exit 1
            ;;
    esac
done

if ! command -v uv >/dev/null 2>&1; then
    echo "uv is required to run kenney pipeline" >&2
    exit 1
fi

cd "${repo_root}"

command=(uv run greatbadbeyond pack)
command+=(--header "${header_out}")
command+=(--blob "${blob_out}")

if [[ -n "${workers}" ]]; then
    command+=(--workers "${workers}")
fi
if [[ -n "${inflight}" ]]; then
    command+=(--inflight "${inflight}")
fi

"${command[@]}"
