#!/usr/bin/env python3
"""
Visualize results from offline_evaluator

This script:
- Reads benchmark_results_offline*.csv
- Computes aggregated statistics per algorithm and memory
- Generates plots like the paper (ARE vs memory, metric summaries)
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import sys
import os
from pathlib import Path

def read_results(filepath):
    """Read and validate results CSV"""
    try:
        df = pd.read_csv(filepath)
        required = ['algorithm', 'memory_kb', 'flow_id', 'are', 'cosine_sim', 
                    'euclidean_dist', 'energy_sim', 'packets']
        missing = [c for c in required if c not in df.columns]
        if missing:
            print(f"Error: Missing columns: {missing}")
            return None
        
        # Convert numeric columns
        numeric_cols = ['memory_kb', 'are', 'cosine_sim', 'euclidean_dist', 
                        'energy_sim', 'packets']
        for col in numeric_cols:
            df[col] = pd.to_numeric(df[col], errors='coerce')
        
        return df
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return None

def compute_summary(df):
    """Compute summary statistics per algorithm and memory"""
    summary = df.groupby(['algorithm', 'memory_kb']).agg({
        'are': ['mean', 'median', 'std'],
        'cosine_sim': ['mean', 'median', 'std'],
        'euclidean_dist': ['mean', 'median', 'std'],
        'energy_sim': ['mean', 'median', 'std'],
        'packets': 'sum',
        'flow_id': 'count'
    }).reset_index()
    
    # Flatten column names
    summary.columns = ['_'.join(col).strip('_') if col[1] else col[0] 
                       for col in summary.columns.values]
    
    return summary

def plot_are_vs_memory(df, outdir):
    """Plot ARE vs Memory for each algorithm (like paper Figure 11)"""
    algorithms = df['algorithm'].unique()
    
    plt.figure(figsize=(10, 6))
    
    for alg in algorithms:
        alg_data = df[df['algorithm'] == alg].groupby('memory_kb')['are'].mean()
        plt.plot(alg_data.index, alg_data.values, marker='o', label=alg, linewidth=2)
    
    plt.xlabel('Memory (KB)', fontsize=12)
    plt.ylabel('Average Relative Error (ARE)', fontsize=12)
    plt.title('Reconstruction Quality vs Memory Budget', fontsize=14, fontweight='bold')
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    out_path = os.path.join(outdir, 'are_vs_memory.png')
    plt.savefig(out_path, dpi=150, bbox_inches='tight')
    print(f"Saved: {out_path}")
    plt.close()

def plot_metrics_summary(df, outdir):
    """Plot bar chart comparing all metrics across algorithms"""
    summary = df.groupby('algorithm').agg({
        'are': 'mean',
        'cosine_sim': 'mean',
        'euclidean_dist': 'mean',
        'energy_sim': 'mean'
    }).reset_index()
    
    metrics = ['are', 'cosine_sim', 'euclidean_dist', 'energy_sim']
    metric_names = ['ARE↓', 'Cosine↑', 'Euclidean↓', 'Energy↑']
    
    fig, axes = plt.subplots(2, 2, figsize=(12, 8))
    axes = axes.flatten()
    
    for i, (metric, name) in enumerate(zip(metrics, metric_names)):
        ax = axes[i]
        data = summary.sort_values(metric, ascending=(i in [0, 2]))
        
        bars = ax.bar(range(len(data)), data[metric], color=plt.cm.Set3(np.linspace(0, 1, len(data))))
        ax.set_xticks(range(len(data)))
        ax.set_xticklabels(data['algorithm'], rotation=45, ha='right')
        ax.set_ylabel(name, fontsize=11)
        ax.set_title(f'{name} by Algorithm', fontsize=12, fontweight='bold')
        ax.grid(axis='y', alpha=0.3)
        
        # Add value labels on bars
        for j, bar in enumerate(bars):
            height = bar.get_height()
            ax.text(bar.get_x() + bar.get_width()/2., height,
                   f'{height:.3f}', ha='center', va='bottom', fontsize=9)
    
    plt.tight_layout()
    out_path = os.path.join(outdir, 'metrics_summary.png')
    plt.savefig(out_path, dpi=150, bbox_inches='tight')
    print(f"Saved: {out_path}")
    plt.close()

def plot_per_algorithm_are(df, outdir):
    """Create individual ARE vs memory plot for each algorithm"""
    algorithms = df['algorithm'].unique()
    
    for alg in algorithms:
        alg_data = df[df['algorithm'] == alg]
        summary = alg_data.groupby('memory_kb').agg({
            'are': ['mean', 'std'],
            'flow_id': 'count'
        }).reset_index()
        
        plt.figure(figsize=(8, 5))
        mean_are = summary['are']['mean']
        std_are = summary['are']['std']
        memory = summary['memory_kb']
        
        plt.errorbar(memory, mean_are, yerr=std_are, marker='o', 
                    capsize=5, linewidth=2, markersize=8, label=alg)
        
        plt.xlabel('Memory (KB)', fontsize=12)
        plt.ylabel('Average Relative Error (ARE)', fontsize=12)
        plt.title(f'{alg}: Reconstruction Quality', fontsize=14, fontweight='bold')
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        
        safe_name = alg.replace('-', '_').replace(' ', '_')
        out_path = os.path.join(outdir, f'are_vs_memory_{safe_name}.png')
        plt.savefig(out_path, dpi=150, bbox_inches='tight')
        print(f"Saved: {out_path}")
        plt.close()

def main():
    if len(sys.argv) < 2:
        print("Usage: python visualize_results.py <results.csv> [output_dir]")
        print("Example: python visualize_results.py benchmark_results_offline_test.csv out_plots/")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_dir = sys.argv[2] if len(sys.argv) > 2 else 'out_plots'
    
    if not os.path.exists(input_file):
        print(f"Error: File not found: {input_file}")
        sys.exit(1)
    
    # Create output directory
    os.makedirs(output_dir, exist_ok=True)
    
    print(f"Reading: {input_file}")
    df = read_results(input_file)
    if df is None or df.empty:
        print("Error: No data to process")
        sys.exit(1)
    
    print(f"Loaded {len(df)} rows, {df['flow_id'].nunique()} flows, "
          f"{df['algorithm'].nunique()} algorithms")
    
    # Compute and save summary
    print("\nComputing summary statistics...")
    summary = compute_summary(df)
    summary_path = os.path.join(output_dir, 'summary_stats.csv')
    summary.to_csv(summary_path, index=False)
    print(f"Saved: {summary_path}")
    
    # Generate plots
    print("\nGenerating plots...")
    plot_are_vs_memory(df, output_dir)
    plot_metrics_summary(df, output_dir)
    plot_per_algorithm_are(df, output_dir)
    
    print(f"\n✓ All outputs saved to: {output_dir}/")
    print("\nTop 5 flows by ARE (worst reconstructions):")
    top_errors = df.nlargest(5, 'are')[['algorithm', 'memory_kb', 'flow_id', 
                                         'are', 'packets']]
    print(top_errors.to_string(index=False))

if __name__ == '__main__':
    main()
