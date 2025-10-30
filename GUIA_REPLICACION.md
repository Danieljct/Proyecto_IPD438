# Guía Completa de Replicación de Resultados

## 📁 Estructura del Proyecto

```
Proyecto_IPD438/
├── offline_evaluator.cpp       # Evaluador offline de algoritmos WaveSketch
├── websearch25.csv             # Dataset del paper (33 MB, 1.6M líneas)
├── hadoop15.csv                # Dataset alternativo (20 MB)
├── data_sample_5k.csv          # Muestra de 5000 líneas para pruebas
├── CMakeLists.txt              # Build configuration
├── scripts/
│   ├── visualize_results.py    # Generador de gráficos
│   ├── run_full_sweep.sh       # Barrido completo de memoria
│   └── run_chunked_evaluation.sh  # Procesamiento por chunks (si OOM)
├── Wavelet/                    # Implementación WaveSketch
├── Fourier/                    # Implementación Fourier
├── OmniWindow/                 # Implementación OmniWindow
├── PersistCMS/                 # Implementación PersistCMS
└── Utility/                    # Headers y pffft.c (requerido por Fourier)
```

## 🚀 Inicio Rápido

### 1. Compilar el Evaluador

```bash
mkdir -p build && cd build
cmake ..
make offline_evaluator
cd ..
```

### 2. Ejecutar Prueba Rápida (2-3 minutos)

```bash
# Crear muestra pequeña
head -5000 websearch25.csv > data_sample_5k.csv

# Ejecutar evaluador en la muestra
./offline_evaluator data_sample_5k.csv \
    --memories=64 \
    --output=benchmark_results_test.csv

# Generar gráficos
source .venv/bin/activate
python scripts/visualize_results.py \
    benchmark_results_test.csv \
    out_plots_test
```

### 3. Ejecutar Barrido Completo (~7 minutos)

```bash
# Barrido automático sobre websearch25.csv
./scripts/run_full_sweep.sh websearch25.csv

# Generar gráficos finales
source .venv/bin/activate
python scripts/visualize_results.py \
    benchmark_results_offline_full.csv \
    out_plots_full
```

## 📊 Opciones del Evaluador

```bash
./offline_evaluator <input.csv> [opciones]

Opciones:
  --memories=8,16,32,64,128,256    # Presupuestos de memoria en KB
  --output=resultados.csv          # Archivo de salida
  --windowUs=1000000               # Tamaño de ventana en microsegundos
  --per-packet                     # Modo per-packet (⚠️ alto uso de memoria)
  --inspect=44,243,513             # Inspeccionar flows específicos
```

## ⚙️ Algoritmos Evaluados

1. **wavesketch-ideal** - WaveSketch (versión C++ completa)
2. **fourier** - Transformada de Fourier
3. **omniwindow** - OmniWindow baseline
4. **persistcms** - PersistentCMS baseline

## 📈 Métricas Calculadas

- **ARE** (Average Relative Error) - ↓ menor es mejor
- **Cosine Similarity** - ↑ mayor es mejor  
- **Euclidean Distance** - ↓ menor es mejor
- **Energy Similarity** - ↑ mayor es mejor

## 🖼️ Gráficos Generados

El script `visualize_results.py` genera:

1. `are_vs_memory.png` - ARE vs Memoria (comparación todos los algoritmos)
2. `are_vs_memory_<algoritmo>.png` - ARE vs Memoria (por algoritmo individual)
3. `metrics_summary.png` - Resumen de todas las métricas (4 subplots)
4. `summary_stats.csv` - Estadísticas agregadas en CSV

## 🔧 Solución de Problemas

### Problema: OOM (Out of Memory) / PC se cuelga

**Causa**: Dataset muy grande + modo `--per-packet` expande millones de eventos en RAM.

**Soluciones**:
1. **Usar el modo normal** (sin `--per-packet`): más rápido y seguro
2. **Procesar por chunks**:
   ```bash
   ./scripts/run_chunked_evaluation.sh websearch25.csv output.csv "64,128" 10000
   ```
3. **Usar muestra más pequeña**:
   ```bash
   head -10000 websearch25.csv > muestra.csv
   ```

### Problema: Error de compilación "pffft.c not found"

**Solución**:
```bash
# Copiar Utility desde el repo de los autores
cp -r /ruta/a/uMon-WaveSketch/cpp_version/Utility .
```

### Problema: "ModuleNotFoundError: No module named 'pandas'"

**Solución**:
```bash
# Activar entorno virtual
source .venv/bin/activate

# O instalar dependencias
pip install pandas matplotlib numpy
```

## 📝 Formato de Salida CSV

```csv
time_s,algorithm,memory_kb,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist,energy_sim
0,wavesketch-ideal,64,27,5461,1000000,943,0.123,0.987,15.2,0.954
...
```

## 🎯 Replicación del Paper

Para replicar las figuras del paper (Fig 11, 12):

1. **Ejecutar barrido de memoria** (8, 16, 32, 64, 128, 256 KB)
2. **Generar gráficos** con `visualize_results.py`
3. **Comparar curvas** de ARE vs memoria entre algoritmos

### Resultados Esperados

- **WaveSketch** debe tener ARE más bajo que los baselines (Fourier, OmniWindow, PersistCMS)
- La ventaja es más pronunciada con **poco presupuesto de memoria** (<64 KB)
- **Cosine similarity** debe ser ~0.95+ para WaveSketch
- **Energy similarity** debe ser ~0.90+ para WaveSketch

## 📚 Referencias

- Paper: "μΜοΝ: Empowering Microsecond-level Network Monitoring with Wavelets" (SIGCOMM '24)
- Dataset: websearch25.csv del repositorio uMon-WaveSketch
- Implementación: cpp_version/ del repo oficial

## ⏱️ Tiempos de Ejecución Estimados

| Operación | Tiempo | Notas |
|-----------|--------|-------|
| Compilación | ~30 seg | Primera vez |
| Prueba muestra (5k líneas) | ~5 seg | 1 memoria |
| Dataset completo (1 memoria) | ~70 seg | websearch25.csv |
| Barrido completo (6 memorias) | ~7 min | Sin per-packet |
| Con modo per-packet | ⚠️ OOM | No recomendado para dataset completo |

## 🎓 Notas Técnicas

### ¿Por qué NO usar `--per-packet`?

El modo `--per-packet` expande cada ventana agregada en eventos individuales:
- Ventana con 1000 paquetes → 1000 eventos en memoria
- 1.6M líneas → potencialmente 100M+ eventos → varios GB de RAM

**Recomendación**: Use el modo normal (agregado por ventana) que es:
- ✅ 10x más rápido
- ✅ Usa ~100 MB RAM en lugar de GB
- ✅ Da resultados equivalentes para métricas agregadas

### Diferencias con el Paper

- **Paper**: Usa trazas de ns-3 con timestamps exactos (nanosegundos)
- **Nuestro evaluador**: Procesa CSV offline con timestamps en microsegundos
- **Implicación**: Los valores absolutos pueden diferir, pero las **tendencias relativas** (WaveSketch vs baselines) deben coincidir

---

**¿Preguntas o problemas?** Revisa los logs en `build/` o ejecuta con `2>&1 | tee log.txt`
