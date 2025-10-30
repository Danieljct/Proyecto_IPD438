#!/usr/bin/env python3
"""
Genera gráficos detallados mostrando cómo varía ARE con memoria para cada algoritmo.
Incluye análisis de flows individuales y distribuciones.
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys
from pathlib import Path

def load_data(csv_path):
    """Carga datos del CSV de resultados"""
    df = pd.read_csv(csv_path)
    return df

def plot_are_variation_by_algorithm(df, output_dir):
    """Gráfico de ARE vs memoria para cada algoritmo"""
    fig, axes = plt.subplots(2, 2, figsize=(15, 12))
    fig.suptitle('Variación de ARE con Memoria por Algoritmo', fontsize=16, fontweight='bold')
    
    algorithms = df['algorithm'].unique()
    colors = {'96': '#e74c3c', '128': '#f39c12', '192': '#3498db', '256': '#2ecc71'}
    
    for idx, alg in enumerate(algorithms):
        ax = axes[idx // 2, idx % 2]
        alg_data = df[df['algorithm'] == alg]
        
        # Boxplot para cada memoria
        memories = sorted(alg_data['memory_kb'].unique())
        data_by_mem = [alg_data[alg_data['memory_kb'] == mem]['are'].values for mem in memories]
        
        bp = ax.boxplot(data_by_mem, labels=[f'{m}KB' for m in memories], 
                        patch_artist=True, showfliers=True)
        
        # Colorear boxes
        for patch, mem in zip(bp['boxes'], memories):
            patch.set_facecolor(colors[str(mem)])
            patch.set_alpha(0.7)
        
        # Línea de media
        means = [alg_data[alg_data['memory_kb'] == mem]['are'].mean() for mem in memories]
        ax.plot(range(1, len(memories)+1), means, 'D-', color='black', 
                linewidth=2, markersize=8, label='Media', zorder=10)
        
        ax.set_xlabel('Memoria (KB)', fontsize=11, fontweight='bold')
        ax.set_ylabel('ARE (Average Relative Error)', fontsize=11, fontweight='bold')
        ax.set_title(f'{alg.upper()}', fontsize=13, fontweight='bold')
        ax.grid(True, alpha=0.3, linestyle='--')
        ax.legend()
        
        # Anotar valores de media
        for i, (mem, mean) in enumerate(zip(memories, means)):
            ax.text(i+1, mean, f'{mean:.3f}', ha='center', va='bottom', 
                   fontsize=9, fontweight='bold', color='darkred')
        
        # Límite del eje Y adaptativo
        max_val = alg_data['are'].quantile(0.95)  # Ignorar outliers extremos
        ax.set_ylim([-0.05 * max_val, max_val * 1.2])
    
    plt.tight_layout()
    plt.savefig(output_dir / 'are_variation_detailed.png', dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_dir / 'are_variation_detailed.png'}")
    plt.close()

def plot_improvement_heatmap(df, output_dir):
    """Heatmap mostrando mejora relativa respecto a 96KB"""
    fig, ax = plt.subplots(figsize=(10, 6))
    
    algorithms = sorted(df['algorithm'].unique())
    memories = sorted(df['memory_kb'].unique())
    
    # Calcular ARE medio por algoritmo y memoria
    pivot = df.groupby(['algorithm', 'memory_kb'])['are'].mean().unstack()
    
    # Calcular mejora relativa respecto a 96KB (%)
    improvement = pd.DataFrame()
    for col in pivot.columns:
        improvement[col] = ((pivot[96] - pivot[col]) / pivot[96] * 100).fillna(0)
    
    # Crear heatmap
    im = ax.imshow(improvement.values, cmap='RdYlGn', aspect='auto', vmin=-20, vmax=100)
    
    # Configurar ejes
    ax.set_xticks(range(len(memories)))
    ax.set_yticks(range(len(algorithms)))
    ax.set_xticklabels([f'{m} KB' for m in memories], fontsize=11)
    ax.set_yticklabels([alg.upper() for alg in algorithms], fontsize=11)
    
    # Rotar etiquetas
    plt.setp(ax.get_xticklabels(), rotation=45, ha="right", rotation_mode="anchor")
    
    # Añadir valores en cada celda
    for i in range(len(algorithms)):
        for j in range(len(memories)):
            val = improvement.values[i, j]
            color = 'white' if abs(val) > 30 else 'black'
            ax.text(j, i, f'{val:.1f}%', ha="center", va="center", 
                   color=color, fontsize=10, fontweight='bold')
    
    ax.set_title('Mejora de ARE vs Memoria Base (96KB)\n' + 
                 'Verde = Mejor (menor error), Rojo = Peor',
                 fontsize=13, fontweight='bold', pad=20)
    
    # Colorbar
    cbar = plt.colorbar(im, ax=ax)
    cbar.set_label('Mejora (%)', rotation=270, labelpad=20, fontsize=11, fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(output_dir / 'improvement_heatmap.png', dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_dir / 'improvement_heatmap.png'}")
    plt.close()

def plot_flow_samples(df, output_dir, n_flows=6):
    """Gráfico mostrando evolución de flows individuales"""
    fig, axes = plt.subplots(2, 3, figsize=(18, 10))
    fig.suptitle('Evolución de ARE con Memoria para Flows Individuales', 
                 fontsize=16, fontweight='bold')
    
    algorithms = df['algorithm'].unique()
    colors_alg = {'wavesketch-ideal': '#9b59b6', 'fourier': '#e74c3c', 
                  'omniwindow': '#3498db', 'persistcms': '#f39c12'}
    
    # Seleccionar flows con error variable
    flow_variance = df.groupby('flow_id')['are'].var()
    interesting_flows = flow_variance.nlargest(n_flows).index
    
    for idx, flow_id in enumerate(interesting_flows):
        if idx >= 6:
            break
        ax = axes[idx // 3, idx % 3]
        
        flow_data = df[df['flow_id'] == flow_id]
        
        for alg in algorithms:
            alg_flow = flow_data[flow_data['algorithm'] == alg].sort_values('memory_kb')
            if len(alg_flow) > 0:
                ax.plot(alg_flow['memory_kb'], alg_flow['are'], 
                       'o-', label=alg, linewidth=2, markersize=8,
                       color=colors_alg.get(alg, 'gray'), alpha=0.8)
        
        # Configuración
        packets = flow_data['packets'].iloc[0]
        ax.set_xlabel('Memoria (KB)', fontsize=10, fontweight='bold')
        ax.set_ylabel('ARE', fontsize=10, fontweight='bold')
        ax.set_title(f'Flow {flow_id} ({packets} paquetes)', fontsize=11, fontweight='bold')
        ax.grid(True, alpha=0.3)
        ax.legend(fontsize=8, loc='best')
        ax.set_xticks([96, 128, 192, 256])
    
    plt.tight_layout()
    plt.savefig(output_dir / 'flow_samples_evolution.png', dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_dir / 'flow_samples_evolution.png'}")
    plt.close()

def plot_algorithm_comparison(df, output_dir):
    """Comparación directa entre algoritmos para cada memoria"""
    memories = sorted(df['memory_kb'].unique())
    
    fig, axes = plt.subplots(2, 2, figsize=(16, 12))
    fig.suptitle('Comparación de Algoritmos por Memoria', fontsize=16, fontweight='bold')
    
    algorithms = sorted(df['algorithm'].unique())
    colors = plt.cm.Set3(np.linspace(0, 1, len(algorithms)))
    
    for idx, mem in enumerate(memories):
        ax = axes[idx // 2, idx % 2]
        
        mem_data = df[df['memory_kb'] == mem]
        data_by_alg = [mem_data[mem_data['algorithm'] == alg]['are'].values 
                       for alg in algorithms]
        
        bp = ax.boxplot(data_by_alg, labels=[alg.split('-')[0].upper()[:8] for alg in algorithms],
                        patch_artist=True, showfliers=False)
        
        # Colorear
        for patch, color in zip(bp['boxes'], colors):
            patch.set_facecolor(color)
            patch.set_alpha(0.7)
        
        # Media
        means = [mem_data[mem_data['algorithm'] == alg]['are'].mean() for alg in algorithms]
        ax.plot(range(1, len(algorithms)+1), means, 'D-', color='red', 
                linewidth=2.5, markersize=10, label='Media', zorder=10)
        
        # Anotar medias
        for i, (alg, mean) in enumerate(zip(algorithms, means)):
            ax.text(i+1, mean, f'{mean:.2f}', ha='center', va='bottom',
                   fontsize=9, fontweight='bold', color='darkred')
        
        ax.set_ylabel('ARE', fontsize=11, fontweight='bold')
        ax.set_title(f'Memoria: {mem} KB', fontsize=13, fontweight='bold')
        ax.grid(True, alpha=0.3, axis='y')
        ax.legend()
        
        # Límite adaptativo
        max_val = mem_data['are'].quantile(0.90)
        ax.set_ylim([0, max_val * 1.3])
    
    plt.tight_layout()
    plt.savefig(output_dir / 'algorithm_comparison.png', dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_dir / 'algorithm_comparison.png'}")
    plt.close()

def plot_statistics_table(df, output_dir):
    """Tabla con estadísticas resumidas"""
    fig, ax = plt.subplots(figsize=(14, 8))
    ax.axis('tight')
    ax.axis('off')
    
    # Calcular estadísticas
    stats = df.groupby(['algorithm', 'memory_kb'])['are'].agg([
        ('Media', 'mean'),
        ('Mediana', 'median'),
        ('Desv.Std', 'std'),
        ('Min', 'min'),
        ('Max', 'max'),
        ('Q25', lambda x: x.quantile(0.25)),
        ('Q75', lambda x: x.quantile(0.75))
    ]).round(4)
    
    # Resetear índice para tabla
    stats_reset = stats.reset_index()
    
    # Crear tabla
    table = ax.table(cellText=stats_reset.values,
                    colLabels=stats_reset.columns,
                    cellLoc='center',
                    loc='center',
                    colWidths=[0.12, 0.08] + [0.1]*7)
    
    table.auto_set_font_size(False)
    table.set_fontsize(9)
    table.scale(1, 2)
    
    # Colorear header
    for i in range(len(stats_reset.columns)):
        table[(0, i)].set_facecolor('#3498db')
        table[(0, i)].set_text_props(weight='bold', color='white')
    
    # Colorear filas alternadas
    for i in range(1, len(stats_reset) + 1):
        color = '#ecf0f1' if i % 2 == 0 else 'white'
        for j in range(len(stats_reset.columns)):
            table[(i, j)].set_facecolor(color)
    
    plt.title('Estadísticas Detalladas de ARE por Algoritmo y Memoria',
             fontsize=14, fontweight='bold', pad=20)
    
    plt.savefig(output_dir / 'statistics_table.png', dpi=300, bbox_inches='tight')
    print(f"✓ Guardado: {output_dir / 'statistics_table.png'}")
    plt.close()

def generate_summary_report(df, output_dir):
    """Genera reporte de texto con resumen"""
    report_path = output_dir / 'analysis_summary.txt'
    
    with open(report_path, 'w') as f:
        f.write("="*80 + "\n")
        f.write("REPORTE DE ANÁLISIS: Variación de ARE con Memoria\n")
        f.write("="*80 + "\n\n")
        
        # Estadísticas generales
        f.write("1. RESUMEN GENERAL\n")
        f.write("-" * 80 + "\n")
        f.write(f"Total de flows evaluados: {df['flow_id'].nunique()}\n")
        f.write(f"Total de paquetes procesados: {df['packets'].sum()}\n")
        f.write(f"Algoritmos evaluados: {', '.join(df['algorithm'].unique())}\n")
        f.write(f"Memorias evaluadas: {', '.join(map(str, sorted(df['memory_kb'].unique())))} KB\n")
        f.write(f"Total de evaluaciones: {len(df)}\n\n")
        
        # ARE promedio por algoritmo y memoria
        f.write("2. ARE PROMEDIO POR ALGORITMO Y MEMORIA\n")
        f.write("-" * 80 + "\n")
        pivot = df.groupby(['algorithm', 'memory_kb'])['are'].mean().unstack()
        f.write(pivot.to_string())
        f.write("\n\n")
        
        # Variación de ARE (std) por algoritmo
        f.write("3. VARIABILIDAD DE ARE (Desviación Estándar) POR ALGORITMO\n")
        f.write("-" * 80 + "\n")
        std_pivot = df.groupby(['algorithm', 'memory_kb'])['are'].std().unstack()
        f.write(std_pivot.to_string())
        f.write("\n\n")
        
        # Análisis de variación
        f.write("4. ANÁLISIS DE VARIACIÓN CON MEMORIA\n")
        f.write("-" * 80 + "\n")
        for alg in df['algorithm'].unique():
            alg_data = df[df['algorithm'] == alg]
            means = alg_data.groupby('memory_kb')['are'].mean()
            variation = means.max() - means.min()
            pct_variation = (variation / means.mean() * 100) if means.mean() > 0 else 0
            
            f.write(f"\n{alg.upper()}:\n")
            f.write(f"  - ARE mínimo: {means.min():.4f} (en {means.idxmin()} KB)\n")
            f.write(f"  - ARE máximo: {means.max():.4f} (en {means.idxmax()} KB)\n")
            f.write(f"  - Variación absoluta: {variation:.4f}\n")
            f.write(f"  - Variación relativa: {pct_variation:.2f}%\n")
            
            if pct_variation < 5:
                f.write(f"  ⚠️  BAJO IMPACTO: Este algoritmo NO varía significativamente con memoria\n")
            else:
                f.write(f"  ✓ ALTO IMPACTO: Este algoritmo SÍ se beneficia del aumento de memoria\n")
        
        f.write("\n\n")
        
        # Ranking de algoritmos por memoria
        f.write("5. RANKING DE ALGORITMOS (menor ARE = mejor)\n")
        f.write("-" * 80 + "\n")
        for mem in sorted(df['memory_kb'].unique()):
            f.write(f"\nMemoria: {mem} KB\n")
            ranking = df[df['memory_kb'] == mem].groupby('algorithm')['are'].mean().sort_values()
            for rank, (alg, are) in enumerate(ranking.items(), 1):
                f.write(f"  {rank}. {alg:20s} → ARE = {are:.4f}\n")
    
    print(f"✓ Guardado: {report_path}")

def main():
    if len(sys.argv) < 2:
        print("Uso: python generate_detailed_plots.py <results.csv> [output_dir]")
        sys.exit(1)
    
    csv_path = Path(sys.argv[1])
    output_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path('out_detailed_analysis')
    
    if not csv_path.exists():
        print(f"Error: No se encuentra {csv_path}")
        sys.exit(1)
    
    output_dir.mkdir(exist_ok=True)
    print(f"\n{'='*60}")
    print(f"Generando análisis detallado de {csv_path}")
    print(f"Salida en: {output_dir}")
    print(f"{'='*60}\n")
    
    # Cargar datos
    df = load_data(csv_path)
    print(f"Datos cargados: {len(df)} filas, {df['flow_id'].nunique()} flows\n")
    
    # Generar gráficos
    print("Generando gráficos...")
    plot_are_variation_by_algorithm(df, output_dir)
    plot_improvement_heatmap(df, output_dir)
    plot_flow_samples(df, output_dir)
    plot_algorithm_comparison(df, output_dir)
    plot_statistics_table(df, output_dir)
    
    # Generar reporte
    print("\nGenerando reporte de texto...")
    generate_summary_report(df, output_dir)
    
    print(f"\n{'='*60}")
    print(f"✓ Análisis completo. Ver resultados en: {output_dir}/")
    print(f"{'='*60}\n")

if __name__ == "__main__":
    main()
