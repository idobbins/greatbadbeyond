#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_KENNEY_ROOT = REPO_ROOT / "resources" / "external" / "Kenney"
DEFAULT_RAW_INDEX = REPO_ROOT / "resources" / "external" / "kenney_resource_index.tsv"
DEFAULT_RAW_SUMMARY = REPO_ROOT / "resources" / "external" / "kenney_resource_index_summary.md"
DEFAULT_SEMANTIC_INDEX = REPO_ROOT / "resources" / "external" / "kenney_resource_semantic_index.tsv"
DEFAULT_SEMANTIC_SUMMARY = REPO_ROOT / "resources" / "external" / "kenney_resource_semantic_index_summary.md"
DEFAULT_MANIFEST = REPO_ROOT / "resources" / "external" / "kenney_asset_manifest.tsv"
DEFAULT_MANIFEST_SUMMARY = REPO_ROOT / "resources" / "external" / "kenney_asset_manifest_summary.md"
DEFAULT_SOURCE_ROOT = REPO_ROOT / "resources"
DEFAULT_HEADER_OUT = REPO_ROOT / "generated" / "manifest.h"
DEFAULT_BLOB_OUT = REPO_ROOT / "resources" / "external" / "kenney_assets.pack"
CODEGEN_SCRIPT = REPO_ROOT / "tools" / "manifest_codegen" / "generate_manifest.py"

MODEL_EXTENSIONS = {
    "obj",
    "fbx",
    "dae",
    "stl",
    "glb",
    "gltf",
    "3ds",
    "blend",
    "skp",
}
IMAGE_EXTENSIONS = {"png", "jpg", "svg"}


def write_tsv(path: Path, header: list[str], rows: list[list[str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    with tmp_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)
    tmp_path.replace(path)
    print(f"wrote {path}")


def write_text(path: Path, lines: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_path = path.with_suffix(path.suffix + ".tmp")
    tmp_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    tmp_path.replace(path)
    print(f"wrote {path}")


def sorted_counts(counter: Counter[str]) -> list[tuple[str, int]]:
    return sorted(counter.items(), key=lambda item: (-item[1], item[0]))


def build_index(kenney_root: Path, index_path: Path, summary_path: Path) -> None:
    if not kenney_root.is_dir():
        raise SystemExit(f"error: missing directory: {kenney_root}")

    relative_files: list[str] = []
    for path in kenney_root.rglob("*"):
        if path.is_file():
            relative_files.append(path.relative_to(kenney_root).as_posix())
    relative_files.sort()

    rows: list[list[str]] = []
    top_level_counts: Counter[str] = Counter()
    extension_counts: Counter[str] = Counter()
    for relative_path in relative_files:
        parts = relative_path.split("/")
        if len(parts) >= 2:
            top_level = parts[0]
            pack = parts[1]
        else:
            top_level = "[root]"
            pack = ""

        file_name = parts[-1] if parts else relative_path
        ext_start = file_name.rfind(".")
        extension = ""
        if (ext_start > -1) and (ext_start < (len(file_name) - 1)):
            extension = file_name[ext_start + 1 :]

        rows.append([top_level, pack, extension, relative_path])
        top_level_counts[top_level] += 1
        extension_counts[extension if extension else "[no_ext]"] += 1

    write_tsv(
        index_path,
        ["top_level", "pack", "extension", "relative_path"],
        rows,
    )

    summary_lines = [
        "# Kenney Resource Index Summary",
        "",
        f"Total files: {len(rows)}",
        "",
        "## Top-Level Buckets",
        "",
    ]
    for key, count in sorted_counts(top_level_counts):
        summary_lines.append(f"- `{key}`: {count}")

    summary_lines.extend(["", "## Extensions", ""])
    for key, count in sorted_counts(extension_counts):
        summary_lines.append(f"- `{key}`: {count}")

    write_text(summary_path, summary_lines)


def add_unique_tag(tags: list[str], tag: str) -> None:
    if (not tag) or (tag in tags):
        return
    tags.append(tag)


def build_semantic_row(
    top_level: str,
    pack: str,
    extension: str,
    relative_path: str,
) -> list[str]:
    ext = extension.lower()
    lower_path = relative_path.lower()
    file_name = lower_path.split("/")[-1]

    semantic_kind = "other"
    dimension_hint = "unknown"
    content_role = "runtime"
    engine_hint = "generic"
    semantic_tags: list[str] = []

    if ext in MODEL_EXTENSIONS:
        semantic_kind = "model"
        dimension_hint = "3d"
        add_unique_tag(semantic_tags, "mesh")
    elif ext in IMAGE_EXTENSIONS:
        semantic_kind = "image"
        dimension_hint = "2d_vector" if ext == "svg" else "2d_raster"
    elif ext == "ogg":
        semantic_kind = "audio"
        dimension_hint = "audio"
    elif ext in {"ttf", "otf"}:
        semantic_kind = "font"
        dimension_hint = "2d_font"
        add_unique_tag(semantic_tags, "font")
    elif ext in {"txt", "pdf", "html"}:
        semantic_kind = "document"
        dimension_hint = "meta"
    elif ext == "url":
        semantic_kind = "link"
        dimension_hint = "meta"
        content_role = "external_reference"
    elif ext == "zip":
        semantic_kind = "archive"
        dimension_hint = "bundle"
        content_role = "distribution_bundle"
    elif ext in {"tmx", "tsx", "xml", "capx", "c3p", "unitypackage", "swf"}:
        semantic_kind = "project_data"
        dimension_hint = "tooling"
        content_role = "tooling_or_project"
    elif ext in {"mat", "bin"}:
        semantic_kind = "auxiliary_data"
        dimension_hint = "tooling"

    if ("/textures/" in lower_path) or ("texture" in lower_path):
        add_unique_tag(semantic_tags, "texture")
    if ("sprite" in lower_path) or ("spritesheet" in lower_path):
        add_unique_tag(semantic_tags, "sprite")
    if "icon" in lower_path:
        add_unique_tag(semantic_tags, "icon")
    if "ui " in lower_path:
        add_unique_tag(semantic_tags, "ui")
    if ("ui/" in lower_path) or ("ui_" in lower_path) or ("/ui " in lower_path):
        add_unique_tag(semantic_tags, "ui")
    if ("audio" in lower_path) or ("music" in lower_path) or ("sound" in lower_path):
        add_unique_tag(semantic_tags, "audio")
    if ("preview" in lower_path) or ("sample" in file_name):
        add_unique_tag(semantic_tags, "preview")
        if content_role == "runtime":
            content_role = "preview_or_sample"
    if (
        (file_name == "license.txt")
        or (file_name == "readme.txt")
        or ("license" in file_name)
        or ("readme" in file_name)
        or ("instruction" in file_name)
    ):
        add_unique_tag(semantic_tags, "documentation")
        if content_role == "runtime":
            content_role = "documentation"
    if "prototype" in lower_path:
        add_unique_tag(semantic_tags, "prototype")
    if ("animated" in lower_path) or ("animation" in lower_path):
        add_unique_tag(semantic_tags, "animated")

    if top_level == "Audio":
        add_unique_tag(semantic_tags, "audio_pack")
    elif top_level == "2D assets":
        add_unique_tag(semantic_tags, "2d_pack")
    elif top_level == "3D assets":
        add_unique_tag(semantic_tags, "3d_pack")
    elif top_level == "Icons":
        add_unique_tag(semantic_tags, "icon_pack")
    elif top_level == "UI assets":
        add_unique_tag(semantic_tags, "ui_pack")

    if ext in {"unitypackage", "mat"}:
        engine_hint = "unity"
    elif ext in {"tmx", "tsx"}:
        engine_hint = "tiled"
    elif ext == "capx":
        engine_hint = "construct2"
    elif ext == "c3p":
        engine_hint = "construct3"
    elif ext == "blend":
        engine_hint = "blender"

    tags = ",".join(semantic_tags) if semantic_tags else "[none]"
    return [
        top_level,
        pack,
        ext,
        relative_path,
        semantic_kind,
        dimension_hint,
        content_role,
        engine_hint,
        tags,
    ]


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle, delimiter="\t")
        return [dict(row) for row in reader]


def build_semantic_index(
    kenney_root: Path,
    raw_index_path: Path,
    raw_summary_path: Path,
    semantic_index_path: Path,
    semantic_summary_path: Path,
) -> None:
    if not raw_index_path.exists():
        build_index(kenney_root, raw_index_path, raw_summary_path)

    rows_in = read_tsv(raw_index_path)

    rows_out: list[list[str]] = []
    kind_counts: Counter[str] = Counter()
    dimension_counts: Counter[str] = Counter()
    role_counts: Counter[str] = Counter()
    engine_counts: Counter[str] = Counter()
    tag_counts: Counter[str] = Counter()

    for row in rows_in:
        semantic_row = build_semantic_row(
            row["top_level"],
            row["pack"],
            row["extension"],
            row["relative_path"],
        )
        rows_out.append(semantic_row)
        kind_counts[semantic_row[4]] += 1
        dimension_counts[semantic_row[5]] += 1
        role_counts[semantic_row[6]] += 1
        engine_counts[semantic_row[7]] += 1
        for tag in semantic_row[8].split(","):
            if tag and (tag != "[none]"):
                tag_counts[tag] += 1

    write_tsv(
        semantic_index_path,
        [
            "top_level",
            "pack",
            "extension",
            "relative_path",
            "semantic_kind",
            "dimension_hint",
            "content_role",
            "engine_hint",
            "semantic_tags",
        ],
        rows_out,
    )

    summary_lines = [
        "# Kenney Semantic Index Summary",
        "",
        f"Total files: {len(rows_out)}",
        "",
        "## Semantic Kinds",
        "",
    ]
    for key, count in sorted_counts(kind_counts):
        summary_lines.append(f"- `{key}`: {count}")

    summary_lines.extend(["", "## Dimension Hints", ""])
    for key, count in sorted_counts(dimension_counts):
        summary_lines.append(f"- `{key}`: {count}")

    summary_lines.extend(["", "## Content Roles", ""])
    for key, count in sorted_counts(role_counts):
        summary_lines.append(f"- `{key}`: {count}")

    summary_lines.extend(["", "## Engine Hints", ""])
    for key, count in sorted_counts(engine_counts):
        summary_lines.append(f"- `{key}`: {count}")

    summary_lines.extend(["", "## Top Semantic Tags", ""])
    for key, count in sorted_counts(tag_counts)[:20]:
        summary_lines.append(f"- `{key}`: {count}")

    write_text(semantic_summary_path, summary_lines)


def sanitize_manifest_name(relative_path: str) -> str:
    name = re.sub(r"[^a-z0-9]+", "_", relative_path.lower()).strip("_")
    if not name:
        name = "unnamed_asset"
    return name


def build_manifest(
    kenney_root: Path,
    raw_index_path: Path,
    raw_summary_path: Path,
    semantic_index_path: Path,
    semantic_summary_path: Path,
    manifest_path: Path,
    manifest_summary_path: Path,
) -> None:
    if not semantic_index_path.exists():
        build_semantic_index(
            kenney_root=kenney_root,
            raw_index_path=raw_index_path,
            raw_summary_path=raw_summary_path,
            semantic_index_path=semantic_index_path,
            semantic_summary_path=semantic_summary_path,
        )

    rows_in = read_tsv(semantic_index_path)

    seen_names: Counter[str] = Counter()
    rows_out: list[list[str]] = []
    kind_counts: Counter[str] = Counter()
    role_counts: Counter[str] = Counter()
    engine_counts: Counter[str] = Counter()

    for row in rows_in:
        relative_path = row["relative_path"]
        name = sanitize_manifest_name(relative_path)
        seen_names[name] += 1
        if seen_names[name] > 1:
            name = f"{name}_{seen_names[name]}"

        semantic_kind = row["semantic_kind"]
        content_role = row["content_role"]
        engine_hint = row["engine_hint"]
        semantic_tags = row["semantic_tags"]

        rows_out.append(
            [
                name,
                f"external/Kenney/{relative_path}",
                semantic_kind,
                content_role,
                engine_hint,
                semantic_tags,
            ]
        )

        kind_counts[semantic_kind] += 1
        role_counts[content_role] += 1
        engine_counts[engine_hint] += 1

    write_tsv(
        manifest_path,
        [
            "asset_name",
            "asset_relative_path",
            "semantic_kind",
            "content_role",
            "engine_hint",
            "semantic_tags",
        ],
        rows_out,
    )

    summary_lines = [
        "# Kenney Asset Manifest Summary",
        "",
        f"Total assets: {len(rows_out)}",
        "",
        "## By Semantic Kind",
        "",
    ]
    for key, count in sorted_counts(kind_counts):
        summary_lines.append(f"- `{key}`: {count}")

    summary_lines.extend(["", "## By Content Role", ""])
    for key, count in sorted_counts(role_counts):
        summary_lines.append(f"- `{key}`: {count}")

    summary_lines.extend(["", "## By Engine Hint", ""])
    for key, count in sorted_counts(engine_counts):
        summary_lines.append(f"- `{key}`: {count}")

    write_text(manifest_summary_path, summary_lines)


def run_pack_codegen(
    manifest_path: Path,
    source_root: Path,
    header_out: Path,
    blob_out: Path,
    workers: int,
    inflight: int,
) -> None:
    if not CODEGEN_SCRIPT.is_file():
        raise SystemExit(f"error: missing codegen script: {CODEGEN_SCRIPT}")

    command = [
        sys.executable,
        str(CODEGEN_SCRIPT),
        "--input",
        str(manifest_path),
        "--source-root",
        str(source_root),
        "--header",
        str(header_out),
        "--blob",
        str(blob_out),
        "--workers",
        str(workers),
        "--inflight",
        str(inflight),
    ]
    subprocess.run(command, check=True)


def add_shared_path_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--kenney-root", type=Path, default=DEFAULT_KENNEY_ROOT)
    parser.add_argument("--raw-index", type=Path, default=DEFAULT_RAW_INDEX)
    parser.add_argument("--raw-summary", type=Path, default=DEFAULT_RAW_SUMMARY)
    parser.add_argument("--semantic-index", type=Path, default=DEFAULT_SEMANTIC_INDEX)
    parser.add_argument("--semantic-summary", type=Path, default=DEFAULT_SEMANTIC_SUMMARY)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--manifest-summary", type=Path, default=DEFAULT_MANIFEST_SUMMARY)


def add_pack_args(parser: argparse.ArgumentParser, default_workers: int) -> None:
    parser.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    parser.add_argument("--header", type=Path, default=DEFAULT_HEADER_OUT)
    parser.add_argument("--blob", type=Path, default=DEFAULT_BLOB_OUT)
    parser.add_argument("--workers", type=int, default=default_workers)
    parser.add_argument("--inflight", type=int, default=default_workers * 2)


def parse_args() -> argparse.Namespace:
    default_workers = max(1, min(8, (os.cpu_count() or 1)))

    parser = argparse.ArgumentParser(description="Build Kenney asset indexes, manifest, and binary pack")
    subparsers = parser.add_subparsers(dest="command", required=True)

    index_parser = subparsers.add_parser("index", help="Build raw file index and summary")
    index_parser.add_argument("--kenney-root", type=Path, default=DEFAULT_KENNEY_ROOT)
    index_parser.add_argument("--output", type=Path, default=DEFAULT_RAW_INDEX)
    index_parser.add_argument("--summary", type=Path, default=DEFAULT_RAW_SUMMARY)

    semantic_parser = subparsers.add_parser("semantic-index", help="Build semantic index and summary")
    semantic_parser.add_argument("--kenney-root", type=Path, default=DEFAULT_KENNEY_ROOT)
    semantic_parser.add_argument("--raw-index", type=Path, default=DEFAULT_RAW_INDEX)
    semantic_parser.add_argument("--raw-summary", type=Path, default=DEFAULT_RAW_SUMMARY)
    semantic_parser.add_argument("--output", type=Path, default=DEFAULT_SEMANTIC_INDEX)
    semantic_parser.add_argument("--summary", type=Path, default=DEFAULT_SEMANTIC_SUMMARY)

    manifest_parser = subparsers.add_parser("manifest", help="Build manifest TSV and summary")
    add_shared_path_args(manifest_parser)

    pack_parser = subparsers.add_parser("pack", help="Build manifest and binary pack/header")
    add_shared_path_args(pack_parser)
    add_pack_args(pack_parser, default_workers)

    all_parser = subparsers.add_parser("all", help="Rebuild index, semantic index, manifest, and pack")
    add_shared_path_args(all_parser)
    add_pack_args(all_parser, default_workers)

    return parser.parse_args()


def run_index(args: argparse.Namespace) -> None:
    build_index(args.kenney_root, args.output, args.summary)


def run_semantic_index(args: argparse.Namespace) -> None:
    build_semantic_index(
        kenney_root=args.kenney_root,
        raw_index_path=args.raw_index,
        raw_summary_path=args.raw_summary,
        semantic_index_path=args.output,
        semantic_summary_path=args.summary,
    )


def run_manifest(args: argparse.Namespace) -> None:
    build_manifest(
        kenney_root=args.kenney_root,
        raw_index_path=args.raw_index,
        raw_summary_path=args.raw_summary,
        semantic_index_path=args.semantic_index,
        semantic_summary_path=args.semantic_summary,
        manifest_path=args.manifest,
        manifest_summary_path=args.manifest_summary,
    )


def run_pack(args: argparse.Namespace) -> None:
    build_manifest(
        kenney_root=args.kenney_root,
        raw_index_path=args.raw_index,
        raw_summary_path=args.raw_summary,
        semantic_index_path=args.semantic_index,
        semantic_summary_path=args.semantic_summary,
        manifest_path=args.manifest,
        manifest_summary_path=args.manifest_summary,
    )
    run_pack_codegen(
        manifest_path=args.manifest,
        source_root=args.source_root,
        header_out=args.header,
        blob_out=args.blob,
        workers=args.workers,
        inflight=args.inflight,
    )


def run_all(args: argparse.Namespace) -> None:
    build_index(args.kenney_root, args.raw_index, args.raw_summary)
    build_semantic_index(
        kenney_root=args.kenney_root,
        raw_index_path=args.raw_index,
        raw_summary_path=args.raw_summary,
        semantic_index_path=args.semantic_index,
        semantic_summary_path=args.semantic_summary,
    )
    build_manifest(
        kenney_root=args.kenney_root,
        raw_index_path=args.raw_index,
        raw_summary_path=args.raw_summary,
        semantic_index_path=args.semantic_index,
        semantic_summary_path=args.semantic_summary,
        manifest_path=args.manifest,
        manifest_summary_path=args.manifest_summary,
    )
    run_pack_codegen(
        manifest_path=args.manifest,
        source_root=args.source_root,
        header_out=args.header,
        blob_out=args.blob,
        workers=args.workers,
        inflight=args.inflight,
    )


def main() -> None:
    args = parse_args()
    if args.command == "index":
        run_index(args)
    elif args.command == "semantic-index":
        run_semantic_index(args)
    elif args.command == "manifest":
        run_manifest(args)
    elif args.command == "pack":
        run_pack(args)
    elif args.command == "all":
        run_all(args)
    else:
        raise SystemExit(f"error: unknown command {args.command}")


if __name__ == "__main__":
    main()

