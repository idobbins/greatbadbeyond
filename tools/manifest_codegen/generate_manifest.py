#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import hashlib
import os
import struct
import sys
import tempfile
import time
import zlib
from array import array
from collections import defaultdict
from concurrent.futures import Future
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf
from PIL import Image


PACK_MAGIC = 0x4B504247  # "GBPK"
PACK_VERSION = 3
INVALID_INDEX = 0xFFFFFFFF

FLAG_ALIAS = 1 << 0
FLAG_CONVERSION_FAILED = 1 << 1
FLAG_HAS_BOUNDS = 1 << 2

KIND_RAW = 0
KIND_MESH = 1
KIND_IMAGE = 2
KIND_AUDIO = 3
KIND_DOCUMENT = 4
KIND_OTHER = 5

FORMAT_RAW_BYTES = 0
FORMAT_MESH_PNUV_F32_U32 = 1
FORMAT_IMAGE_RGBA8_MIPS = 2
FORMAT_AUDIO_PCM16_INTERLEAVED = 3

CODEC_NONE = 0
CODEC_DEFLATE_ZLIB = 1

HEADER_STRUCT = struct.Struct("<IIIIIQQQQQQ")
RECORD_STRUCT = struct.Struct("<" + ("I" * 29) + ("Q" * 3))


@dataclass(frozen=True)
class StringRef:
    offset: int
    length: int


@dataclass(frozen=True)
class AssetRow:
    name: str
    relative_path: str
    semantic_kind: str
    content_role: str
    engine_hint: str
    semantic_tags: str


@dataclass
class BuildRecord:
    name_ref: StringRef
    path_ref: StringRef
    kind_ref: StringRef
    role_ref: StringRef
    engine_ref: StringRef
    tags_ref: StringRef
    kind_enum: int = KIND_OTHER
    format_enum: int = FORMAT_RAW_BYTES
    flags: int = 0
    alias_index: int = INVALID_INDEX
    meta0: int = 0
    meta1: int = 0
    meta2: int = 0
    meta3: int = 0
    compression_codec: int = CODEC_NONE
    aux0: int = 0
    aux1: int = 0
    aux2: int = 0
    aux3: int = 0
    aux4: int = 0
    aux5: int = 0
    aux6: int = 0
    aux7: int = 0
    payload_offset: int = 0
    payload_size: int = 0
    decoded_size: int = 0


@dataclass(frozen=True)
class ConversionResult:
    kind_enum: int
    format_enum: int
    meta0: int
    meta1: int
    meta2: int
    meta3: int
    flags: int
    aux: tuple[int, int, int, int, int, int, int, int]
    digest: bytes
    compression_codec: int
    stored_payload: bytes
    decoded_size: int


class StringTableBuilder:
    def __init__(self) -> None:
        self._bytes = bytearray()
        self._refs: dict[str, StringRef] = {}

    def intern(self, value: str) -> StringRef:
        if value in self._refs:
            return self._refs[value]

        encoded = value.encode("utf-8")
        offset = len(self._bytes)
        self._bytes.extend(encoded)
        self._bytes.append(0)
        ref = StringRef(offset=offset, length=len(encoded))
        self._refs[value] = ref
        return ref

    @property
    def bytes(self) -> bytes:
        return bytes(self._bytes)


class ProgressBar:
    def __init__(self, label: str, total: int) -> None:
        self.label = label
        self.total = max(total, 1)
        self.current = 0
        self.is_tty = sys.stderr.isatty()
        self.start_time = time.monotonic()
        self.last_emit_time = 0.0
        self.next_non_tty_ratio = 0.0

    def _format_line(self) -> str:
        ratio = min(max(self.current / self.total, 0.0), 1.0)
        elapsed = time.monotonic() - self.start_time
        if self.is_tty:
            width = 34
            filled = int(width * ratio)
            bar = ("#" * filled) + ("-" * (width - filled))
            return f"[{self.label}] [{bar}] {self.current}/{self.total} {ratio*100:5.1f}% {elapsed:6.1f}s"
        return f"[{self.label}] {self.current}/{self.total} ({ratio*100:5.1f}%) {elapsed:6.1f}s"

    def update(self, current: int) -> None:
        self.current = min(max(current, 0), self.total)
        now = time.monotonic()

        if self.is_tty:
            if (self.current < self.total) and ((now - self.last_emit_time) < 0.08):
                return
            self.last_emit_time = now
            sys.stderr.write("\r" + self._format_line())
            if self.current >= self.total:
                sys.stderr.write("\n")
            sys.stderr.flush()
            return

        ratio = self.current / self.total
        if (self.current < self.total) and (ratio < self.next_non_tty_ratio):
            return

        self.next_non_tty_ratio = min(1.0, ratio + 0.05)
        sys.stderr.write(self._format_line() + "\n")
        sys.stderr.flush()

    def finish(self) -> None:
        if self.current < self.total:
            self.update(self.total)


def sanitize_identifier(value: str) -> str:
    lowered = value.strip().lower()
    out = []
    prev_us = False
    for ch in lowered:
        if ch.isalnum() or ch == "_":
            out.append(ch)
            prev_us = False
        else:
            if not prev_us:
                out.append("_")
            prev_us = True

    result = "".join(out).strip("_")
    if not result:
        result = "item"
    if result[0].isdigit():
        result = f"n{result}"
    return result


def parse_pack_path(asset_relative_path: str) -> tuple[str, str, str]:
    prefix = "external/Kenney/"
    rel = asset_relative_path
    if rel.startswith(prefix):
        rel = rel[len(prefix) :]

    parts = rel.split("/")
    if len(parts) < 2:
        return "[root]", "[root]", rel

    top_level = parts[0]
    pack = parts[1]
    leaf = "/".join(parts[2:]) if len(parts) > 2 else parts[-1]
    return top_level, pack, leaf


def read_rows(path: Path) -> list[AssetRow]:
    rows: list[AssetRow] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f, delimiter="\t")
        required = {
            "asset_name",
            "asset_relative_path",
            "semantic_kind",
            "content_role",
            "engine_hint",
            "semantic_tags",
        }
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"Manifest missing required columns: {sorted(missing)}")

        for row in reader:
            rows.append(
                AssetRow(
                    name=row["asset_name"],
                    relative_path=row["asset_relative_path"],
                    semantic_kind=row["semantic_kind"],
                    content_role=row["content_role"],
                    engine_hint=row["engine_hint"],
                    semantic_tags=row["semantic_tags"],
                )
            )

    return rows


def semantic_kind_to_enum(semantic_kind: str) -> int:
    value = semantic_kind.lower()
    if value == "model":
        return KIND_MESH
    if value == "image":
        return KIND_IMAGE
    if value == "audio":
        return KIND_AUDIO
    if value == "document":
        return KIND_DOCUMENT
    if value in {"archive", "link", "project_data", "auxiliary_data", "font"}:
        return KIND_OTHER
    return KIND_OTHER


RASTER_IMAGE_EXTENSIONS = {
    "png",
    "jpg",
    "jpeg",
    "bmp",
    "tga",
    "webp",
    "gif",
}


def parse_obj_to_mesh_payload(path: Path) -> tuple[bytes, int, int, int, int, tuple[float, float, float], tuple[float, float, float], float]:
    positions: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    uvs: list[tuple[float, float]] = []
    vertices: list[float] = []
    indices: list[int] = []

    min_x = float("inf")
    min_y = float("inf")
    min_z = float("inf")
    max_x = float("-inf")
    max_y = float("-inf")
    max_z = float("-inf")

    def to_zero_index(index: int, count: int) -> int:
        if index > 0:
            return index - 1
        if index < 0:
            return count + index
        return -1

    def emit_face_vertex(token: str) -> None:
        nonlocal min_x, min_y, min_z, max_x, max_y, max_z

        p_index = 0
        t_index = 0
        n_index = 0

        parts = token.split("/")
        if len(parts) >= 1 and parts[0]:
            p_index = int(parts[0])
        if len(parts) >= 2 and parts[1]:
            t_index = int(parts[1])
        if len(parts) >= 3 and parts[2]:
            n_index = int(parts[2])

        p_zero = to_zero_index(p_index, len(positions))
        if p_zero < 0 or p_zero >= len(positions):
            raise ValueError(f"OBJ position index out of range in {path}")

        px, py, pz = positions[p_zero]
        nx, ny, nz = (0.0, 1.0, 0.0)
        tu, tv = (0.0, 0.0)

        if n_index != 0:
            n_zero = to_zero_index(n_index, len(normals))
            if n_zero < 0 or n_zero >= len(normals):
                raise ValueError(f"OBJ normal index out of range in {path}")
            nx, ny, nz = normals[n_zero]

        if t_index != 0:
            t_zero = to_zero_index(t_index, len(uvs))
            if t_zero < 0 or t_zero >= len(uvs):
                raise ValueError(f"OBJ UV index out of range in {path}")
            tu, tv = uvs[t_zero]

        vertices.extend([px, py, pz, nx, ny, nz, tu, tv])
        indices.append(len(indices))
        min_x = min(min_x, px)
        min_y = min(min_y, py)
        min_z = min(min_z, pz)
        max_x = max(max_x, px)
        max_y = max(max_y, py)
        max_z = max(max_z, pz)

    with path.open("r", encoding="utf-8", errors="replace") as f:
        for raw_line in f:
            line = raw_line.strip()
            if (not line) or line.startswith("#"):
                continue

            parts = line.split()
            if not parts:
                continue

            tag = parts[0]
            if tag == "v" and len(parts) >= 4:
                positions.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif tag == "vn" and len(parts) >= 4:
                normals.append((float(parts[1]), float(parts[2]), float(parts[3])))
            elif tag == "vt" and len(parts) >= 3:
                u = float(parts[1])
                v = float(parts[2])
                uvs.append((u, 1.0 - v))
            elif tag == "f" and len(parts) >= 4:
                face_tokens = parts[1:]
                for tri in range(1, len(face_tokens) - 1):
                    emit_face_vertex(face_tokens[0])
                    emit_face_vertex(face_tokens[tri])
                    emit_face_vertex(face_tokens[tri + 1])

    if not vertices or not indices:
        raise ValueError(f"OBJ mesh produced no vertices/indices: {path}")

    vertex_array = array("f", vertices)
    index_array = array("I", indices)
    if os.sys.byteorder != "little":
        vertex_array.byteswap()
        index_array.byteswap()

    vertex_bytes = vertex_array.tobytes()
    index_bytes = index_array.tobytes()
    payload = vertex_bytes + index_bytes
    vertex_count = len(vertices) // 8
    index_count = len(indices)
    vertex_stride = 32
    index_offset = len(vertex_bytes)

    center_x = (min_x + max_x) * 0.5
    center_y = (min_y + max_y) * 0.5
    center_z = (min_z + max_z) * 0.5
    radius_sq = 0.0
    for i in range(0, len(vertices), 8):
        dx = vertices[i + 0] - center_x
        dy = vertices[i + 1] - center_y
        dz = vertices[i + 2] - center_z
        distance_sq = dx*dx + dy*dy + dz*dz
        if distance_sq > radius_sq:
            radius_sq = distance_sq
    radius = radius_sq ** 0.5

    bounds_min = (float(min_x), float(min_y), float(min_z))
    bounds_max = (float(max_x), float(max_y), float(max_z))
    return payload, vertex_count, index_count, vertex_stride, index_offset, bounds_min, bounds_max, float(radius)


def rgba8_mip_payload(path: Path) -> tuple[bytes, int, int, int]:
    image = Image.open(path).convert("RGBA")
    levels: list[tuple[int, int, bytes]] = []

    current = image
    while True:
        levels.append((current.width, current.height, current.tobytes()))
        if current.width == 1 and current.height == 1:
            break

        next_w = max(1, current.width // 2)
        next_h = max(1, current.height // 2)
        current = current.resize((next_w, next_h), Image.Resampling.LANCZOS)

    mip_count = len(levels)
    directory_size = 4 + (mip_count * 16)
    offset = directory_size
    entries = bytearray()
    payload_data = bytearray()
    for width, height, data in levels:
        size = len(data)
        entries.extend(struct.pack("<IIII", width, height, offset, size))
        payload_data.extend(data)
        offset += size

    payload = struct.pack("<I", mip_count) + bytes(entries) + bytes(payload_data)
    return payload, image.width, image.height, mip_count


def pcm16_payload(path: Path) -> tuple[bytes, int, int, int]:
    samples, sample_rate = sf.read(str(path), dtype="int16", always_2d=True)
    if samples.size == 0:
        raise ValueError(f"Audio produced no samples: {path}")

    data = np.ascontiguousarray(samples)
    frame_count = int(data.shape[0])
    channel_count = int(data.shape[1])
    payload = data.tobytes(order="C")
    return payload, int(sample_rate), channel_count, frame_count


def choose_family_aliases(rows: list[AssetRow]) -> dict[int, int]:
    model_rank = {"obj": 0, "glb": 1, "gltf": 2, "fbx": 3, "dae": 4, "stl": 5, "blend": 6, "3ds": 7, "skp": 8}
    image_rank = {"png": 0, "jpg": 1, "jpeg": 1, "svg": 2}
    audio_rank = {"ogg": 0}
    family_specs = (
        ("model", model_rank),
        ("image", image_rank),
        ("audio", audio_rank),
    )

    aliases: dict[int, int] = {}

    for family_name, rank_map in family_specs:
        grouped: dict[str, list[int]] = defaultdict(list)
        for idx, row in enumerate(rows):
            ext = Path(row.relative_path).suffix.lower().lstrip(".")
            if ext not in rank_map:
                continue
            stem_key = f"{family_name}|{Path(row.relative_path).with_suffix('').as_posix().lower()}"
            grouped[stem_key].append(idx)

        for indices in grouped.values():
            if len(indices) < 2:
                continue

            canonical = min(
                indices,
                key=lambda i: (
                    rank_map.get(Path(rows[i].relative_path).suffix.lower().lstrip("."), 1000),
                    rows[i].relative_path.lower(),
                ),
            )
            for idx in indices:
                if idx != canonical:
                    aliases[idx] = canonical

    return aliases


def build_native_payload(row: AssetRow, source_path: Path) -> tuple[int, int, int, int, int, int, bytes, int, tuple[int, int, int, int, int, int, int, int]]:
    ext = source_path.suffix.lower().lstrip(".")
    fallback_kind = semantic_kind_to_enum(row.semantic_kind)
    semantic_kind = row.semantic_kind.lower()
    aux_zero = (0, 0, 0, 0, 0, 0, 0, 0)

    try:
        if ext == "obj":
            payload, vertex_count, index_count, vertex_stride, index_offset, bounds_min, bounds_max, radius = parse_obj_to_mesh_payload(source_path)
            aux = (
                int(struct.unpack("<I", struct.pack("<f", bounds_min[0]))[0]),
                int(struct.unpack("<I", struct.pack("<f", bounds_min[1]))[0]),
                int(struct.unpack("<I", struct.pack("<f", bounds_min[2]))[0]),
                int(struct.unpack("<I", struct.pack("<f", bounds_max[0]))[0]),
                int(struct.unpack("<I", struct.pack("<f", bounds_max[1]))[0]),
                int(struct.unpack("<I", struct.pack("<f", bounds_max[2]))[0]),
                int(struct.unpack("<I", struct.pack("<f", radius))[0]),
                0,
            )
            return KIND_MESH, FORMAT_MESH_PNUV_F32_U32, vertex_count, index_count, vertex_stride, index_offset, payload, FLAG_HAS_BOUNDS, aux

        if ext in RASTER_IMAGE_EXTENSIONS:
            payload, width, height, mip_count = rgba8_mip_payload(source_path)
            return KIND_IMAGE, FORMAT_IMAGE_RGBA8_MIPS, width, height, mip_count, 4, payload, 0, aux_zero

        if semantic_kind == "audio":
            payload, sample_rate, channels, frame_count = pcm16_payload(source_path)
            return KIND_AUDIO, FORMAT_AUDIO_PCM16_INTERLEAVED, sample_rate, channels, frame_count, 16, payload, 0, aux_zero
    except Exception:
        raw = source_path.read_bytes()
        return fallback_kind, FORMAT_RAW_BYTES, 0, 0, 0, 0, raw, FLAG_CONVERSION_FAILED, aux_zero

    raw = source_path.read_bytes()
    return fallback_kind, FORMAT_RAW_BYTES, 0, 0, 0, 0, raw, 0, aux_zero


def should_attempt_compression(fmt_enum: int) -> bool:
    if fmt_enum == FORMAT_MESH_PNUV_F32_U32:
        return False
    return True


def maybe_compress_payload(fmt_enum: int, payload: bytes) -> tuple[int, bytes]:
    if not should_attempt_compression(fmt_enum):
        return CODEC_NONE, payload

    if len(payload) < 256:
        return CODEC_NONE, payload

    compressed = zlib.compress(payload, level=6)
    minimum_savings = max(64, len(payload) // 100)
    if len(compressed) + minimum_savings < len(payload):
        return CODEC_DEFLATE_ZLIB, compressed

    return CODEC_NONE, payload


def convert_row_for_pack(row: AssetRow, source_root: Path) -> ConversionResult:
    aux_zero = (0, 0, 0, 0, 0, 0, 0, 0)
    source_path = source_root / row.relative_path
    if not source_path.exists():
        missing_payload = b""
        missing_digest = hashlib.blake2b(missing_payload, digest_size=16).digest()
        return ConversionResult(
            kind_enum=KIND_OTHER,
            format_enum=FORMAT_RAW_BYTES,
            meta0=0,
            meta1=0,
            meta2=0,
            meta3=0,
            flags=FLAG_CONVERSION_FAILED,
            aux=aux_zero,
            digest=missing_digest,
            compression_codec=CODEC_NONE,
            stored_payload=missing_payload,
            decoded_size=0,
        )

    kind_enum, fmt_enum, meta0, meta1, meta2, meta3, payload, flags, aux = build_native_payload(row, source_path)
    digest = hashlib.blake2b(payload, digest_size=16).digest()
    compression_codec, stored_payload = maybe_compress_payload(fmt_enum, payload)
    return ConversionResult(
        kind_enum=kind_enum,
        format_enum=fmt_enum,
        meta0=meta0,
        meta1=meta1,
        meta2=meta2,
        meta3=meta3,
        flags=flags,
        aux=aux,
        digest=digest,
        compression_codec=compression_codec,
        stored_payload=stored_payload,
        decoded_size=len(payload),
    )


def apply_conversion_to_record(
    idx: int,
    record: BuildRecord,
    conversion: ConversionResult,
    payload_out,
    payload_offset: int,
    dedup: dict[
        tuple[int, bytes, int, int, int, int, tuple[int, int, int, int, int, int, int, int]],
        tuple[int, int, int, int, int, tuple[int, int, int, int, int, int, int, int], int],
    ],
) -> int:
    record.flags |= conversion.flags
    dedup_key = (
        conversion.format_enum,
        conversion.digest,
        conversion.meta0,
        conversion.meta1,
        conversion.meta2,
        conversion.meta3,
        conversion.aux,
    )

    if dedup_key in dedup:
        canonical_index, canonical_offset, canonical_size, canonical_codec, canonical_decoded_size, canonical_aux, canonical_flags = dedup[dedup_key]
        record.flags |= FLAG_ALIAS
        record.flags |= (canonical_flags & FLAG_HAS_BOUNDS)
        record.alias_index = canonical_index
        record.payload_offset = canonical_offset
        record.payload_size = canonical_size
        record.compression_codec = canonical_codec
        record.decoded_size = canonical_decoded_size
        record.aux0 = canonical_aux[0]
        record.aux1 = canonical_aux[1]
        record.aux2 = canonical_aux[2]
        record.aux3 = canonical_aux[3]
        record.aux4 = canonical_aux[4]
        record.aux5 = canonical_aux[5]
        record.aux6 = canonical_aux[6]
        record.aux7 = canonical_aux[7]
        return payload_offset

    record.kind_enum = conversion.kind_enum
    record.format_enum = conversion.format_enum
    record.meta0 = conversion.meta0
    record.meta1 = conversion.meta1
    record.meta2 = conversion.meta2
    record.meta3 = conversion.meta3
    record.compression_codec = conversion.compression_codec
    record.aux0 = conversion.aux[0]
    record.aux1 = conversion.aux[1]
    record.aux2 = conversion.aux[2]
    record.aux3 = conversion.aux[3]
    record.aux4 = conversion.aux[4]
    record.aux5 = conversion.aux[5]
    record.aux6 = conversion.aux[6]
    record.aux7 = conversion.aux[7]
    record.payload_offset = payload_offset
    record.payload_size = len(conversion.stored_payload)
    record.decoded_size = conversion.decoded_size
    payload_out.write(conversion.stored_payload)

    dedup[dedup_key] = (
        idx,
        payload_offset,
        len(conversion.stored_payload),
        conversion.compression_codec,
        conversion.decoded_size,
        conversion.aux,
        record.flags,
    )
    return payload_offset + len(conversion.stored_payload)


def generate_header(rows: list[AssetRow], out_header: Path, pack_size: int) -> None:
    out_header.parent.mkdir(parents=True, exist_ok=True)

    grouped: dict[str, dict[str, list[tuple[str, int]]]] = defaultdict(lambda: defaultdict(list))
    per_ns_counts: dict[tuple[str, str], dict[str, int]] = defaultdict(lambda: defaultdict(int))

    for index, row in enumerate(rows):
        top_level, pack, leaf = parse_pack_path(row.relative_path)
        top_key = sanitize_identifier(top_level)
        pack_key = sanitize_identifier(pack)
        symbol_base = sanitize_identifier(leaf)

        count_map = per_ns_counts[(top_key, pack_key)]
        count_map[symbol_base] += 1
        suffix = count_map[symbol_base]
        symbol = symbol_base if suffix == 1 else f"{symbol_base}_{suffix}"

        grouped[top_key][pack_key].append((symbol, index))

    with out_header.open("w", encoding="utf-8") as f:
        f.write("#pragma once\n\n")
        f.write("#include <array>\n")
        f.write("#include <bit>\n")
        f.write("#include <cstddef>\n")
        f.write("#include <cstdint>\n")
        f.write("#include <span>\n")
        f.write("#include <string_view>\n\n")

        f.write("namespace manifest\n")
        f.write("{\n")
        f.write("inline constexpr std::uint32_t kPackMagic = 0x4B504247u;\n")
        f.write(f"inline constexpr std::uint32_t kPackVersion = {PACK_VERSION}u;\n")
        f.write("inline constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;\n\n")

        f.write("enum class AssetKind : std::uint32_t\n")
        f.write("{\n")
        f.write("    RAW = 0,\n")
        f.write("    MESH = 1,\n")
        f.write("    IMAGE = 2,\n")
        f.write("    AUDIO = 3,\n")
        f.write("    DOCUMENT = 4,\n")
        f.write("    OTHER = 5,\n")
        f.write("};\n\n")

        f.write("enum class AssetFormat : std::uint32_t\n")
        f.write("{\n")
        f.write("    RAW_BYTES = 0,\n")
        f.write("    MESH_PNUV_F32_U32 = 1,\n")
        f.write("    IMAGE_RGBA8_MIPS = 2,\n")
        f.write("    AUDIO_PCM16_INTERLEAVED = 3,\n")
        f.write("};\n\n")

        f.write("enum class CompressionCodec : std::uint32_t\n")
        f.write("{\n")
        f.write("    NONE = 0,\n")
        f.write("    DEFLATE_ZLIB = 1,\n")
        f.write("};\n\n")

        f.write("enum AssetFlags : std::uint32_t\n")
        f.write("{\n")
        f.write("    ASSET_FLAG_ALIAS = 1u << 0,\n")
        f.write("    ASSET_FLAG_CONVERSION_FAILED = 1u << 1,\n")
        f.write("    ASSET_FLAG_HAS_BOUNDS = 1u << 2,\n")
        f.write("};\n\n")

        f.write("#pragma pack(push, 1)\n")
        f.write("struct PackHeader\n")
        f.write("{\n")
        f.write("    std::uint32_t magic;\n")
        f.write("    std::uint32_t version;\n")
        f.write("    std::uint32_t flags;\n")
        f.write("    std::uint32_t assetCount;\n")
        f.write("    std::uint32_t reserved;\n")
        f.write("    std::uint64_t stringTableOffset;\n")
        f.write("    std::uint64_t stringTableSize;\n")
        f.write("    std::uint64_t assetTableOffset;\n")
        f.write("    std::uint64_t assetTableSize;\n")
        f.write("    std::uint64_t payloadOffset;\n")
        f.write("    std::uint64_t payloadSize;\n")
        f.write("};\n\n")

        f.write("struct StringRef\n")
        f.write("{\n")
        f.write("    std::uint32_t offset;\n")
        f.write("    std::uint32_t length;\n")
        f.write("};\n\n")

        f.write("struct AssetRecord\n")
        f.write("{\n")
        f.write("    StringRef name;\n")
        f.write("    StringRef relativePath;\n")
        f.write("    StringRef semanticKind;\n")
        f.write("    StringRef contentRole;\n")
        f.write("    StringRef engineHint;\n")
        f.write("    StringRef semanticTags;\n")
        f.write("    std::uint32_t kind;\n")
        f.write("    std::uint32_t format;\n")
        f.write("    std::uint32_t flags;\n")
        f.write("    std::uint32_t aliasIndex;\n")
        f.write("    std::uint32_t meta0;\n")
        f.write("    std::uint32_t meta1;\n")
        f.write("    std::uint32_t meta2;\n")
        f.write("    std::uint32_t meta3;\n")
        f.write("    std::uint32_t compression;\n")
        f.write("    std::uint32_t aux0;\n")
        f.write("    std::uint32_t aux1;\n")
        f.write("    std::uint32_t aux2;\n")
        f.write("    std::uint32_t aux3;\n")
        f.write("    std::uint32_t aux4;\n")
        f.write("    std::uint32_t aux5;\n")
        f.write("    std::uint32_t aux6;\n")
        f.write("    std::uint32_t aux7;\n")
        f.write("    std::uint64_t payloadOffset;\n")
        f.write("    std::uint64_t payloadSize;\n")
        f.write("    std::uint64_t decodedSize;\n")
        f.write("};\n")
        f.write("#pragma pack(pop)\n\n")

        f.write("struct ResolvedAsset\n")
        f.write("{\n")
        f.write("    bool valid;\n")
        f.write("    std::uint32_t index;\n")
        f.write("    std::string_view name;\n")
        f.write("    std::string_view relativePath;\n")
        f.write("    std::string_view semanticKind;\n")
        f.write("    std::string_view contentRole;\n")
        f.write("    std::string_view engineHint;\n")
        f.write("    std::string_view semanticTags;\n")
        f.write("    AssetKind kind;\n")
        f.write("    AssetFormat format;\n")
        f.write("    std::uint32_t flags;\n")
        f.write("    std::uint32_t aliasIndex;\n")
        f.write("    std::uint32_t meta0;\n")
        f.write("    std::uint32_t meta1;\n")
        f.write("    std::uint32_t meta2;\n")
        f.write("    std::uint32_t meta3;\n")
        f.write("    CompressionCodec compression;\n")
        f.write("    std::uint32_t aux0;\n")
        f.write("    std::uint32_t aux1;\n")
        f.write("    std::uint32_t aux2;\n")
        f.write("    std::uint32_t aux3;\n")
        f.write("    std::uint32_t aux4;\n")
        f.write("    std::uint32_t aux5;\n")
        f.write("    std::uint32_t aux6;\n")
        f.write("    std::uint32_t aux7;\n")
        f.write("    std::uint64_t decodedSize;\n")
        f.write("    std::span<const std::byte> payload;\n")
        f.write("};\n\n")

        f.write("struct MeshBounds\n")
        f.write("{\n")
        f.write("    bool valid;\n")
        f.write("    float minX;\n")
        f.write("    float minY;\n")
        f.write("    float minZ;\n")
        f.write("    float maxX;\n")
        f.write("    float maxY;\n")
        f.write("    float maxZ;\n")
        f.write("    float radius;\n")
        f.write("};\n\n")

        f.write("inline auto TryGetHeader(std::span<const std::byte> pack) -> const PackHeader *\n")
        f.write("{\n")
        f.write("    if (pack.size() < sizeof(PackHeader))\n")
        f.write("    {\n")
        f.write("        return nullptr;\n")
        f.write("    }\n")
        f.write("    const auto *header = reinterpret_cast<const PackHeader *>(pack.data());\n")
        f.write("    if ((header->magic != kPackMagic) || (header->version != kPackVersion))\n")
        f.write("    {\n")
        f.write("        return nullptr;\n")
        f.write("    }\n")
        f.write("    if ((header->stringTableOffset + header->stringTableSize) > pack.size())\n")
        f.write("    {\n")
        f.write("        return nullptr;\n")
        f.write("    }\n")
        f.write("    if ((header->assetTableOffset + header->assetTableSize) > pack.size())\n")
        f.write("    {\n")
        f.write("        return nullptr;\n")
        f.write("    }\n")
        f.write("    if ((header->payloadOffset + header->payloadSize) > pack.size())\n")
        f.write("    {\n")
        f.write("        return nullptr;\n")
        f.write("    }\n")
        f.write("    return header;\n")
        f.write("}\n\n")

        f.write("inline auto TryGetAssetTable(std::span<const std::byte> pack, const PackHeader &header) -> std::span<const AssetRecord>\n")
        f.write("{\n")
        f.write("    if ((header.assetTableSize % sizeof(AssetRecord)) != 0)\n")
        f.write("    {\n")
        f.write("        return {};\n")
        f.write("    }\n")
        f.write("    std::size_t count = header.assetTableSize / sizeof(AssetRecord);\n")
        f.write("    if (count != header.assetCount)\n")
        f.write("    {\n")
        f.write("        return {};\n")
        f.write("    }\n")
        f.write("    const auto *records = reinterpret_cast<const AssetRecord *>(pack.data() + header.assetTableOffset);\n")
        f.write("    return {records, count};\n")
        f.write("}\n\n")

        f.write("inline auto TryResolveString(std::span<const std::byte> pack, const PackHeader &header, StringRef ref) -> std::string_view\n")
        f.write("{\n")
        f.write("    std::size_t begin = header.stringTableOffset + static_cast<std::size_t>(ref.offset);\n")
        f.write("    std::size_t end = begin + static_cast<std::size_t>(ref.length);\n")
        f.write("    std::size_t tableEnd = header.stringTableOffset + header.stringTableSize;\n")
        f.write("    if ((begin >= tableEnd) || (end > tableEnd))\n")
        f.write("    {\n")
        f.write("        return {};\n")
        f.write("    }\n")
        f.write("    const char *chars = reinterpret_cast<const char *>(pack.data());\n")
        f.write("    return {chars + begin, ref.length};\n")
        f.write("}\n\n")

        f.write("inline auto TryResolvePayload(std::span<const std::byte> pack, const PackHeader &header, const AssetRecord &record) -> std::span<const std::byte>\n")
        f.write("{\n")
        f.write("    std::size_t begin = header.payloadOffset + static_cast<std::size_t>(record.payloadOffset);\n")
        f.write("    std::size_t end = begin + static_cast<std::size_t>(record.payloadSize);\n")
        f.write("    std::size_t payloadEnd = header.payloadOffset + header.payloadSize;\n")
        f.write("    if ((begin >= payloadEnd) || (end > payloadEnd))\n")
        f.write("    {\n")
        f.write("        return {};\n")
        f.write("    }\n")
        f.write("    return {pack.data() + begin, static_cast<std::size_t>(record.payloadSize)};\n")
        f.write("}\n\n")

        f.write("inline auto DecodeF32(std::uint32_t bits) -> float\n")
        f.write("{\n")
        f.write("    return std::bit_cast<float>(bits);\n")
        f.write("}\n\n")

        f.write("inline auto ResolveAsset(std::span<const std::byte> pack, std::uint32_t index) -> ResolvedAsset\n")
        f.write("{\n")
        f.write("    ResolvedAsset result = {};\n")
        f.write("    result.valid = false;\n")
        f.write("    result.index = index;\n")
        f.write("    result.kind = AssetKind::RAW;\n")
        f.write("    result.format = AssetFormat::RAW_BYTES;\n")
        f.write("\n")
        f.write("    const PackHeader *header = TryGetHeader(pack);\n")
        f.write("    if (header == nullptr)\n")
        f.write("    {\n")
        f.write("        return result;\n")
        f.write("    }\n")
        f.write("\n")
        f.write("    std::span<const AssetRecord> records = TryGetAssetTable(pack, *header);\n")
        f.write("    if ((records.empty()) || (index >= records.size()))\n")
        f.write("    {\n")
        f.write("        return result;\n")
        f.write("    }\n")
        f.write("\n")
        f.write("    const AssetRecord &record = records[index];\n")
        f.write("    result.name = TryResolveString(pack, *header, record.name);\n")
        f.write("    result.relativePath = TryResolveString(pack, *header, record.relativePath);\n")
        f.write("    result.semanticKind = TryResolveString(pack, *header, record.semanticKind);\n")
        f.write("    result.contentRole = TryResolveString(pack, *header, record.contentRole);\n")
        f.write("    result.engineHint = TryResolveString(pack, *header, record.engineHint);\n")
        f.write("    result.semanticTags = TryResolveString(pack, *header, record.semanticTags);\n")
        f.write("    result.kind = static_cast<AssetKind>(record.kind);\n")
        f.write("    result.format = static_cast<AssetFormat>(record.format);\n")
        f.write("    result.flags = record.flags;\n")
        f.write("    result.aliasIndex = record.aliasIndex;\n")
        f.write("    result.meta0 = record.meta0;\n")
        f.write("    result.meta1 = record.meta1;\n")
        f.write("    result.meta2 = record.meta2;\n")
        f.write("    result.meta3 = record.meta3;\n")
        f.write("    result.compression = static_cast<CompressionCodec>(record.compression);\n")
        f.write("    result.aux0 = record.aux0;\n")
        f.write("    result.aux1 = record.aux1;\n")
        f.write("    result.aux2 = record.aux2;\n")
        f.write("    result.aux3 = record.aux3;\n")
        f.write("    result.aux4 = record.aux4;\n")
        f.write("    result.aux5 = record.aux5;\n")
        f.write("    result.aux6 = record.aux6;\n")
        f.write("    result.aux7 = record.aux7;\n")
        f.write("    result.decodedSize = record.decodedSize;\n")
        f.write("    result.payload = TryResolvePayload(pack, *header, record);\n")
        f.write("    result.valid = !result.payload.empty();\n")
        f.write("    return result;\n")
        f.write("}\n\n")

        f.write("inline auto TryGetMeshBounds(const ResolvedAsset &asset) -> MeshBounds\n")
        f.write("{\n")
        f.write("    MeshBounds bounds = {};\n")
        f.write("    bounds.valid = false;\n")
        f.write("    if (!asset.valid)\n")
        f.write("    {\n")
        f.write("        return bounds;\n")
        f.write("    }\n")
        f.write("    if (asset.format != AssetFormat::MESH_PNUV_F32_U32)\n")
        f.write("    {\n")
        f.write("        return bounds;\n")
        f.write("    }\n")
        f.write("    if ((asset.flags & ASSET_FLAG_HAS_BOUNDS) == 0u)\n")
        f.write("    {\n")
        f.write("        return bounds;\n")
        f.write("    }\n")
        f.write("    bounds.valid = true;\n")
        f.write("    bounds.minX = DecodeF32(asset.aux0);\n")
        f.write("    bounds.minY = DecodeF32(asset.aux1);\n")
        f.write("    bounds.minZ = DecodeF32(asset.aux2);\n")
        f.write("    bounds.maxX = DecodeF32(asset.aux3);\n")
        f.write("    bounds.maxY = DecodeF32(asset.aux4);\n")
        f.write("    bounds.maxZ = DecodeF32(asset.aux5);\n")
        f.write("    bounds.radius = DecodeF32(asset.aux6);\n")
        f.write("    return bounds;\n")
        f.write("}\n\n")

        f.write("struct AssetHandle\n")
        f.write("{\n")
        f.write("    std::uint32_t index;\n")
        f.write("    inline auto Resolve(std::span<const std::byte> pack) const -> ResolvedAsset\n")
        f.write("    {\n")
        f.write("        return ResolveAsset(pack, index);\n")
        f.write("    }\n")
        f.write("};\n")
        f.write("}\n\n")

        f.write("namespace manifest::kenney\n")
        f.write("{\n")
        f.write(f"inline constexpr std::uint32_t kAssetCount = {len(rows)}u;\n")
        f.write(f"inline constexpr std::uint64_t kPackSizeBytes = {pack_size}ull;\n\n")
        f.write("namespace handles\n")
        f.write("{\n")
        for top_key in sorted(grouped.keys()):
            f.write(f"namespace {top_key}\n")
            f.write("{\n")
            for pack_key in sorted(grouped[top_key].keys()):
                f.write(f"namespace {pack_key}\n")
                f.write("{\n")
                for symbol, index in grouped[top_key][pack_key]:
                    f.write(f"inline constexpr manifest::AssetHandle {symbol} = {{ {index}u }};\n")
                f.write("}\n")
            f.write("}\n")
        f.write("}\n")
        f.write("}\n")


def build_pack(
    rows: list[AssetRow],
    source_root: Path,
    blob_path: Path,
    workers: int = 1,
    max_inflight: int | None = None,
) -> tuple[list[BuildRecord], int]:
    source_root = source_root.resolve()
    string_builder = StringTableBuilder()
    records: list[BuildRecord] = []

    preferred_aliases = choose_family_aliases(rows)
    worker_count = max(1, workers)
    inflight_limit = max_inflight if max_inflight is not None else (worker_count * 2)
    inflight_limit = max(1, inflight_limit)

    payload_tmp = tempfile.NamedTemporaryFile(prefix="kenney_payload_", suffix=".bin", delete=False)
    payload_tmp_path = Path(payload_tmp.name)
    payload_tmp.close()

    try:
        payload_offset = 0
        dedup: dict[tuple[int, bytes, int, int, int, int, tuple[int, int, int, int, int, int, int, int]], tuple[int, int, int, int, int, tuple[int, int, int, int, int, int, int, int], int]] = {}
        row_progress = ProgressBar("pack:records", len(rows))

        with payload_tmp_path.open("wb") as payload_out:
            if worker_count == 1:
                for idx, row in enumerate(rows):
                    name_ref = string_builder.intern(row.name)
                    path_ref = string_builder.intern(row.relative_path)
                    kind_ref = string_builder.intern(row.semantic_kind)
                    role_ref = string_builder.intern(row.content_role)
                    engine_ref = string_builder.intern(row.engine_hint)
                    tags_ref = string_builder.intern(row.semantic_tags)
                    record = BuildRecord(
                        name_ref=name_ref,
                        path_ref=path_ref,
                        kind_ref=kind_ref,
                        role_ref=role_ref,
                        engine_ref=engine_ref,
                        tags_ref=tags_ref,
                    )
                    records.append(record)

                    if idx in preferred_aliases:
                        record.flags |= FLAG_ALIAS
                        record.alias_index = preferred_aliases[idx]
                    else:
                        conversion = convert_row_for_pack(row, source_root)
                        payload_offset = apply_conversion_to_record(
                            idx=idx,
                            record=record,
                            conversion=conversion,
                            payload_out=payload_out,
                            payload_offset=payload_offset,
                            dedup=dedup,
                        )

                    if ((idx + 1) % 128) == 0 or (idx + 1) == len(rows):
                        row_progress.update(idx + 1)
            else:
                pending: dict[int, Future[ConversionResult]] = {}
                next_to_finalize = 0

                def flush_next_record() -> None:
                    nonlocal next_to_finalize, payload_offset
                    while next_to_finalize < len(records):
                        if next_to_finalize in preferred_aliases:
                            next_to_finalize += 1
                            if (next_to_finalize % 128) == 0 or next_to_finalize == len(rows):
                                row_progress.update(next_to_finalize)
                            continue

                        future = pending.pop(next_to_finalize, None)
                        if future is None:
                            raise RuntimeError(f"Missing conversion future for row index {next_to_finalize}")

                        conversion = future.result()
                        payload_offset = apply_conversion_to_record(
                            idx=next_to_finalize,
                            record=records[next_to_finalize],
                            conversion=conversion,
                            payload_out=payload_out,
                            payload_offset=payload_offset,
                            dedup=dedup,
                        )
                        next_to_finalize += 1
                        if (next_to_finalize % 128) == 0 or next_to_finalize == len(rows):
                            row_progress.update(next_to_finalize)
                        break

                with ThreadPoolExecutor(max_workers=worker_count) as executor:
                    for idx, row in enumerate(rows):
                        name_ref = string_builder.intern(row.name)
                        path_ref = string_builder.intern(row.relative_path)
                        kind_ref = string_builder.intern(row.semantic_kind)
                        role_ref = string_builder.intern(row.content_role)
                        engine_ref = string_builder.intern(row.engine_hint)
                        tags_ref = string_builder.intern(row.semantic_tags)
                        record = BuildRecord(
                            name_ref=name_ref,
                            path_ref=path_ref,
                            kind_ref=kind_ref,
                            role_ref=role_ref,
                            engine_ref=engine_ref,
                            tags_ref=tags_ref,
                        )
                        records.append(record)

                        if idx in preferred_aliases:
                            record.flags |= FLAG_ALIAS
                            record.alias_index = preferred_aliases[idx]
                        else:
                            pending[idx] = executor.submit(convert_row_for_pack, row, source_root)

                        while len(pending) >= inflight_limit:
                            flush_next_record()

                    while next_to_finalize < len(rows):
                        flush_next_record()

        row_progress.finish()

        def resolve_alias_root(index: int) -> int:
            visited: set[int] = set()
            current = index
            while current != INVALID_INDEX:
                if current in visited:
                    return index
                visited.add(current)
                next_index = records[current].alias_index
                if next_index == INVALID_INDEX:
                    return current
                current = next_index
            return index

        alias_progress = ProgressBar("pack:aliases", len(records))
        for idx, record in enumerate(records):
            if record.alias_index == INVALID_INDEX:
                if ((idx + 1) % 512) == 0 or (idx + 1) == len(records):
                    alias_progress.update(idx + 1)
                continue

            root = resolve_alias_root(record.alias_index)
            if root == idx:
                if ((idx + 1) % 512) == 0 or (idx + 1) == len(records):
                    alias_progress.update(idx + 1)
                continue
            target = records[root]
            record.alias_index = root
            record.kind_enum = target.kind_enum
            record.format_enum = target.format_enum
            record.meta0 = target.meta0
            record.meta1 = target.meta1
            record.meta2 = target.meta2
            record.meta3 = target.meta3
            record.compression_codec = target.compression_codec
            record.aux0 = target.aux0
            record.aux1 = target.aux1
            record.aux2 = target.aux2
            record.aux3 = target.aux3
            record.aux4 = target.aux4
            record.aux5 = target.aux5
            record.aux6 = target.aux6
            record.aux7 = target.aux7
            record.flags |= (target.flags & FLAG_HAS_BOUNDS)
            record.payload_offset = target.payload_offset
            record.payload_size = target.payload_size
            record.decoded_size = target.decoded_size
            if ((idx + 1) % 512) == 0 or (idx + 1) == len(records):
                alias_progress.update(idx + 1)
        alias_progress.finish()

        string_table = string_builder.bytes
        asset_table_size = len(records) * RECORD_STRUCT.size
        header_size = HEADER_STRUCT.size
        string_offset = header_size
        asset_offset = string_offset + len(string_table)
        payload_start = asset_offset + asset_table_size

        payload_size = payload_tmp_path.stat().st_size

        blob_path.parent.mkdir(parents=True, exist_ok=True)
        write_progress = ProgressBar("pack:write", int(payload_size))
        with blob_path.open("wb") as out, payload_tmp_path.open("rb") as payload_in:
            header_bytes = HEADER_STRUCT.pack(
                PACK_MAGIC,
                PACK_VERSION,
                0,
                len(records),
                0,
                string_offset,
                len(string_table),
                asset_offset,
                asset_table_size,
                payload_start,
                payload_size,
            )
            out.write(header_bytes)
            out.write(string_table)

            for record in records:
                out.write(
                    RECORD_STRUCT.pack(
                        record.name_ref.offset,
                        record.name_ref.length,
                        record.path_ref.offset,
                        record.path_ref.length,
                        record.kind_ref.offset,
                        record.kind_ref.length,
                        record.role_ref.offset,
                        record.role_ref.length,
                        record.engine_ref.offset,
                        record.engine_ref.length,
                        record.tags_ref.offset,
                        record.tags_ref.length,
                        record.kind_enum,
                        record.format_enum,
                        record.flags,
                        record.alias_index,
                        record.meta0,
                        record.meta1,
                        record.meta2,
                        record.meta3,
                        record.compression_codec,
                        record.aux0,
                        record.aux1,
                        record.aux2,
                        record.aux3,
                        record.aux4,
                        record.aux5,
                        record.aux6,
                        record.aux7,
                        record.payload_offset,
                        record.payload_size,
                        record.decoded_size,
                    )
                )

            bytes_written = 0
            while True:
                chunk = payload_in.read(1024 * 1024)
                if not chunk:
                    break
                out.write(chunk)
                bytes_written += len(chunk)
                write_progress.update(bytes_written)
        write_progress.finish()

        return records, blob_path.stat().st_size
    finally:
        if payload_tmp_path.exists():
            payload_tmp_path.unlink()


def main() -> None:
    default_workers = max(1, min(8, (os.cpu_count() or 1)))
    parser = argparse.ArgumentParser(description="Generate binary asset pack + manifest header")
    parser.add_argument("--input", required=True, type=Path, help="Input TSV manifest")
    parser.add_argument("--source-root", required=True, type=Path, help="Root directory that contains relative asset paths")
    parser.add_argument("--header", required=True, type=Path, help="Output manifest header")
    parser.add_argument("--blob", required=True, type=Path, help="Output binary pack blob")
    parser.add_argument("--workers", type=int, default=default_workers, help=f"Conversion worker threads (default: {default_workers})")
    parser.add_argument("--inflight", type=int, default=default_workers * 2, help="Maximum in-flight converted payloads before blocking")
    args = parser.parse_args()

    rows = read_rows(args.input)
    records, pack_size = build_pack(rows, args.source_root, args.blob, workers=args.workers, max_inflight=args.inflight)
    generate_header(rows, args.header, pack_size)

    alias_count = sum(1 for r in records if r.alias_index != INVALID_INDEX)
    mesh_count = sum(1 for r in records if r.format_enum == FORMAT_MESH_PNUV_F32_U32)
    image_count = sum(1 for r in records if r.format_enum == FORMAT_IMAGE_RGBA8_MIPS)
    audio_count = sum(1 for r in records if r.format_enum == FORMAT_AUDIO_PCM16_INTERLEAVED)
    raw_count = sum(1 for r in records if r.format_enum == FORMAT_RAW_BYTES)
    failed_count = sum(1 for r in records if (r.flags & FLAG_CONVERSION_FAILED) != 0)
    compressed_count = sum(1 for r in records if r.compression_codec != CODEC_NONE)
    bounds_count = sum(1 for r in records if (r.flags & FLAG_HAS_BOUNDS) != 0)

    print(f"generated header: {args.header}")
    print(f"generated pack  : {args.blob}")
    print(f"asset count     : {len(records)}")
    print(f"pack bytes      : {pack_size}")
    print(f"alias count     : {alias_count}")
    print(f"mesh records    : {mesh_count}")
    print(f"image records   : {image_count}")
    print(f"audio records   : {audio_count}")
    print(f"raw records     : {raw_count}")
    print(f"compressed      : {compressed_count}")
    print(f"with bounds     : {bounds_count}")
    print(f"failed convert  : {failed_count}")


if __name__ == "__main__":
    main()
