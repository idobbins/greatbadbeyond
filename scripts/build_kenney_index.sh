#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
kenney_root="${repo_root}/resources/external/Kenney"
index_file="${repo_root}/resources/external/kenney_resource_index.tsv"
summary_file="${repo_root}/resources/external/kenney_resource_index_summary.md"

if [[ ! -d "${kenney_root}" ]]; then
    echo "error: missing directory: ${kenney_root}" >&2
    exit 1
fi

tmp_index="$(mktemp)"
tmp_summary="$(mktemp)"
trap 'rm -f "${tmp_index}" "${tmp_summary}"' EXIT

{
    printf "top_level\tpack\textension\trelative_path\n"
    (
        cd "${repo_root}"
        rg --files "resources/external/Kenney" | LC_ALL=C sort
    ) | awk '
        BEGIN {
            prefix = "resources/external/Kenney/";
            plen = length(prefix);
            OFS = "\t";
        }
        {
            path = $0;
            rel = substr(path, plen + 1);

            n = split(rel, parts, "/");
            top = (n >= 2) ? parts[1] : "[root]";
            pack = (n >= 2) ? parts[2] : "";

            file = (n >= 1) ? parts[n] : "";
            ext = "";
            if (match(file, /\.[^.]+$/))
            {
                ext = substr(file, RSTART + 1);
            }

            print top, pack, ext, rel;
        }
    '
} > "${tmp_index}"

{
    total_files="$(( $(wc -l < "${tmp_index}") - 1 ))"
    printf "# Kenney Resource Index Summary\n\n"
    printf "Total files: %s\n\n" "${total_files}"

    printf "## Top-Level Buckets\n\n"
    awk -F'\t' '
        NR > 1 {
            top[$1] += 1;
        }
        END {
            for (k in top)
            {
                printf "%s\t%d\n", k, top[k];
            }
        }
    ' "${tmp_index}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        {
            printf "- `%s`: %s\n", $1, $2;
        }
    '

    printf "\n## Extensions\n\n"
    awk -F'\t' '
        NR > 1 {
            ext_key = ($3 == "") ? "[no_ext]" : $3;
            ext[ext_key] += 1;
        }
        END {
            for (k in ext)
            {
                printf "%s\t%d\n", k, ext[k];
            }
        }
    ' "${tmp_index}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        {
            printf "- `%s`: %s\n", $1, $2;
        }
    '
} > "${tmp_summary}"

mv "${tmp_index}" "${index_file}"
mv "${tmp_summary}" "${summary_file}"

echo "wrote ${index_file}"
echo "wrote ${summary_file}"
