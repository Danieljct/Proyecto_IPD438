#!/usr/bin/env python3
"""
Análisis de resultados NS-3 con RECOMPILACIÓN por memoria
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

def analyze_memory_sensitivity(df):
    """Analiza si hay variación con memoria"""
    print("\n=== Análisis de Sensibilidad a Memoria ===\n")
    
    for algo in df['algorithm'].unique():
        algo_data = df[df['algorithm'] == algo]
        are_by_mem = algo_data.groupby('memory_kb')['are'].mean()
        
        if len(are_by_mem) > 1:
            variation = ((are_by_mem.max() - are_by_mem.min()) / are_by_mem.mean()) * 100
            
            print(f"{algo}:")
            print(f"  ARE @ 96KB:  {are_by_mem.get(96, 0):.4f}")
            print(f"  ARE @ 128KB: {are_by_mem.get(128, 0):.4f}")
            print(f"  ARE @ 192KB: {are_by_mem.get(192, 0):.4f}")
            print(f"  ARE @ 256KB: {are_by_mem.get(256, 0):.4f}")
            print(f"  → Variación: {variation:.2f}%")
            
            if variation > 5:
                print(f"  ✓ Sensible a memoria (variación > 5%)")
            else:
                print(f"  ⚠️  Poco sensible (variación < 5%)")
        print()

def plot_are_comparison(df, output_dir):
    """Gráfico comparativo de ARE vs Memoria"""
    fig, ax = plt.subplots(figsize=(12, 7))
    
    algorithms = df['algorithm'].unique()
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']
    
    for idx, algo in enumerate(algorithms):
        algo_data = df[df['algorithm'] == algo]
        avg_are = algo_data.groupby('memory_kb')['are'].mean()
        std_are = algo_data.groupby('memory_kb')['are'].std()
        
        ax.errorbar(avg_are.index, avg_are.values, yerr=std_are.values,
                   marker='o', linewidth=2, markersize=8, capsize=5,
                   label=algo, color=colors[idx % len(colors)])
    
    ax.set_xlabel('Memoria (KB)', fontsize=13, fontweight='bold')
    ax.set_ylabel('Average Relative Error (ARE)', fontsize=13, fontweight='bold')
    ax.set_title('ARE vs Memoria (NS-3 con Recompilación)', fontsize=15, fontweight='bold')
    ax.legend(fontsize=11, loc='best')
    ax.grid(True, alpha=0.3, linestyle='--')
    ax.set_xticks([96, 128, 192, 256])
    plt.tight_layout()
    
    output_file = output_dir / 'are_vs_memory_recompiled.png'
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"  ✓ {output_file}")
    plt.close()

def plot_memory_impact_per_algo(df, output_dir):
    """Gráfico individual por algoritmo mostrando impacto de memoria"""
    algorithms = df['algorithm'].unique()
    
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    axes = axes.flatten()
    
    for idx, algo in enumerate(algorithms):
        ax = axes[idx]
        algo_data = df[df['algorithm'] == algo]
        
        # Datos agrupados
        grouped = algo_data.groupby('memory_kb')['are'].agg(['mean', 'std', 'min', 'max'])
        
        # Barras con error
        x = grouped.index
        y = grouped['mean']
        yerr = grouped['std']
        
        bars = ax.bar(x, y, yerr=yerr, capsize=5, alpha=0.7, 
                     color=['#3498db', '#e74c3c', '#2ecc71', '#f39c12'][idx])
        
        # Anotar valores
        for i, (xi, yi) in enumerate(zip(x, y)):
            ax.text(xi, yi + yerr.iloc[i], f'{yi:.2f}', 
                   ha='center', va='bottom', fontsize=9, fontweight='bold')
        
        ax.set_xlabel('Memoria (KB)', fontsize=11)
        ax.set_ylabel('ARE', fontsize=11)
        ax.set_title(f'{algo}', fontsize=12, fontweight='bold')
        ax.set_xticks([96, 128, 192, 256])
        ax.grid(True, alpha=0.3, axis='y')
    
    plt.tight_layout()
    output_file = output_dir / 'memory_impact_per_algo.png'
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"  ✓ {output_file}")
    plt.close()

def compare_with_offline(df, output_dir):
    """Genera comparación visual con resultados offline"""
    
    # Datos offline (extraídos del análisis previo)
    offline_data = {
        'wavesketch': {'96': 0.48, '128': 0.48, '192': 0.48, '256': 0.48},
        'fourier': {'96': 2.06, '128': 1.51, '192': 0.58, '256': 0.37},
        'omniwindow': {'96': 0.78, '128': 0.78, '192': 0.76, '256': 0.78},
        'persistcms': {'96': 1.55, '128': 1.55, '192': 1.55, '256': 1.55}
    }
    
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    axes = axes.flatten()
    
    memories = [96, 128, 192, 256]
    
    for idx, algo in enumerate(['wavesketch', 'fourier', 'omniwindow', 'persistcms']):
        ax = axes[idx]
        
        # Datos NS-3
        algo_data = df[df['algorithm'] == algo]
        ns3_are = algo_data.groupby('memory_kb')['are'].mean()
        
        # Datos offline
        offline_are = [offline_data[algo][str(m)] for m in memories]
        
        # Plotear
        x = np.arange(len(memories))
        width = 0.35
        
        ax.bar(x - width/2, [ns3_are.get(m, 0) for m in memories], width, 
              label='NS-3', alpha=0.8, color='#3498db')
        ax.bar(x + width/2, offline_are, width, 
              label='Offline', alpha=0.8, color='#e74c3c')
        
        ax.set_xlabel('Memoria (KB)', fontsize=11)
        ax.set_ylabel('ARE', fontsize=11)
        ax.set_title(f'{algo}', fontsize=12, fontweight='bold')
        ax.set_xticks(x)
        ax.set_xticklabels(memories)
        ax.legend(fontsize=9)
        ax.grid(True, alpha=0.3, axis='y')
    
    plt.suptitle('Comparación: NS-3 vs Offline', fontsize=15, fontweight='bold', y=1.00)
    plt.tight_layout()
    
    output_file = output_dir / 'comparison_ns3_vs_offline.png'
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"  ✓ {output_file}")
    plt.close()

def main():
    print("=== Análisis NS-3 con Recompilación por Memoria ===\n")
    
    # Detectar archivo de entrada
    base_dir = Path(__file__).parent.parent
    input_file = base_dir / 'results_ns3_sweep_recompiled' / 'all_results.csv'
    
    if not input_file.exists():
        print(f"✗ Error: No se encuentra {input_file}")
        print("  Ejecutar primero: scripts/run_ns3_sweep_recompiled.sh")
        sys.exit(1)
    
    # Cargar datos
    df = load_data(input_file)
    
    # Análisis de sensibilidad
    analyze_memory_sensitivity(df)
    
    # Crear directorio de salida
    output_dir = base_dir / 'analysis_ns3_recompiled'
    output_dir.mkdir(exist_ok=True)
    print(f"✓ Directorio de salida: {output_dir}\n")
    
    # Generar gráficos
    print("Generando visualizaciones...")
    plot_are_comparison(df, output_dir)
    plot_memory_impact_per_algo(df, output_dir)
    compare_with_offline(df, output_dir)
    
    print(f"\n✓ Análisis completado. Resultados en: {output_dir}")

if __name__ == '__main__':
    main()
