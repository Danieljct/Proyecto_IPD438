import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import sys
import os

# Archivo de entrada
INPUT_FILE = "benchmark_results_offline.csv"
# Archivo de salida
OUTPUT_PLOT_FILE = "benchmark_graphs.png"

def analyze_and_plot(df):
    """
    Analiza el DataFrame y genera los 4 gráficos de precisión.
    """
    print("--- Análisis de Resultados del Benchmark ---")
    
    # Agrupar por algoritmo y memoria, calculando la media de las métricas
    # Esto promedia los resultados de todos los flujos y todos los tiempos
    df_grouped = df.groupby(['algorithm', 'memory_kb']).agg({
        'are': 'mean',
        'cosine_sim': 'mean',
        'euclidean_dist': 'mean',
        'energy_sim': 'mean'
    }).reset_index()
    
    print("Resultados promediados:")
    print(df_grouped)
    print("------------------------------------------")

    # --- Generar los 4 Gráficos (Cuadrícula 1x4) ---
    
    # Configurar el estilo de Seaborn
    sns.set_theme(style="whitegrid")
    
    fig, axes = plt.subplots(1, 4, figsize=(16, 12))
    fig.suptitle('Análisis de Precisión de Algoritmos de Medición vs. Memoria', fontsize=20, y=1.03)

    # Asegurarnos de que memory_kb es numérico y ordenar por memoria
    df_grouped['memory_kb'] = pd.to_numeric(df_grouped['memory_kb'], errors='coerce')
    df_grouped = df_grouped.sort_values(['algorithm', 'memory_kb'])

    # Paleta de colores para los algoritmos
    algorithms = list(df_grouped['algorithm'].unique())
    palette = dict(zip(algorithms, sns.color_palette("deep", len(algorithms))))

    # Plotear explícitamente por algoritmo para controlar leyenda y estilos
    ax1 = axes[0]
    ax2 = axes[1]
    ax3 = axes[2]
    ax4 = axes[3]

    x_values = sorted(df_grouped['memory_kb'].unique())

    markers = ['o', 's', 'D', '^', 'v', 'P', 'X']
    for i, alg in enumerate(algorithms):
        alg_data = df_grouped[df_grouped['algorithm'] == alg]
        # ensure alignment to all x_values (fill missing with NaN)
        alg_series = alg_data.set_index('memory_kb')
        alg_euc = [alg_series['euclidean_dist'].get(x, float('nan')) for x in x_values]
        alg_are = [alg_series['are'].get(x, float('nan')) for x in x_values]
        alg_cos = [alg_series['cosine_sim'].get(x, float('nan')) for x in x_values]
        alg_energy = [alg_series['energy_sim'].get(x, float('nan')) for x in x_values]

        color = palette.get(alg)
        marker = markers[i % len(markers)]

        # Plot each metric on its axis
        ax1.plot(x_values, alg_euc, label=alg, marker=marker, color=color)
        ax2.plot(x_values, alg_are, label=alg, marker=marker, color=color)
        ax3.plot(x_values, alg_cos, label=alg, marker=marker, color=color)
        ax4.plot(x_values, alg_energy, label=alg, marker=marker, color=color)

    ax1.set_title('Distancia Euclidiana (Menor es mejor)')
    ax1.set_xlabel('Memoria (KB)')
    ax1.set_ylabel('Distancia Euclidiana')

    ax2.set_title('Error Relativo Promedio (ARE) (Menor es mejor)')
    ax2.set_xlabel('Memoria (KB)')
    ax2.set_ylabel('ARE')
    ax2.set_yscale('log')

    ax3.set_title('Similitud Coseno (Mayor es mejor)')
    ax3.set_xlabel('Memoria (KB)')
    ax3.set_ylabel('Similitud Coseno')
    ax3.set_ylim(0.8, 1.01)

    ax4.set_title('Similitud de Energía (Más cercano a 1 es mejor)')
    ax4.set_xlabel('Memoria (KB)')
    ax4.set_ylabel('Similitud de Energía')
    ax4.set_ylim(0.8, 1.01)

    # Mover la leyenda fuera de los gráficos: usar la leyenda del primer eje si existe
    handles, labels = ax1.get_legend_handles_labels()
    if handles:
        # Colocar la leyenda a la derecha fuera del área de trazado
        fig.legend(handles, labels, loc='center right', bbox_to_anchor=(1.15, 0.5), title="Algoritmo")
        # Si el eje 1 tenía su propia leyenda, eliminarla para evitar duplicados
        if ax1.get_legend() is not None:
            ax1.get_legend().remove()
    else:
        # Forzar que se muestre la leyenda en el primer eje si no se encontró ninguna (fallback)
        ax1.legend(title='Algoritmo', loc='best')

    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    
    # Guardar el gráfico en un archivo
    plt.savefig(OUTPUT_PLOT_FILE, bbox_inches='tight')
    print(f"Gráficos de análisis guardados en: {OUTPUT_PLOT_FILE}")
    
    plt.show() # Descomenta si quieres que se muestre en pantalla

def main():
    if not os.path.exists(INPUT_FILE):
        print(f"Error: No se encontró el archivo '{INPUT_FILE}'.")
        print("Asegúrate de ejecutar el script 'run_benchmark_sweep.sh' primero.")
        sys.exit(1)
        
    try:
        df = pd.read_csv(INPUT_FILE)
        print(f"Datos cargados exitosamente desde {INPUT_FILE}")
        if df.empty:
            print("Error: El archivo de datos está vacío.")
            sys.exit(1)
        analyze_and_plot(df)
    except Exception as e:
        print(f"Ocurrió un error al procesar el archivo: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
