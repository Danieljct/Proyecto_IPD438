#!/usr/bin/env python3
"""Compute error metrics for sampled flow_rate CSV files against baseline."""

from __future__ import annotations

import argparse
import glob
import math
import sys
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Iterable, List

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


@dataclass(frozen=True)
class SamplingMetrics:
    ratio_label: str
    ratio_value: float
    file_path: Path
    are_total: float
    are_recon: float
    cosine_total: float
    cosine_recon: float
    rmse_total: float
    rmse_recon: float
    window_count: int


def parse_ratio_from_name(path: Path) -> str:
    stem = path.stem
    if "sr" not in stem:
        return "unknown"
    try:
        tag = stem.split("sr", 1)[1]
    except IndexError:
        return "unknown"
    tag = tag.replace("_", "/")
    if tag.startswith("/"):
        tag = tag[1:]
    if not tag:
        return "unknown"
    parts = tag.split("/")
    try:
        nums = [int(p) for p in parts if p]
    except ValueError:
        return tag
    if len(nums) == 1:
        return str(nums[0])
    frac = Fraction(nums[0], math.prod(nums[1:]))
    if frac.denominator == 1:
        return f"{frac.numerator}"
    return f"{frac.numerator}/{frac.denominator}"


def parse_ratio_value(ratio_label: str) -> float:
    try:
        if "/" in ratio_label:
            num, den = ratio_label.split("/", 1)
            return float(Fraction(int(num), int(den)))
        return float(Fraction(int(ratio_label), 1))
    except Exception:
        return float("nan")


def compute_metrics_for_file(baseline: pd.DataFrame, sample_path: Path) -> SamplingMetrics | None:
    sample = pd.read_csv(sample_path)
    merged = baseline.merge(sample, on="time_s", suffixes=("_base", ""))
    if merged.empty:
        return None

    base = merged["total_rate_gbps_base"].to_numpy(dtype=float)
    total = merged["total_rate_gbps"].to_numpy(dtype=float)
    recon = merged["reconstructed_rate_gbps"].to_numpy(dtype=float)

    mask = base != 0
    if not mask.any():
        are_total = float("nan")
        are_recon = float("nan")
    else:
        are_total = float(np.mean(np.abs(total[mask] - base[mask]) / base[mask]))
        are_recon = float(np.mean(np.abs(recon[mask] - base[mask]) / base[mask]))

    def cosine_similarity(a: np.ndarray, b: np.ndarray) -> float:
        denom = float(np.linalg.norm(a) * np.linalg.norm(b))
        if denom == 0:
            return float("nan")
        return float(np.clip(np.dot(a, b) / denom, -1.0, 1.0))

    cosine_total = cosine_similarity(total, base)
    cosine_recon = cosine_similarity(recon, base)

    rmse_total = float(math.sqrt(np.mean((total - base) ** 2)))
    rmse_recon = float(math.sqrt(np.mean((recon - base) ** 2)))

    ratio_label = parse_ratio_from_name(sample_path)
    return SamplingMetrics(
        ratio_label=ratio_label,
        ratio_value=parse_ratio_value(ratio_label),
        file_path=sample_path,
        are_total=are_total,
        are_recon=are_recon,
        cosine_total=cosine_total,
        cosine_recon=cosine_recon,
        rmse_total=rmse_total,
        rmse_recon=rmse_recon,
        window_count=int(len(merged)),
    )


def format_percentage(value: float) -> str:
    if math.isnan(value):
        return "nan"
    return f"{value * 100:.3f}%"


def format_float(value: float) -> str:
    if math.isnan(value):
        return "nan"
    return f"{value:.6f}"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Compute ARE, cosine similarity, and RMSE for sampling runs")
    parser.add_argument(
        "--baseline",
        default="flow_rate.csv",
        help="Baseline CSV path (default: flow_rate.csv)",
    )
    parser.add_argument(
        "--pattern",
        default="flow_rate_sr*.csv",
        help="Glob pattern for sampled CSV files",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Optional path to save metrics as CSV",
    )
    parser.add_argument(
        "--plot",
        default=None,
        help="Optional path (PNG/PDF) to save plots of metrics vs sampling ratio",
    )
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    baseline_path = Path(args.baseline)
    if not baseline_path.exists():
        print(f"Baseline file '{baseline_path}' not found", file=sys.stderr)
        sys.exit(1)

    baseline = pd.read_csv(baseline_path)
    baseline = baseline[["time_s", "total_rate_gbps"]]

    sample_files = sorted(Path(p) for p in glob.glob(args.pattern))
    if not sample_files:
        print(f"No files match pattern '{args.pattern}'", file=sys.stderr)
        sys.exit(1)

    metrics: List[SamplingMetrics] = []
    for path in sample_files:
        result = compute_metrics_for_file(baseline, path)
        if result is not None:
            metrics.append(result)

    metrics.sort(key=lambda m: (m.ratio_value if not math.isnan(m.ratio_value) else -math.inf), reverse=True)

    if not metrics:
        print("No metrics were computed (empty datasets)", file=sys.stderr)
        sys.exit(1)

    header = (
        "ratio",
        "windows",
        "ARE_total",
        "ARE_recon",
        "cosine_total",
        "cosine_recon",
        "RMSE_total",
        "RMSE_recon",
        "file",
    )
    print("\t".join(header))
    for m in metrics:
        line = (
            m.ratio_label,
            str(m.window_count),
            format_percentage(m.are_total),
            format_percentage(m.are_recon),
            format_float(m.cosine_total),
            format_float(m.cosine_recon),
            format_float(m.rmse_total),
            format_float(m.rmse_recon),
            str(m.file_path),
        )
        print("\t".join(line))

    if args.output:
        df_out = pd.DataFrame(
            {
                "ratio": [m.ratio_label for m in metrics],
                "file": [str(m.file_path) for m in metrics],
                "windows": [m.window_count for m in metrics],
                "are_total": [m.are_total for m in metrics],
                "are_recon": [m.are_recon for m in metrics],
                "cosine_total": [m.cosine_total for m in metrics],
                "cosine_recon": [m.cosine_recon for m in metrics],
                "rmse_total": [m.rmse_total for m in metrics],
                "rmse_recon": [m.rmse_recon for m in metrics],
            }
        )
        df_out.to_csv(args.output, index=False)

    if args.plot:
        fig, axes = plt.subplots(1, 3, figsize=(15, 4))
        ratios = list(range(len(metrics)))
        labels = [m.ratio_label for m in metrics]

        axes[0].plot(labels, [m.are_total * 100 for m in metrics], marker="o", label="ARE total")
        axes[0].plot(labels, [m.are_recon * 100 for m in metrics], marker="s", label="ARE recon")
        axes[0].set_ylabel("ARE (%)")
        axes[0].set_xlabel("Sampling ratio")
        axes[0].set_title("Average relative error")
        axes[0].legend(frameon=False)

        axes[1].plot(labels, [m.cosine_total for m in metrics], marker="o", label="Cosine total")
        axes[1].plot(labels, [m.cosine_recon for m in metrics], marker="s", label="Cosine recon")
        axes[1].set_ylabel("Cosine similarity")
        axes[1].set_xlabel("Sampling ratio")
        axes[1].set_ylim(0, 1.05)
        axes[1].set_title("Cosine similarity vs baseline")
        axes[1].legend(frameon=False)

        axes[2].plot(labels, [m.rmse_total for m in metrics], marker="o", label="RMSE total")
        axes[2].plot(labels, [m.rmse_recon for m in metrics], marker="s", label="RMSE recon")
        axes[2].set_ylabel("RMSE (Gbps)")
        axes[2].set_xlabel("Sampling ratio")
        axes[2].set_title("RMSE vs baseline")
        axes[2].legend(frameon=False)

        fig.suptitle("Sampling metrics vs baseline")
        fig.tight_layout()
        fig.savefig(args.plot, dpi=200)
        plt.close(fig)


if __name__ == "__main__":
    main()
