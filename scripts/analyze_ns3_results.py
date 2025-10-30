#!/usr/bin/env python3
"""
Análisis de resultados de simulaciones NS-3 con algoritmos de sketch
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys
from pathlib import Path

def load_data(filepath):
    """Carga el archivo CSV consolidado"""
    try:
        df = pd.read_csv(filepath)
        print(f"✓ Datos cargados: {len(df)} registros")
        print(f"  Algoritmos: {df['algorithm'].unique()}")
        print(f"  Memorias (KB): {sorted(df['memory_kb'].unique())}")
        print(f"  Flujos únicos: {df['flow_id'].nunique()}")
        print(f"  Tiempo: {df['time_s'].min():.1f}s - {df['time_s'].max():.1f}s")
        return df
    except Exception as e:
        print(f"✗ Error cargando datos: {e}")
        sys.exit(1)

def plot_are_by_algorithm_memory(df, output_dir):
    """Gráfico de ARE por algoritmo y memoria"""
    plt.figure(figsize=(12, 6))
    
    for algo in df['algorithm'].unique():
        algo_data = df[df['algorithm'] == algo]
        avg_are = algo_data.groupby('memory_kb')['are'].mean()
        plt.plot(avg_are.index, avg_are.values, marker='o', linewidth=2, label=algo)
    
    plt.xlabel('Memoria (KB)', fontsize=12)
    plt.ylabel('Average Relative Error (ARE)', fontsize=12)
    plt.title('ARE vs Memoria para Diferentes Algoritmos (NS-3)', fontsize=14, fontweight='bold')
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    output_file = output_dir / 'are_vs_memory.png'
    plt.savefig(output_file, dpi=150)
    print(f"  ✓ {output_file}")
    plt.close()

def plot_cosine_similarity(df, output_dir):
    """Gráfico de Cosine Similarity por algoritmo y memoria"""
    plt.figure(figsize=(12, 6))
    
    for algo in df['algorithm'].unique():
        algo_data = df[df['algorithm'] == algo]
        avg_cos = algo_data.groupby('memory_kb')['cosine_sim'].mean()
        plt.plot(avg_cos.index, avg_cos.values, marker='s', linewidth=2, label=algo)
    
    plt.xlabel('Memoria (KB)', fontsize=12)
    plt.ylabel('Cosine Similarity', fontsize=12)
    plt.title('Cosine Similarity vs Memoria (NS-3)', fontsize=14, fontweight='bold')
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.ylim([0, 1.05])
    plt.tight_layout()
    
    output_file = output_dir / 'cosine_vs_memory.png'
    plt.savefig(output_file, dpi=150)
    print(f"  ✓ {output_file}")
    plt.close()

def plot_heatmap_comparison(df, output_dir):
    """Heatmap comparando ARE de algoritmos en diferentes memorias"""
    pivot = df.groupby(['algorithm', 'memory_kb'])['are'].mean().unstack()
    
    fig, ax = plt.subplots(figsize=(10, 6))
    im = ax.imshow(pivot.values, cmap='RdYlGn_r', aspect='auto')
    
    ax.set_xticks(np.arange(len(pivot.columns)))
    ax.set_yticks(np.arange(len(pivot.index)))
    ax.set_xticklabels([f'{int(x)} KB' for x in pivot.columns])
    ax.set_yticklabels(pivot.index)
    
    # Anotar valores
    for i in range(len(pivot.index)):
        for j in range(len(pivot.columns)):
            value = pivot.values[i, j]
            text = ax.text(j, i, f'{value:.2f}',
                          ha="center", va="center", color="black", fontsize=10)
    
    ax.set_title('Heatmap: ARE Promedio (NS-3)', fontsize=14, fontweight='bold')
    fig.colorbar(im, ax=ax, label='ARE')
    plt.tight_layout()
    
    output_file = output_dir / 'heatmap_are.png'
    plt.savefig(output_file, dpi=150)
    print(f"  ✓ {output_file}")
    plt.close()

def plot_temporal_evolution(df, output_dir):
    """Evolución temporal del ARE para cada algoritmo"""
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    axes = axes.flatten()
    
    algorithms = df['algorithm'].unique()
    
    for idx, algo in enumerate(algorithms):
        ax = axes[idx]
        algo_data = df[df['algorithm'] == algo]
        
        for mem in sorted(algo_data['memory_kb'].unique()):
            mem_data = algo_data[algo_data['memory_kb'] == mem]
            avg_are = mem_data.groupby('time_s')['are'].mean()
            ax.plot(avg_are.index, avg_are.values, marker='o', label=f'{int(mem)} KB', alpha=0.7)
        
        ax.set_xlabel('Tiempo (s)', fontsize=10)
        ax.set_ylabel('ARE', fontsize=10)
        ax.set_title(f'{algo} - Evolución Temporal', fontsize=12, fontweight='bold')
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
    
    plt.tight_layout()
    output_file = output_dir / 'temporal_evolution.png'
    plt.savefig(output_file, dpi=150)
    print(f"  ✓ {output_file}")
    plt.close()

def generate_summary_table(df, output_dir):
    """Genera tabla resumen de métricas"""
    summary = df.groupby(['algorithm', 'memory_kb']).agg({
        'are': ['mean', 'std', 'min', 'max'],
        'cosine_sim': ['mean', 'std'],
        'packets': 'sum',
        'flow_id': 'nunique'
    }).round(4)
    
    summary.columns = ['_'.join(col).strip() for col in summary.columns.values]
    summary = summary.reset_index()
    
    output_file = output_dir / 'summary_statistics.csv'
    summary.to_csv(output_file, index=False)
    print(f"  ✓ {output_file}")
    
    # Imprimir en consola
    print("\n=== Resumen de Estadísticas ===")
    print(summary.to_string())
    
    return summary

def main():
    print("=== Análisis de Resultados NS-3 con Algoritmos de Sketch ===\n")
    
    # Detectar archivo de entrada
    base_dir = Path(__file__).parent.parent
    input_file = base_dir / 'results_ns3_sweep' / 'all_results.csv'
    
    if not input_file.exists():
        print(f"✗ Error: No se encuentra {input_file}")
        print("  Ejecutar primero: scripts/run_ns3_sweep.sh")
        sys.exit(1)
    
    # Cargar datos
    df = load_data(input_file)
    
    # Crear directorio de salida
    output_dir = base_dir / 'analysis_ns3'
    output_dir.mkdir(exist_ok=True)
    print(f"\n✓ Directorio de salida: {output_dir}\n")
    
    # Generar gráficos
    print("Generando visualizaciones...")
    plot_are_by_algorithm_memory(df, output_dir)
    plot_cosine_similarity(df, output_dir)
    plot_heatmap_comparison(df, output_dir)
    plot_temporal_evolution(df, output_dir)
    
    # Generar tabla resumen
    print("\nGenerando estadísticas...")
    summary = generate_summary_table(df, output_dir)
    
    # Análisis de variación por algoritmo
    print("\n=== Análisis de Sensibilidad a Memoria ===")
    for algo in df['algorithm'].unique():
        algo_data = df[df['algorithm'] == algo]
        are_by_mem = algo_data.groupby('memory_kb')['are'].mean()
        
        if len(are_by_mem) > 1:
            variation = ((are_by_mem.max() - are_by_mem.min()) / are_by_mem.mean()) * 100
            print(f"\n{algo}:")
            print(f"  ARE mín: {are_by_mem.min():.4f} ({are_by_mem.idxmin()} KB)")
            print(f"  ARE máx: {are_by_mem.max():.4f} ({are_by_mem.idxmax()} KB)")
            print(f"  Variación: {variation:.2f}%")
    
    print(f"\n✓ Análisis completado. Resultados en: {output_dir}")
    print(f"  Gráficos: {len(list(output_dir.glob('*.png')))} archivos PNG")
    print(f"  CSV: {len(list(output_dir.glob('*.csv')))} archivo(s)")

if __name__ == '__main__':
    main()
