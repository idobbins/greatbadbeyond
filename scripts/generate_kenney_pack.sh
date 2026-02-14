#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest_builder="${repo_root}/scripts/build_kenney_manifest.sh"
codegen="${repo_root}/tools/manifest_codegen/generate_manifest.py"
manifest_input="${repo_root}/resources/external/kenney_asset_manifest.tsv"
source_root="${repo_root}/resources"
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
    echo "uv is required to run manifest codegen" >&2
    exit 1
fi

mkdir -p "$(dirname "${header_out}")"
mkdir -p "$(dirname "${blob_out}")"

"${manifest_builder}"

codegen_args=()
if [[ -n "${workers}" ]]; then
    codegen_args+=(--workers "${workers}")
fi
if [[ -n "${inflight}" ]]; then
    codegen_args+=(--inflight "${inflight}")
fi

UV_CACHE_DIR="${UV_CACHE_DIR:-/tmp/uv-cache}" \
uv run python "${codegen}" \
    --input "${manifest_input}" \
    --source-root "${source_root}" \
    --header "${header_out}" \
    --blob "${blob_out}" \
    "${codegen_args[@]}"

echo "wrote ${header_out}"
echo "wrote ${blob_out}"
