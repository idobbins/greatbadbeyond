#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
raw_index="${repo_root}/resources/external/kenney_resource_index.tsv"
semantic_index="${repo_root}/resources/external/kenney_resource_semantic_index.tsv"
semantic_summary="${repo_root}/resources/external/kenney_resource_semantic_index_summary.md"

if [[ ! -f "${raw_index}" ]]; then
    "${repo_root}/scripts/build_kenney_index.sh"
fi

tmp_index="$(mktemp)"
tmp_summary="$(mktemp)"
tmp_tags="$(mktemp)"
trap 'rm -f "${tmp_index}" "${tmp_summary}" "${tmp_tags}"' EXIT

awk -F'\t' '
BEGIN {
    OFS = "\t";
    print "top_level", "pack", "extension", "relative_path", "semantic_kind", "dimension_hint", "content_role", "engine_hint", "semantic_tags";
}

NR == 1 {
    next;
}

function has(haystack, needle) {
    return index(haystack, needle) > 0;
}

function append_tag(tag) {
    if (tag == "")
    {
        return;
    }
    if (index("," tags ",", "," tag ",") == 0)
    {
        if (tags == "")
        {
            tags = tag;
        }
        else
        {
            tags = tags "," tag;
        }
    }
}

{
    top = $1;
    pack = $2;
    ext = tolower($3);
    rel = $4;
    lower = tolower(rel);
    file = lower;
    sub(/^.*\//, "", file);

    kind = "other";
    dimension = "unknown";
    role = "runtime";
    engine = "generic";
    tags = "";

    if ((ext == "obj") || (ext == "fbx") || (ext == "dae") || (ext == "stl") || (ext == "glb") || (ext == "gltf") || (ext == "3ds") || (ext == "blend") || (ext == "skp"))
    {
        kind = "model";
        dimension = "3d";
        append_tag("mesh");
    }
    else if ((ext == "png") || (ext == "jpg") || (ext == "svg"))
    {
        kind = "image";
        dimension = (ext == "svg") ? "2d_vector" : "2d_raster";
    }
    else if (ext == "ogg")
    {
        kind = "audio";
        dimension = "audio";
    }
    else if ((ext == "ttf") || (ext == "otf"))
    {
        kind = "font";
        dimension = "2d_font";
        append_tag("font");
    }
    else if ((ext == "txt") || (ext == "pdf") || (ext == "html"))
    {
        kind = "document";
        dimension = "meta";
    }
    else if (ext == "url")
    {
        kind = "link";
        dimension = "meta";
        role = "external_reference";
    }
    else if (ext == "zip")
    {
        kind = "archive";
        dimension = "bundle";
        role = "distribution_bundle";
    }
    else if ((ext == "tmx") || (ext == "tsx") || (ext == "xml") || (ext == "capx") || (ext == "c3p") || (ext == "unitypackage") || (ext == "swf"))
    {
        kind = "project_data";
        dimension = "tooling";
        role = "tooling_or_project";
    }
    else if ((ext == "mat") || (ext == "bin"))
    {
        kind = "auxiliary_data";
        dimension = "tooling";
    }

    if (has(lower, "/textures/") || has(lower, "texture"))
    {
        append_tag("texture");
    }
    if (has(lower, "sprite") || has(lower, "spritesheet"))
    {
        append_tag("sprite");
    }
    if (has(lower, "icon"))
    {
        append_tag("icon");
    }
    if (has(lower, "ui "))
    {
        append_tag("ui");
    }
    if (has(lower, "ui/") || has(lower, "ui_") || has(lower, "/ui "))
    {
        append_tag("ui");
    }
    if (has(lower, "audio") || has(lower, "music") || has(lower, "sound"))
    {
        append_tag("audio");
    }
    if (has(lower, "preview") || has(file, "sample"))
    {
        append_tag("preview");
        if (role == "runtime")
        {
            role = "preview_or_sample";
        }
    }
    if ((file == "license.txt") || (file == "readme.txt") || has(file, "license") || has(file, "readme") || has(file, "instruction"))
    {
        append_tag("documentation");
        if (role == "runtime")
        {
            role = "documentation";
        }
    }
    if (has(lower, "prototype"))
    {
        append_tag("prototype");
    }
    if (has(lower, "animated") || has(lower, "animation"))
    {
        append_tag("animated");
    }

    if (top == "Audio")
    {
        append_tag("audio_pack");
    }
    else if (top == "2D assets")
    {
        append_tag("2d_pack");
    }
    else if (top == "3D assets")
    {
        append_tag("3d_pack");
    }
    else if (top == "Icons")
    {
        append_tag("icon_pack");
    }
    else if (top == "UI assets")
    {
        append_tag("ui_pack");
    }

    if (ext == "unitypackage" || ext == "mat")
    {
        engine = "unity";
    }
    else if ((ext == "tmx") || (ext == "tsx"))
    {
        engine = "tiled";
    }
    else if (ext == "capx")
    {
        engine = "construct2";
    }
    else if (ext == "c3p")
    {
        engine = "construct3";
    }
    else if (ext == "blend")
    {
        engine = "blender";
    }

    if (tags == "")
    {
        tags = "[none]";
    }

    print top, pack, ext, rel, kind, dimension, role, engine, tags;
}
' "${raw_index}" > "${tmp_index}"

awk -F'\t' '
NR == 1 {
    next;
}
{
    split($9, arr, ",");
    for (i in arr)
    {
        tag = arr[i];
        if ((tag != "") && (tag != "[none]"))
        {
            print tag;
        }
    }
}
' "${tmp_index}" | LC_ALL=C sort | uniq -c | awk '
{
    printf "%s\t%s\n", $2, $1;
}
' | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 > "${tmp_tags}"

{
    total_files="$(( $(wc -l < "${tmp_index}") - 1 ))"
    printf "# Kenney Semantic Index Summary\n\n"
    printf "Total files: %s\n\n" "${total_files}"

    printf "## Semantic Kinds\n\n"
    awk -F'\t' '
        NR > 1 { c[$5] += 1; }
        END {
            for (k in c)
            {
                printf "%s\t%d\n", k, c[k];
            }
        }
    ' "${tmp_index}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '

    printf "\n## Dimension Hints\n\n"
    awk -F'\t' '
        NR > 1 { c[$6] += 1; }
        END {
            for (k in c)
            {
                printf "%s\t%d\n", k, c[k];
            }
        }
    ' "${tmp_index}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '

    printf "\n## Content Roles\n\n"
    awk -F'\t' '
        NR > 1 { c[$7] += 1; }
        END {
            for (k in c)
            {
                printf "%s\t%d\n", k, c[k];
            }
        }
    ' "${tmp_index}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '

    printf "\n## Engine Hints\n\n"
    awk -F'\t' '
        NR > 1 { c[$8] += 1; }
        END {
            for (k in c)
            {
                printf "%s\t%d\n", k, c[k];
            }
        }
    ' "${tmp_index}" | LC_ALL=C sort -t$'\t' -k2,2nr -k1,1 | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '

    printf "\n## Top Semantic Tags\n\n"
    head -n 20 "${tmp_tags}" | awk -F'\t' '
        { printf "- `%s`: %s\n", $1, $2; }
    '
} > "${tmp_summary}"

mv "${tmp_index}" "${semantic_index}"
mv "${tmp_summary}" "${semantic_summary}"

echo "wrote ${semantic_index}"
echo "wrote ${semantic_summary}"
