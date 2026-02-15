#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import html
import math
import statistics
from collections import Counter
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_BUILD_DIR = REPO_ROOT / "cmake-build-release"
DEFAULT_HITCH_TRACE = DEFAULT_BUILD_DIR / "hitch_trace.csv"
DEFAULT_FRAME_STATS = DEFAULT_BUILD_DIR / "frame_stats.csv"
DEFAULT_OUTPUT = DEFAULT_BUILD_DIR / "trace_report.html"


def parse_float(value: str) -> float:
    text = value.strip()
    if text == "":
        return math.nan
    lowered = text.lower()
    if lowered in {"nan", "inf", "+inf", "-inf"}:
        try:
            return float(lowered)
        except ValueError:
            return math.nan
    try:
        return float(text)
    except ValueError:
        return math.nan


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return [dict(row) for row in csv.DictReader(handle)]


def summarize_numeric(values: list[float]) -> dict[str, float]:
    finite = [value for value in values if math.isfinite(value)]
    if not finite:
        return {"count": 0, "min": math.nan, "mean": math.nan, "max": math.nan}
    return {
        "count": float(len(finite)),
        "min": min(finite),
        "mean": statistics.mean(finite),
        "max": max(finite),
    }


def format_float(value: float, digits: int = 3) -> str:
    if not math.isfinite(value):
        return "nan"
    return f"{value:.{digits}f}"


def svg_line_chart(
    title: str,
    x_values: list[float],
    series: list[tuple[str, list[float], str]],
    width: int = 980,
    height: int = 260,
) -> str:
    valid_x = [x for x in x_values if math.isfinite(x)]
    if not valid_x:
        return f"<h3>{html.escape(title)}</h3><p>No data.</p>"

    x_min = min(valid_x)
    x_max = max(valid_x)
    if x_max <= x_min:
        x_max = x_min + 1.0

    all_y: list[float] = []
    for _, values, _ in series:
        all_y.extend(value for value in values if math.isfinite(value))
    if not all_y:
        return f"<h3>{html.escape(title)}</h3><p>No finite Y values.</p>"

    y_min = min(all_y)
    y_max = max(all_y)
    if y_max <= y_min:
        y_max = y_min + 1.0

    margin_left = 56
    margin_right = 22
    margin_top = 20
    margin_bottom = 34
    plot_width = width - margin_left - margin_right
    plot_height = height - margin_top - margin_bottom

    def sx(x: float) -> float:
        ratio = (x - x_min) / (x_max - x_min)
        return margin_left + (ratio * plot_width)

    def sy(y: float) -> float:
        ratio = (y - y_min) / (y_max - y_min)
        return margin_top + ((1.0 - ratio) * plot_height)

    lines: list[str] = []
    lines.append(f"<h3>{html.escape(title)}</h3>")
    lines.append(f"<svg viewBox='0 0 {width} {height}' width='{width}' height='{height}'>")
    lines.append(f"<rect x='0' y='0' width='{width}' height='{height}' fill='#10141a' rx='8'/>")
    lines.append(
        f"<line x1='{margin_left}' y1='{height - margin_bottom}' x2='{width - margin_right}' y2='{height - margin_bottom}' stroke='#4e5b6a' stroke-width='1'/>"
    )
    lines.append(
        f"<line x1='{margin_left}' y1='{margin_top}' x2='{margin_left}' y2='{height - margin_bottom}' stroke='#4e5b6a' stroke-width='1'/>"
    )

    for step in range(1, 5):
        y = margin_top + (step * plot_height / 5.0)
        lines.append(
            f"<line x1='{margin_left}' y1='{y:.2f}' x2='{width - margin_right}' y2='{y:.2f}' stroke='#202935' stroke-width='1'/>"
        )

    for name, values, color in series:
        points: list[str] = []
        for x, y in zip(x_values, values):
            if not (math.isfinite(x) and math.isfinite(y)):
                continue
            points.append(f"{sx(x):.2f},{sy(y):.2f}")
        if points:
            lines.append(
                f"<polyline fill='none' stroke='{html.escape(color)}' stroke-width='1.8' points='{' '.join(points)}'/>"
            )
        lines.append(
            f"<text x='{margin_left + 6}' y='{margin_top + 14 + (16 * len(lines) % 48)}' fill='{html.escape(color)}' font-size='12'>{html.escape(name)}</text>"
        )

    lines.append(
        f"<text x='{margin_left}' y='{height - 8}' fill='#9cb2cc' font-size='12'>x: wall_s [{format_float(x_min, 2)} .. {format_float(x_max, 2)}]</text>"
    )
    lines.append(
        f"<text x='{margin_left}' y='{margin_top - 4}' fill='#9cb2cc' font-size='12'>y: ms [{format_float(y_min, 3)} .. {format_float(y_max, 3)}]</text>"
    )
    lines.append("</svg>")
    return "\n".join(lines)


def build_html_report(
    frame_stats_path: Path,
    hitch_trace_path: Path,
    frame_stats_rows: list[dict[str, str]],
    hitch_rows: list[dict[str, str]],
) -> str:
    frame_avg_fps = [parse_float(row.get("avg_fps", "")) for row in frame_stats_rows]
    frame_avg_ms = [parse_float(row.get("avg_ms", "")) for row in frame_stats_rows]
    frame_p95_ms = [parse_float(row.get("p95_ms", "")) for row in frame_stats_rows]
    frame_p99_ms = [parse_float(row.get("p99_ms", "")) for row in frame_stats_rows]
    frame_gpu_ms = [parse_float(row.get("gpu_total_avg_ms", "")) for row in frame_stats_rows]
    frame_wall_s = [parse_float(row.get("wall_s", "")) for row in frame_stats_rows]

    frame_events = [row for row in hitch_rows if row.get("event", "") == "frame"]
    hitch_wall_s = [parse_float(row.get("wall_s", "")) for row in frame_events]
    hitch_frame_ms = [parse_float(row.get("frame_ms", "")) for row in frame_events]
    hitch_work_ms = [parse_float(row.get("frame_work_ms", "")) for row in frame_events]
    hitch_acq_ms = [parse_float(row.get("acq_ms", "")) for row in frame_events]
    hitch_submit_q_ms = [parse_float(row.get("queue_submit_ms", "")) for row in frame_events]
    hitch_gpu_total_ms = [parse_float(row.get("gpu_total_ms", "")) for row in frame_events]

    event_counts = Counter(row.get("event", "") for row in hitch_rows)
    trigger_counts = Counter(int(row.get("trigger_mask", "0") or "0") for row in frame_events)

    top_frame_events = sorted(
        frame_events,
        key=lambda row: parse_float(row.get("frame_work_ms", "")),
        reverse=True,
    )[:12]

    stats_avg_fps = summarize_numeric(frame_avg_fps)
    stats_avg_ms = summarize_numeric(frame_avg_ms)
    stats_gpu_ms = summarize_numeric(frame_gpu_ms)
    stats_hitch_work_ms = summarize_numeric(hitch_work_ms)

    blocks: list[str] = []
    blocks.append("<!doctype html>")
    blocks.append("<html lang='en'><head><meta charset='utf-8'/>")
    blocks.append("<meta name='viewport' content='width=device-width, initial-scale=1'/>")
    blocks.append("<title>Greatbadbeyond Trace Report</title>")
    blocks.append(
        "<style>"
        "body{font-family:Consolas,Menlo,monospace;background:#0b0f14;color:#d9e5f2;margin:0;padding:16px;}"
        "h1,h2,h3{margin:0 0 10px 0;}"
        ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px;margin:12px 0 18px 0;}"
        ".card{background:#121924;border:1px solid #263447;border-radius:10px;padding:10px 12px;}"
        "table{border-collapse:collapse;width:100%;font-size:13px;}"
        "th,td{border:1px solid #2a3a4e;padding:6px 8px;text-align:left;}"
        "th{background:#182435;}"
        ".muted{color:#9cb2cc;}"
        ".section{margin-top:16px;}"
        "</style></head><body>"
    )
    blocks.append("<h1>Greatbadbeyond Trace Report</h1>")
    blocks.append(
        f"<p class='muted'>frame_stats: {html.escape(str(frame_stats_path))}<br/>"
        f"hitch_trace: {html.escape(str(hitch_trace_path))}</p>"
    )

    blocks.append("<div class='grid'>")
    blocks.append(
        "<div class='card'><h3>Frame Stats Windows</h3>"
        f"<div>count: {len(frame_stats_rows)}</div>"
        f"<div>avg_fps mean/min/max: {format_float(stats_avg_fps['mean'])} / {format_float(stats_avg_fps['min'])} / {format_float(stats_avg_fps['max'])}</div>"
        f"<div>avg_ms mean/min/max: {format_float(stats_avg_ms['mean'])} / {format_float(stats_avg_ms['min'])} / {format_float(stats_avg_ms['max'])}</div>"
        f"<div>gpu_total_avg_ms mean/min/max: {format_float(stats_gpu_ms['mean'])} / {format_float(stats_gpu_ms['min'])} / {format_float(stats_gpu_ms['max'])}</div>"
        "</div>"
    )
    blocks.append(
        "<div class='card'><h3>Hitch Events</h3>"
        f"<div>rows: {len(hitch_rows)}</div>"
        f"<div>frame events: {len(frame_events)}</div>"
        f"<div>event types: {', '.join(f'{k}={v}' for k, v in sorted(event_counts.items()))}</div>"
        f"<div>trigger masks: {', '.join(f'{k}={v}' for k, v in sorted(trigger_counts.items()))}</div>"
        f"<div>frame_work_ms mean/min/max: {format_float(stats_hitch_work_ms['mean'])} / {format_float(stats_hitch_work_ms['min'])} / {format_float(stats_hitch_work_ms['max'])}</div>"
        "</div>"
    )
    blocks.append("</div>")

    blocks.append("<div class='section'>")
    blocks.append(
        svg_line_chart(
            "Hitch Frame Timeline (ms)",
            hitch_wall_s,
            [
                ("frame_ms", hitch_frame_ms, "#8ad4ff"),
                ("frame_work_ms", hitch_work_ms, "#ff8e72"),
                ("acq_ms", hitch_acq_ms, "#ffd166"),
                ("queue_submit_ms", hitch_submit_q_ms, "#9afc8f"),
                ("gpu_total_ms", hitch_gpu_total_ms, "#c7a3ff"),
            ],
        )
    )
    blocks.append("</div>")

    blocks.append("<div class='section'>")
    blocks.append(
        svg_line_chart(
            "Frame Stats Windows (ms)",
            frame_wall_s,
            [
                ("avg_ms", frame_avg_ms, "#8ad4ff"),
                ("p95_ms", frame_p95_ms, "#ff8e72"),
                ("p99_ms", frame_p99_ms, "#ffd166"),
                ("gpu_total_avg_ms", frame_gpu_ms, "#c7a3ff"),
            ],
        )
    )
    blocks.append("</div>")

    blocks.append("<div class='section'><h2>Top Frame Work Events</h2>")
    blocks.append("<table><thead><tr><th>wall_s</th><th>loop_frame</th><th>frame_ms</th><th>frame_work_ms</th><th>acq_ms</th><th>queue_submit_ms</th><th>trigger_mask</th></tr></thead><tbody>")
    for row in top_frame_events:
        blocks.append(
            "<tr>"
            f"<td>{html.escape(row.get('wall_s', ''))}</td>"
            f"<td>{html.escape(row.get('loop_frame', ''))}</td>"
            f"<td>{html.escape(row.get('frame_ms', ''))}</td>"
            f"<td>{html.escape(row.get('frame_work_ms', ''))}</td>"
            f"<td>{html.escape(row.get('acq_ms', ''))}</td>"
            f"<td>{html.escape(row.get('queue_submit_ms', ''))}</td>"
            f"<td>{html.escape(row.get('trigger_mask', ''))}</td>"
            "</tr>"
        )
    blocks.append("</tbody></table></div>")
    blocks.append("</body></html>")
    return "\n".join(blocks)


def main() -> None:
    parser = argparse.ArgumentParser(description="Visualize frame_stats.csv and hitch_trace.csv")
    parser.add_argument("--frame-stats", type=Path, default=DEFAULT_FRAME_STATS)
    parser.add_argument("--hitch-trace", type=Path, default=DEFAULT_HITCH_TRACE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    if not args.frame_stats.is_file():
        raise SystemExit(f"error: missing frame stats CSV: {args.frame_stats}")
    if not args.hitch_trace.is_file():
        raise SystemExit(f"error: missing hitch trace CSV: {args.hitch_trace}")

    frame_stats_rows = read_csv_rows(args.frame_stats)
    hitch_rows = read_csv_rows(args.hitch_trace)

    report_html = build_html_report(
        frame_stats_path=args.frame_stats,
        hitch_trace_path=args.hitch_trace,
        frame_stats_rows=frame_stats_rows,
        hitch_rows=hitch_rows,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(report_html, encoding="utf-8")
    print(f"wrote {args.output}")
    print(f"frame_stats rows: {len(frame_stats_rows)}")
    print(f"hitch_trace rows: {len(hitch_rows)}")


if __name__ == "__main__":
    main()

