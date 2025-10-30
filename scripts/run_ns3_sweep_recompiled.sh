#!/bin/bash
# Script para ejecutar barrido de NS-3 con RECOMPILACIÓN por cada memoria
# (igual que el evaluador offline)

cd "$(dirname "$0")/.."

echo "=== Barrido NS-3 con Recompilación por Memoria ==="
echo ""

OUTPUT_DIR="results_ns3_sweep_recompiled"
mkdir -p "$OUTPUT_DIR"

ALGORITHMS=("wavesketch" "fourier" "omniwindow" "persistcms")
MEMORIES=(96 128 192 256)
SIM_TIME=10.0

echo "Algoritmos: ${ALGORITHMS[*]}"
echo "Memorias (KB): ${MEMORIES[*]}"
echo "Tiempo de simulación: ${SIM_TIME}s"
echo "Directorio de salida: $OUTPUT_DIR"
echo ""
echo "⚠️  NOTA: Cada memoria requerirá RECOMPILACIÓN completa"
echo ""

# Archivo consolidado de resultados
CONSOLIDATED="$OUTPUT_DIR/all_results.csv"
echo "time_s,algorithm,memory_kb,flow_id,packets,are,cosine_sim" > "$CONSOLIDATED"

total_runs=$((${#ALGORITHMS[@]} * ${#MEMORIES[@]}))
current_run=0

# Guardar parameter.h original
cp Utility/parameter.h Utility/parameter.h.backup

for mem in "${MEMORIES[@]}"; do
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║ Compilando con MEMORY_KB = ${mem} KB"
    echo "╚════════════════════════════════════════════════════════════╝"
    
    # Modificar parameter.h
    sed -i "s/^#define MEMORY_KB .*/#define MEMORY_KB ${mem}/" Utility/parameter.h
    
    # Verificar cambio
    grep "^#define MEMORY_KB" Utility/parameter.h
    
    # Recompilar completamente
    echo "  → Limpiando build..."
    rm -rf build/*.o build/fattree_with_sketches 2>/dev/null
    
    echo "  → Recompilando..."
    cd build
    make fattree_with_sketches 2>&1 | grep -E "(Building|Linking|Built target)" || true
    
    if [ ! -f fattree_with_sketches ]; then
        echo "  ✗ ERROR: Compilación falló para memoria ${mem}KB"
        cd ..
        continue
    fi
    
    echo "  ✓ Compilación exitosa con MEMORY_KB=${mem}"
    cd ..
    
    # Ejecutar todos los algoritmos con esta memoria
    for algo in "${ALGORITHMS[@]}"; do
        current_run=$((current_run + 1))
        echo ""
        echo "  [$current_run/$total_runs] Ejecutando: algoritmo=$algo, memoria=${mem}KB..."
        
        output_file="$OUTPUT_DIR/${algo}_${mem}kb.csv"
        
        # Ejecutar simulación
        cd build
        ./fattree_with_sketches \
            --algorithm="$algo" \
            --memoryKB="$mem" \
            --simTime="$SIM_TIME" \
            --outputFile="../$output_file" \
            2>&1 | grep -E "(ECN marks|Drops|completada)" || true
        cd ..
        
        # Consolidar resultados
        if [ -f "$output_file" ]; then
            tail -n +2 "$output_file" >> "$CONSOLIDATED"
            echo "    ✓ Guardado en: $output_file"
        else
            echo "    ✗ ERROR: No se generó $output_file"
        fi
    done
    
    echo ""
done

# Restaurar parameter.h original
echo ""
echo "Restaurando parameter.h original..."
mv Utility/parameter.h.backup Utility/parameter.h

echo ""
echo "=== Barrido completado ==="
echo "Resultados consolidados en: $CONSOLIDATED"
echo ""
echo "Resumen de archivos generados:"
ls -lh "$OUTPUT_DIR"/*.csv 2>/dev/null || echo "  (ninguno)"

# Generar estadísticas
if [ -f "$CONSOLIDATED" ] && [ $(wc -l < "$CONSOLIDATED") -gt 1 ]; then
    echo ""
    echo "=== Estadísticas por Algoritmo y Memoria ==="
    
    for algo in "${ALGORITHMS[@]}"; do
        echo ""
        echo "Algoritmo: $algo"
        for mem in "${MEMORIES[@]}"; do
            stats=$(grep ",$algo,$mem," "$CONSOLIDATED" | awk -F',' '
                BEGIN { sum_are=0; sum_cos=0; count=0; }
                { sum_are+=$6; sum_cos+=$7; count++; }
                END { 
                    if (count > 0) {
                        printf "  %dKB: %d mediciones, ARE=%.4f, Cosine=%.4f", mem, count, sum_are/count, sum_cos/count;
                    }
                }
            ' mem="$mem")
            echo "$stats"
        done
    done
fi

echo ""
echo "Para análisis detallado con gráficos, ejecutar:"
echo "  python3 scripts/analyze_ns3_results_recompiled.py"
