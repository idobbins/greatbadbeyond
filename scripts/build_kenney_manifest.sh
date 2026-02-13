#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
semantic_index="${repo_root}/resources/external/kenney_resource_semantic_index.tsv"
manifest_file="${repo_root}/resources/external/kenney_asset_manifest.tsv"
manifest_summary="${repo_root}/resources/external/kenney_asset_manifest_summary.md"

if [[ ! -f "${semantic_index}" ]]; then
    "${repo_root}/scripts/build_kenney_semantic_index.sh"
fi

tmp_manifest="$(mktemp)"
tmp_summary="$(mktemp)"
tmp_tags="$(mktemp)"
trap 'rm -f "${tmp_manifest}" "${tmp_summary}" "${tmp_tags}"' EXIT

awk -F'\t' '
BEGIN {
    OFS = "\t";
    print "asset_name", "asset_relative_path", "semantic_kind", "content_role", "engine_hint", "semantic_tags";
}
NR == 1 {
    next;
}
{
    rel = $4;
    kind = $5;
    role = $7;
    engine = $8;
    tags = $9;

    name = tolower(rel);
    gsub(/[^a-z0-9]+/, "_", name);
    gsub(/^_+/, "", name);
    gsub(/_+$/, "", name);

    if (name == "")
    {
        name = "unnamed_asset";
    }

    seen[name] += 1;
    if (seen[name] > 1)
    {
        name = name "_" seen[name];
    }

    print name, "external/Kenney/" rel, kind, role, engine, tags;
}
' "${semantic_index}" > "${tmp_manifest}"

awk -F'\t' '
NR == 1 {
    next;
}
{
    count += 1;
    kind[$3] += 1;
    role[$4] += 1;
    engine[$5] += 1;
}
END {
    printf "total\t%d\n", count;
    for (k in kind)
    {
        printf "kind\t%s\t%d\n", k, kind[k];
    }
    for (k in role)
    {
        printf "role\t%s\t%d\n", k, role[k];
    }
    for (k in engine)
    {
        printf "engine\t%s\t%d\n", k, engine[k];
    }
}
' "${tmp_manifest}" > "${tmp_tags}"

{
    total_assets="$(awk -F'\t' '$1 == "total" { print $2 }' "${tmp_tags}")"

    printf "# Kenney Asset Manifest Summary\n\n"
    printf "Total assets: %s\n\n" "${total_assets}"

    printf "## By Semantic Kind\n\n"
    awk -F'\t' '$1 == "kind" { printf "%s\t%s\n", $2, $3 }' "${tmp_tags}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '

    printf "\n## By Content Role\n\n"
    awk -F'\t' '$1 == "role" { printf "%s\t%s\n", $2, $3 }' "${tmp_tags}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '

    printf "\n## By Engine Hint\n\n"
    awk -F'\t' '$1 == "engine" { printf "%s\t%s\n", $2, $3 }' "${tmp_tags}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '
} > "${tmp_summary}"

mv "${tmp_manifest}" "${manifest_file}"
mv "${tmp_summary}" "${manifest_summary}"

echo "wrote ${manifest_file}"
echo "wrote ${manifest_summary}"
