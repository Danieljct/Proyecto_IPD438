#!/bin/bash

# Archivo de salida final
OUTPUT_FILE="benchmark_results.csv"
# Ejecutable de ns-3
EXECUTABLE="./build/fattree_benchmark"

# --- PARÁMETROS DEL EXPERIMENTO ---

# Algoritmos a probar. Incluir los cuatro algoritmos integrados en el benchmark
# (el nombre debe coincidir con lo que acepta fattree_benchmark: "wavesketch-ideal", "fourier", "omniwindow", "persistcms")
ALGORITHMS=("wavesketch-ideal" "fourier" "omniwindow" "persistcms")

# Presupuestos de memoria en KB (eje X de los gráficos)
MEMORY_KB_VALUES=(64 128 256 512 1024 1500)

# Tamaño de ventana fijo
WINDOW_US=50

echo "Iniciando barrido de benchmark (Algoritmos vs. Memoria)..."
echo "Resultados se guardarán en: $OUTPUT_FILE"

# Borrar el archivo de resultados anterior
rm -f $OUTPUT_FILE

# Escribir el encabezado en el nuevo archivo (orden esperado por visualize.py)
echo "time_s,algorithm,memory_kb,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist,energy_sim" > $OUTPUT_FILE

# Loop anidado para el barrido
for alg in "${ALGORITHMS[@]}"
do
  for mem in "${MEMORY_KB_VALUES[@]}"
  do
    echo "--- Ejecutando: Algoritmo = $alg, Memoria = ${mem}KB ---"
    
    TEMP_FILE="temp_results_${alg}_${mem}.csv"
    
    # Ejecutar la simulación con los parámetros
    ./$EXECUTABLE --algorithm=$alg --memoryKB=$mem --windowUs=$WINDOW_US --outputFile="$TEMP_FILE"
    
    if [ $? -eq 0 ]; then
      echo "Simulación completada."
      # Añadir los datos (sin el encabezado) al archivo principal
      tail -n +2 "$TEMP_FILE" >> "$OUTPUT_FILE"
      rm "$TEMP_FILE"
    else
      echo "Error en la simulación con Alg=$alg, Mem=${mem}KB. Abortando."
      exit 1
    fi
  done
done

echo "--- Barrido de benchmark completado ---"
echo "Datos listos para visualización en $OUTPUT_FILE"
