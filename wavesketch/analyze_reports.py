#!/usr/bin/env python3
"""Utility script to summarise sketch metrics and generate plots.

Usage:
    python analyze_reports.py \
        --reports-dir cpp_version/build \
        --output-dir cpp_version/build

The script scans every CSV whose name matches ``report_*.csv`` inside the
``reports-dir`` directory. For the classes Fourier, OmniWindow, Persist-CMS,
Wavelet-Ideal and Wavelet-Practical it aggregates the per-flow metrics and
computes averages for ``l2`` (Euclidean distance), ``are`` (average relative
error), ``cos`` (cosine similarity) and ``energy`` (energy ratio). The values
are written to stdout and plotted against the memory footprint stored in each
CSV file. One PNG is generated per statistic.
"""

from __future__ import annotations

import argparse
import csv
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, Iterable, Tuple


TARGET_CLASSES = [
    "Fourier",
    "OmniWindow",
    "Persist-CMS",
    "Wavelet-Ideal",
    "Wavelet-Practical",
]
TARGET_STATS = {
    "l2": "Average L2 Distance",
    "are": "Average Relative Error",
    "cos": "Average Cosine Similarity",
    "energy": "Average Energy Ratio",
}
PLOT_FILENAMES = {
    "l2": "avg_l2_vs_memory.png",
    "are": "avg_are_vs_memory.png",
    "cos": "avg_cos_vs_memory.png",
    "energy": "avg_energy_vs_memory.png",
}


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Aggregate sketch metrics")
    parser.add_argument(
        "--reports-dir",
        type=Path,
        default=Path("cpp_version/build"),
        help="Directory containing report_*.csv files (default: %(default)s)",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="Directory for generated plots (default: reports directory)",
    )
    parser.add_argument(
        "--no-plots",
        action="store_true",
        help="Skip plot generation and only print numeric summaries.",
    )
    return parser.parse_args(list(argv))


def discover_reports(reports_dir: Path) -> Tuple[Path, ...]:
    if not reports_dir.exists():
        raise FileNotFoundError(f"Reports directory not found: {reports_dir}")
    report_files = tuple(sorted(reports_dir.glob("report_*.csv")))
    if not report_files:
        raise FileNotFoundError(
            f"No report_*.csv files found inside {reports_dir.resolve()}"
        )
    return report_files


def aggregate_metrics(report_files: Iterable[Path]) -> Dict[str, Dict[int, Dict[str, float]]]:
    metrics: Dict[str, Dict[int, Dict[str, float]]] = {
        cls: defaultdict(lambda: {"count": 0.0, **{stat: 0.0 for stat in TARGET_STATS}})
        for cls in TARGET_CLASSES
    }

    for path in report_files:
        with path.open(newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                sketch_class = row.get("class")
                if sketch_class not in metrics:
                    continue
                memory = int(row["memory"])
                entry = metrics[sketch_class][memory]
                entry["count"] += 1
                entry["l2"] += float(row["l2"])
                entry["are"] += float(row["are"])
                entry["cos"] += float(row["cos"])
                entry["energy"] += float(row["energy"])

    return metrics


def compute_averages(metrics: Dict[str, Dict[int, Dict[str, float]]]) -> Dict[str, Dict[int, Dict[str, float]]]:
    for cls_data in metrics.values():
        for memory, entry in cls_data.items():
            count = entry["count"] or 1.0
            for stat in TARGET_STATS:
                entry[stat] = entry[stat] / count
    return metrics


def print_summary(metrics: Dict[str, Dict[int, Dict[str, float]]]) -> None:
    memories = sorted({mem for cls in metrics.values() for mem in cls})
    if not memories:
        print("No matching data found for target classes.")
        return

    header = ["memory"] + [f"{cls}:{stat}" for cls in TARGET_CLASSES for stat in TARGET_STATS]
    print(",".join(header))
    for memory in memories:
        row = [str(memory)]
        for cls in TARGET_CLASSES:
            stat_block = metrics.get(cls, {}).get(memory)
            if stat_block is None:
                row.extend(["nan"] * len(TARGET_STATS))
            else:
                row.extend(f"{stat_block[stat]:.6f}" for stat in TARGET_STATS)
        print(",".join(row))


def generate_plots(
    metrics: Dict[str, Dict[int, Dict[str, float]]],
    output_dir: Path,
) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:  # pragma: no cover - guard for missing dependency
        raise SystemExit(
            "matplotlib is required to draw plots. Install it with 'pip install matplotlib'."
        ) from exc

    output_dir.mkdir(parents=True, exist_ok=True)
    memories = sorted({mem for cls in metrics.values() for mem in cls})
    if not memories:
        print("No data available to plot; skipping plot generation.")
        return

    for stat_key, stat_label in TARGET_STATS.items():
        plt.figure(figsize=(8, 5))
        for cls in TARGET_CLASSES:
            cls_data = metrics.get(cls, {})
            xs = []
            ys = []
            for memory in memories:
                entry = cls_data.get(memory)
                if entry is None:
                    continue
                xs.append(memory / 1024.0)
                ys.append(entry[stat_key])
            if xs:
                plt.plot(xs, ys, marker="o", label=cls)
        plt.title(f"{stat_label} vs Memory")
        plt.xlabel("Memory (KB)")
        plt.ylabel(stat_label)
        plt.grid(True, linestyle="--", alpha=0.4)
        plt.legend()
        plt.tight_layout()
        outfile = output_dir / PLOT_FILENAMES[stat_key]
        plt.savefig(outfile, dpi=300)
        plt.close()
        print(f"Saved plot {outfile}")


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    output_dir = args.output_dir or args.reports_dir

    try:
        report_files = discover_reports(args.reports_dir)
    except FileNotFoundError as err:
        print(err, file=sys.stderr)
        return 1

    metrics = aggregate_metrics(report_files)
    metrics = compute_averages(metrics)

    print_summary(metrics)

    if not args.no_plots:
        generate_plots(metrics, output_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
