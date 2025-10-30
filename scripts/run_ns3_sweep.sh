#!/bin/bash
# Script para ejecutar barrido de NS-3 con diferentes algoritmos y memorias

cd "$(dirname "$0")/.."

echo "=== Barrido de Simulaciones NS-3 con Algoritmos de Sketch ==="
echo ""

OUTPUT_DIR="results_ns3_sweep"
mkdir -p "$OUTPUT_DIR"

ALGORITHMS=("wavesketch" "fourier" "omniwindow" "persistcms")
MEMORIES=(96 128 192 256)
SIM_TIME=10.0

echo "Algoritmos: ${ALGORITHMS[*]}"
echo "Memorias (KB): ${MEMORIES[*]}"
echo "Tiempo de simulación: ${SIM_TIME}s"
echo "Directorio de salida: $OUTPUT_DIR"
echo ""

# Archivo consolidado de resultados
CONSOLIDATED="$OUTPUT_DIR/all_results.csv"
echo "time_s,algorithm,memory_kb,flow_id,packets,are,cosine_sim" > "$CONSOLIDATED"

total_runs=$((${#ALGORITHMS[@]} * ${#MEMORIES[@]}))
current_run=0

for algo in "${ALGORITHMS[@]}"; do
    for mem in "${MEMORIES[@]}"; do
        current_run=$((current_run + 1))
        echo "[$current_run/$total_runs] Ejecutando: algoritmo=$algo, memoria=${mem}KB..."
        
        output_file="$OUTPUT_DIR/${algo}_${mem}kb.csv"
        
        # Ejecutar simulación
        cd build
        ./fattree_with_sketches \
            --algorithm="$algo" \
            --memoryKB="$mem" \
            --simTime="$SIM_TIME" \
            --outputFile="../$output_file" \
            2>&1 | grep -E "(ECN marks|Drops|Flow|completada)" || true
        cd ..
        
        # Consolidar resultados (saltar encabezado)
        if [ -f "$output_file" ]; then
            tail -n +2 "$output_file" >> "$CONSOLIDATED"
            echo "  ✓ Guardado en: $output_file"
        else
            echo "  ✗ ERROR: No se generó $output_file"
        fi
        
        echo ""
    done
done

echo "=== Barrido completado ==="
echo "Resultados consolidados en: $CONSOLIDATED"
echo ""
echo "Resumen de archivos generados:"
ls -lh "$OUTPUT_DIR"/*.csv

# Generar estadísticas rápidas
echo ""
echo "=== Estadísticas por algoritmo ==="
for algo in "${ALGORITHMS[@]}"; do
    echo ""
    echo "Algoritmo: $algo"
    grep ",$algo," "$CONSOLIDATED" | awk -F',' '
        BEGIN { sum_are=0; sum_cos=0; count=0; }
        { sum_are+=$6; sum_cos+=$7; count++; }
        END { 
            if (count > 0) {
                printf "  Flujos: %d\n", count; 
                printf "  ARE promedio: %.4f\n", sum_are/count;
                printf "  Cosine Similarity promedio: %.4f\n", sum_cos/count;
            }
        }
    '
done

echo ""
echo "Para análisis detallado, ejecutar:"
echo "  python3 scripts/analyze_ns3_results.py"
