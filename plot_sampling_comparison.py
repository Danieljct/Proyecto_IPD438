#!/usr/bin/env python3
"""Generate comparison plots across sampling ratios for flow/queue CSV outputs."""

from __future__ import annotations

import argparse
import glob
import re
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import matplotlib.pyplot as plt
import pandas as pd


@dataclass(frozen=True)
class SamplingRun:
    numerator: int
    denominator: int
    ratio_value: float
    ratio_label: str
    flow_path: Path
    queue_path: Path | None


_SAMPLING_PATTERN = re.compile(r"sr(\d+)(?:_(\d+))?", re.IGNORECASE)


def parse_sampling_ratio(path: Path) -> Tuple[int, int]:
    match = _SAMPLING_PATTERN.search(path.stem)
    if not match:
        raise ValueError(f"Could not parse sampling ratio from {path.name}")
    numerator = int(match.group(1))
    denominator = int(match.group(2) or 1)
    fraction = Fraction(numerator, denominator)
    return fraction.numerator, fraction.denominator


def find_runs(flow_pattern: str, queue_pattern: str | None) -> List[SamplingRun]:
    flow_files = sorted(Path(p) for p in glob.glob(flow_pattern))
    if not flow_files:
        raise FileNotFoundError(f"No flow CSV files match pattern '{flow_pattern}'")

    queue_lookup: Dict[str, Path] = {}
    if queue_pattern:
        for path in glob.glob(queue_pattern):
            stem = Path(path).stem
            queue_lookup[stem] = Path(path)

    runs: List[SamplingRun] = []
    for flow_path in flow_files:
        numerator, denominator = parse_sampling_ratio(flow_path)
        ratio_value = numerator / denominator
        ratio_label = f"{numerator}/{denominator}" if denominator != 1 else f"{numerator}"
        queue_path = queue_lookup.get(flow_path.stem.replace("flow_rate", "queue_gt"))
        runs.append(
            SamplingRun(
                numerator=numerator,
                denominator=denominator,
                ratio_value=ratio_value,
                ratio_label=ratio_label,
                flow_path=flow_path,
                queue_path=queue_path,
            )
        )

    runs.sort(key=lambda run: run.ratio_value, reverse=True)
    return runs


def load_baseline(path: Path | None) -> pd.DataFrame | None:
    if not path:
        return None
    if not path.exists():
        return None
    return pd.read_csv(path)


def align_on_time(base: pd.DataFrame, df: pd.DataFrame, column: str) -> pd.Series:
    merged = pd.merge(base[["time_s", column]], df[["time_s", column]], on="time_s", how="inner", suffixes=("_base", ""))
    return merged.set_index("time_s")


def plot_sampling_runs(
    runs: Iterable[SamplingRun],
    baseline: pd.DataFrame | None,
    output: Path,
    dpi: int,
    show: bool,
    rescale_ecn: bool,
    rescale_recon: bool,
    window_us: float,
    start_time: float | None,
) -> None:
    runs = list(runs)
    if baseline is None:
        baseline = pd.read_csv(runs[0].flow_path)

    window_us = max(0.0, window_us)

    fig = plt.figure(figsize=(14, 8))
    gs = fig.add_gridspec(2, 2, height_ratios=[2, 1], hspace=0.25, wspace=0.2)

    ax_rate = fig.add_subplot(gs[0, 0])
    ax_error = fig.add_subplot(gs[1, 0], sharex=ax_rate)
    ax_ecn = fig.add_subplot(gs[0, 1], sharex=ax_rate)
    ax_queue = fig.add_subplot(gs[1, 1], sharex=ax_rate)

    baseline_time_min = baseline["time_s"].min()
    baseline_time_max = baseline["time_s"].max()

    if start_time is None:
        time_min = baseline_time_min
    else:
        time_min = max(start_time, baseline_time_min)

    if window_us > 0.0:
        time_max = time_min + window_us * 1e-6
    else:
        time_max = baseline_time_max

    if time_max <= time_min:
        raise ValueError("Invalid window: end time must be greater than start time")

    baseline = baseline[baseline["time_s"].between(time_min, time_max)]
    if baseline.empty:
        raise ValueError("Selected window produces an empty baseline slice")

    base_time = baseline["time_s"]
    ax_rate.plot(base_time, baseline["total_rate_gbps"], color="black", linewidth=2, label="Aggregate rate")

    for run in runs:
        df = pd.read_csv(run.flow_path)
        df = df[df["time_s"].between(time_min, time_max)]
        if df.empty:
            continue
        label = f"sr {run.ratio_label}"
        merged = pd.merge(
            baseline[["time_s", "total_rate_gbps"]],
            df[["time_s", "reconstructed_rate_gbps"]],
            on="time_s",
            how="inner",
            suffixes=("_base", ""),
        )

        recon_series = df["reconstructed_rate_gbps"].copy()
        scale_factor = 1.0
        if rescale_recon and not merged.empty:
            recon_vals = merged["reconstructed_rate_gbps"].to_numpy()
            base_vals = merged["total_rate_gbps"].to_numpy()
            denom = float((recon_vals ** 2).sum())
            if denom > 0:
                scale_factor = float((base_vals * recon_vals).sum()) / denom
                recon_series *= scale_factor
                merged["reconstructed_rate_gbps"] = merged["reconstructed_rate_gbps"] * scale_factor
        ax_rate.plot(
            df["time_s"],
            recon_series,
            label=f"Reconstructed ({label}{'' if scale_factor == 1.0 else f' × {scale_factor:.2f}'})",
        )

        if not merged.empty:
            error = (merged["reconstructed_rate_gbps"] - merged["total_rate_gbps"]) / merged["total_rate_gbps"]
            ax_error.plot(merged["time_s"], error, label=label)

        if rescale_ecn and run.ratio_value > 0:
            scale_factor = run.denominator / run.numerator
            ecn_series = df["ecn_marks"] * scale_factor
            ecn_label = f"{label} (est.)"
        else:
            ecn_series = df["ecn_marks"]
            ecn_label = label
        ax_ecn.plot(df["time_s"], ecn_series, label=ecn_label)

        if run.queue_path and run.queue_path.exists():
            q_df = pd.read_csv(run.queue_path)
            q_df = q_df[q_df["time_s"].between(time_min, time_max)]
            if not q_df.empty:
                ax_queue.plot(q_df["time_s"], q_df["max_queue_bytes"], label=label)

    ax_rate.set_ylabel("Throughput (Gbps)")
    ax_rate.set_title("Aggregate vs reconstructed throughput")
    ax_rate.legend(loc="upper right", frameon=False)

    ax_error.set_ylabel("Relative error")
    ax_error.set_xlabel("Time (s)")
    ax_error.axhline(0, color="gray", linewidth=1, linestyle="--")
    ax_error.legend(loc="upper right", frameon=False)

    ax_ecn.set_ylabel("ECN marks (estimated)" if rescale_ecn else "ECN marks")
    ax_ecn.set_title("ECN marks per window")
    ax_ecn.legend(loc="upper right", frameon=False)

    ax_queue.set_ylabel("Max queue (bytes)")
    ax_queue.set_xlabel("Time (s)")
    ax_queue.set_title("Queue occupancy ground truth")
    if ax_queue.lines:
        ax_queue.legend(loc="upper right", frameon=False)
    else:
        ax_queue.text(0.5, 0.5, "No queue data", ha="center", va="center", transform=ax_queue.transAxes)
        ax_queue.set_xticks([])

    fig.suptitle("Sampling ratio comparison", fontsize=14)

    ax_rate.set_xlim(time_min, time_max)

    if output:
        fig.savefig(output, dpi=dpi, bbox_inches="tight")
    if show:
        plt.show()
    plt.close(fig)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plot sampling comparison figures")
    parser.add_argument(
        "--flow-pattern",
        default="flow_rate_sr*.csv",
        help="Glob pattern for flow CSV files",
    )
    parser.add_argument(
        "--queue-pattern",
        default="queue_gt_sr*.csv",
        help="Glob pattern for queue CSV files",
    )
    parser.add_argument(
        "--baseline-flow",
        default="flow_rate.csv",
        help="Path to baseline aggregate flow CSV",
    )
    parser.add_argument(
        "--output",
        default="flow_sampling_comparison.png",
        help="Path to save the comparison figure",
    )
    parser.add_argument("--dpi", type=int, default=200, help="Output figure DPI")
    parser.add_argument("--show", action="store_true", help="Display the figure interactively")
    parser.add_argument(
        "--raw-ecn",
        action="store_true",
        help="Plot sampled ECN counts without scaling back by sampling ratio",
    )
    parser.add_argument(
        "--raw-recon",
        action="store_true",
        help="Plot reconstructed throughput without post-hoc scaling",
    )
    parser.add_argument(
        "--window-us",
        type=float,
        default=0.0,
        help="If > 0, restrict plots to the first N microseconds of the trace",
    )
    parser.add_argument(
        "--start-s",
        type=float,
        default=None,
        help="Start time (in seconds) for the plotting window. Defaults to the trace minimum",
    )
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    runs = find_runs(args.flow_pattern, args.queue_pattern)
    baseline = load_baseline(Path(args.baseline_flow) if args.baseline_flow else None)
    output_path = Path(args.output) if args.output else Path("flow_sampling_comparison.png")

    plot_sampling_runs(
        runs,
        baseline,
        output_path,
        args.dpi,
        args.show,
        not args.raw_ecn,
        not args.raw_recon,
        args.window_us,
        args.start_s,
    )


if __name__ == "__main__":
    main()
