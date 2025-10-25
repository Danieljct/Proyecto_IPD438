#!/usr/bin/env python3
"""
Compare our offline benchmark CSV (benchmark_results_offline.csv) with the
authors' report CSV (report.csv) and produce aggregated summaries and a small
plot. The script is conservative and tries to match records by flow id and by
nearest memory size when needed.

Usage:
  python3 scripts/compare_reports.py \
    --ours ../benchmark_results_offline.csv \
    --authors ../../uMon-WaveSketch/cpp_version/build/report.csv \
    --outdir out_compare

Dependencies: pandas, numpy, matplotlib

Outputs written to outdir:
- summary_ours.csv
- summary_authors.csv
- merged_summary.csv (attempt at matching summaries)
- top_discrepancies.csv (top-10 by ARE difference)
- a simple PNG plot comparing mean AREs for one matched pair (if available)
"""

import argparse
import os
import sys
from pathlib import Path

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


def read_ours(path):
    df = pd.read_csv(path)
    # Ensure numeric columns exist
    for col in ["memory_kb", "are", "cosine_sim", "euclidean_dist", "energy_sim"]:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    return df


def read_authors(path):
    df = pd.read_csv(path)
    # standardize author column names to our naming where possible
    # their CSV has: class,memory,id,length,l1,l2,are,energy,cos,...
    for col in ["memory", "are", "energy", "cos"]:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors="coerce")
    return df


def canonicalize_names(df):
    # Map common algorithm name variants to canonical forms to improve matching
    mapping = {
        'wavesketch-ideal': 'Wavelet-Ideal',
        'wavesketch': 'Wavelet',
        'wavelet-ideal': 'Wavelet-Ideal',
        'persistcms': 'Persist-CMS',
        'persist-cms': 'Persist-CMS',
        'omniwindow': 'OmniWindow',
        'fourier': 'Fourier',
        'naive-sketch': 'Naive-Sketch'
    }
    if 'algorithm' in df.columns:
        df['algorithm_canon'] = df['algorithm'].apply(lambda x: mapping.get(str(x).lower(), x))
    if 'class' in df.columns:
        df['algorithm'] = df['class']
        df['algorithm_canon'] = df['algorithm'].apply(lambda x: mapping.get(str(x).lower(), x))
    return df


def summarize_ours(df):
    # Group by algorithm and memory_kb
    g = df.groupby(["algorithm", "memory_kb"]).agg(
        count=("flow_id", "count"),
        mean_are=("are", "mean"),
        mean_cosine=("cosine_sim", "mean"),
        mean_euclid=("euclidean_dist", "mean"),
        mean_energy=("energy_sim", "mean"),
    ).reset_index()
    return g


def summarize_authors(df):
    # Group by class and memory
    if "class" in df.columns and "memory" in df.columns:
        g = df.groupby(["class", "memory"]).agg(
            count=("id", "count"),
            mean_are=("are", "mean"),
            mean_cosine=("cos", "mean"),
            mean_euclid=("l2", "mean"),
            mean_energy=("energy", "mean"),
        ).reset_index()
        # rename to match our summary column names
        g = g.rename(columns={"class": "algorithm", "memory": "memory_kb"})
        # normalize memory units heuristically: if median of memory_kb is very large
        try:
            med = g['memory_kb'].median()
            # authors file sometimes reports memory in bytes; if median > 10000 and our memories are small,
            # convert to KB
            if med > 10000:
                g['memory_kb'] = g['memory_kb'] / 1024.0
        except Exception:
            pass
        return g
    else:
        raise RuntimeError("Unexpected authors CSV format — missing 'class' or 'memory' columns")


def match_summaries(our_sum, auth_sum):
    # Attempt to match rows by algorithm name (substring match) and memory.
    rows = []
    # Ensure canonical names
    our_sum = canonicalize_names(our_sum)
    auth_sum = canonicalize_names(auth_sum)

    for _, orow in our_sum.iterrows():
        alg = str(orow.algorithm_canon).lower() if 'algorithm_canon' in orow else str(orow.algorithm).lower()
        mem = orow.memory_kb
        # find candidate author rows where algorithm string appears in class name or viceversa
        candidates = auth_sum[auth_sum["algorithm"].str.lower().str.contains(alg, na=False) |
                               auth_sum["algorithm"].str.lower().apply(lambda x: alg in x if isinstance(x, str) else False)]
        if candidates.empty:
            # fallback: try any author row (will pick nearest memory)
            candidates = auth_sum
        # pick candidate with memory closest to our memory
        candidates = candidates.copy()
        candidates["mem_diff"] = np.abs(candidates["memory_kb"].astype(float) - float(mem))
        best = candidates.sort_values("mem_diff").iloc[0]
        merged = {
            "our_algorithm": orow.algorithm,
            "our_memory_kb": orow.memory_kb,
            "our_count": orow.count,
            "our_mean_are": orow.mean_are,
            "our_mean_cosine": orow.mean_cosine,
            "our_mean_euclid": orow.mean_euclid,
            "our_mean_energy": orow.mean_energy,
            "auth_algorithm": best.algorithm,
            "auth_memory_kb": best.memory_kb,
            "auth_count": best.count,
            "auth_mean_are": best.mean_are,
            "auth_mean_cosine": best.mean_cosine,
            "auth_mean_euclid": best.mean_euclid,
            "auth_mean_energy": best.mean_energy,
            "mem_diff": best.mem_diff,
        }
        rows.append(merged)
    return pd.DataFrame(rows)


def find_top_discrepancies(our_df, auth_df, max_rows=10):
    # Join by flow id (flow_id vs id). When memory differs, pick closest memory for that id.
    # Build a simple index of author rows by id
    auth_index = {}
    if "id" in auth_df.columns:
        for _, r in auth_df.iterrows():
            key = r["id"]
            auth_index.setdefault(key, []).append(r)
    else:
        return pd.DataFrame()

    entries = []
    for _, our_row in our_df.iterrows():
        fid = our_row.get("flow_id")
        if pd.isna(fid):
            continue
        if fid not in auth_index:
            continue
        # pick author row with closest memory
        our_mem = our_row.get("memory_kb")
        candidates = auth_index[fid]
        # convert to DataFrame for operations
        cand_df = pd.DataFrame(candidates)
        if "memory" in cand_df.columns:
            mems = pd.to_numeric(cand_df["memory"], errors="coerce").fillna(np.inf)
            idx = (np.abs(mems - our_mem)).argmin()
            auth_row = cand_df.iloc[idx]
        else:
            auth_row = cand_df.iloc[0]

        # compute discrepancy measures (ARE difference if present)
        our_are = pd.to_numeric(our_row.get("are"), errors="coerce")
        auth_are = pd.to_numeric(auth_row.get("are"), errors="coerce")
        are_diff = np.nan
        if not pd.isna(our_are) and not pd.isna(auth_are):
            are_diff = float(our_are) - float(auth_are)

        entries.append({
            "flow_id": fid,
            "our_algorithm": our_row.get("algorithm"),
            "our_memory_kb": our_row.get("memory_kb"),
            "our_are": our_are,
            "auth_algorithm": auth_row.get("class") if "class" in auth_row.index else auth_row.get("algorithm"),
            "auth_memory": auth_row.get("memory"),
            "auth_are": auth_are,
            "are_diff": are_diff,
            "our_packets": our_row.get("packets"),
        })

    out = pd.DataFrame(entries)
    out = out.sort_values(by=["are_diff"], key=lambda col: col.abs(), ascending=False)
    return out.head(max_rows)


def plot_pair(merged_df, outdir):
    if merged_df.empty:
        return
    # pick the first matched pair to plot
    row = merged_df.iloc[0]
    our_alg = row.our_algorithm
    auth_alg = row.auth_algorithm
    our_mem = row.our_memory_kb
    auth_mem = row.auth_memory_kb

    fig, ax = plt.subplots(figsize=(8, 4))
    labels = [f"our {our_alg} ({our_mem})", f"auth {auth_alg} ({auth_mem})"]
    are_vals = [row.our_mean_are, row.auth_mean_are]
    ax.bar(labels, are_vals, color=["C0", "C1"]) 
    ax.set_ylabel("Mean ARE")
    ax.set_title(f"Mean ARE comparison — {our_alg} vs {auth_alg}")
    fname = os.path.join(outdir, "mean_are_comparison.png")
    fig.tight_layout()
    fig.savefig(fname)
    print(f"Wrote plot: {fname}")


def plot_all(merged_df, outdir):
    """Plot ARE vs memory for each our_algorithm using the merged dataframe.
    Saves one combined figure and per-algorithm PNGs.
    """
    if merged_df.empty:
        print("No merged data to plot.")
        return
    import math
    # Ensure memory columns are numeric
    merged_df['our_memory_kb'] = pd.to_numeric(merged_df['our_memory_kb'], errors='coerce')
    merged_df['auth_memory_kb'] = pd.to_numeric(merged_df['auth_memory_kb'], errors='coerce')
    merged_df['our_mean_are'] = pd.to_numeric(merged_df['our_mean_are'], errors='coerce')
    merged_df['auth_mean_are'] = pd.to_numeric(merged_df['auth_mean_are'], errors='coerce')

    algs = merged_df['our_algorithm'].unique()
    n = len(algs)
    cols = 2
    rows = math.ceil(n / cols)
    fig_all, axes = plt.subplots(rows, cols, figsize=(6*cols, 4*rows))
    axes = axes.flatten() if n>1 else [axes]

    for i, alg in enumerate(algs):
        ax = axes[i]
        sub = merged_df[merged_df['our_algorithm'] == alg].sort_values('our_memory_kb')
        if sub.empty:
            continue
        ax.plot(sub['our_memory_kb'], sub['our_mean_are'], marker='o', label='ours')
        ax.plot(sub['auth_memory_kb'], sub['auth_mean_are'], marker='x', label='authors')
        ax.set_xscale('log')
        ax.set_yscale('log')
        ax.set_xlabel('Memory (KB)')
        ax.set_ylabel('Mean ARE')
        ax.set_title(alg)
        ax.legend()
        ax.grid(True, which='both', ls='--', lw=0.5)
        # per-algorithm file
        fname_alg = os.path.join(outdir, f"are_vs_memory_{alg.replace(' ','_')}.png")
        fig_alg, ax_alg = plt.subplots(figsize=(6,4))
        ax_alg.plot(sub['our_memory_kb'], sub['our_mean_are'], marker='o', label='ours')
        ax_alg.plot(sub['auth_memory_kb'], sub['auth_mean_are'], marker='x', label='authors')
        ax_alg.set_xscale('log')
        ax_alg.set_yscale('log')
        ax_alg.set_xlabel('Memory (KB)')
        ax_alg.set_ylabel('Mean ARE')
        ax_alg.set_title(alg)
        ax_alg.legend()
        ax_alg.grid(True, which='both', ls='--', lw=0.5)
        fig_alg.tight_layout()
        fig_alg.savefig(fname_alg)
        print(f"Wrote per-algorithm plot: {fname_alg}")

    fig_all.tight_layout()
    fname_all = os.path.join(outdir, 'are_vs_memory_all.png')
    fig_all.savefig(fname_all)
    print(f"Wrote combined ARE vs memory plot: {fname_all}")


def plot_metrics_summary(merged_df, outdir):
    """Create bar charts comparing our vs authors mean metrics per algorithm for the 4 metrics."""
    if merged_df.empty:
        return
    metrics = [
        ('our_mean_are', 'auth_mean_are', 'Mean ARE'),
        ('our_mean_cosine', 'auth_mean_cosine', 'Mean Cosine'),
        ('our_mean_euclid', 'auth_mean_euclid', 'Mean Euclidean'),
        ('our_mean_energy', 'auth_mean_energy', 'Mean Energy')
    ]
    algs = merged_df['our_algorithm'].tolist()
    for our_col, auth_col, title in metrics:
        fig, ax = plt.subplots(figsize=(8,4))
        our_vals = merged_df[our_col].astype(float).fillna(0.0)
        auth_vals = merged_df[auth_col].astype(float).fillna(0.0)
        x = np.arange(len(algs))
        width = 0.35
        ax.bar(x - width/2, our_vals, width, label='ours')
        ax.bar(x + width/2, auth_vals, width, label='authors')
        ax.set_xticks(x)
        ax.set_xticklabels(algs, rotation=45, ha='right')
        ax.set_ylabel(title)
        ax.set_title(f"Comparison of {title} by algorithm")
        ax.legend()
        fig.tight_layout()
        fname = os.path.join(outdir, f"metric_summary_{our_col}.png")
        fig.savefig(fname)
        print(f"Wrote metric summary: {fname}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ours", default="benchmark_results_offline.csv")
    parser.add_argument("--authors", default="../uMon-WaveSketch/cpp_version/build/report.csv")
    parser.add_argument("--outdir", default="out_compare")
    args = parser.parse_args()

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    our_df = read_ours(args.ours)
    auth_df = read_authors(args.authors)

    our_sum = summarize_ours(our_df)
    auth_sum = summarize_authors(auth_df)

    our_sum.to_csv(outdir / "summary_ours.csv", index=False)
    auth_sum.to_csv(outdir / "summary_authors.csv", index=False)
    print(f"Wrote summary files to {outdir}")

    merged = match_summaries(our_sum, auth_sum)
    merged.to_csv(outdir / "merged_summary.csv", index=False)
    print(f"Wrote merged summary to {outdir / 'merged_summary.csv'}")

    top = find_top_discrepancies(our_df, auth_df, max_rows=20)
    top.to_csv(outdir / "top_discrepancies.csv", index=False)
    print(f"Wrote top discrepancies to {outdir / 'top_discrepancies.csv'}")

    plot_pair(merged, outdir)
    # additional plotting: ARE vs memory per algorithm
    plot_all(merged, str(outdir))
    # metric summary bar charts for quick algorithm-level comparison
    plot_metrics_summary(merged, str(outdir))

    print("Done. Inspect CSVs in the output directory for stats and discrepancies.")


if __name__ == "__main__":
    main()
