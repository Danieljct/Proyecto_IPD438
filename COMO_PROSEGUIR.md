# 🎯 GUÍA RÁPIDA: Cómo Proseguir

## ✅ Estado Actual (29 Oct 2025)

**BUENAS NOTICIAS**: El sistema está completamente operativo y ejecutando el barrido completo.

### Lo que YA está funcionando:
- ✅ Código compilado sin errores
- ✅ Evaluador offline ejecutándose sobre dataset completo (1.6M líneas)
- ✅ Script de visualización listo
- ✅ Barrido de memoria en progreso (2/6 completado)

## 🚀 Próximos Pasos INMEDIATOS

### 1. Esperar que termine el barrido (~5 minutos restantes)

```bash
# Monitorear progreso
watch -n 5 'tail -20 benchmark_results_offline_full.csv'
```

### 2. Una vez termine, generar gráficos finales

```bash
# Activar entorno virtual
source .venv/bin/activate

# Generar gráficos
python scripts/visualize_results.py \
    benchmark_results_offline_full.csv \
    out_plots_full

# Ver gráficos
ls -lh out_plots_full/
```

### 3. Analizar resultados

```bash
# Ver estadísticas
cat out_plots_full/summary_stats.csv

# Abrir gráficos
xdg-open out_plots_full/are_vs_memory.png
xdg-open out_plots_full/metrics_summary.png
```

## 📊 Gráficos que Obtendrás

1. **are_vs_memory.png** - Comparación de todos los algoritmos (como Fig 11 del paper)
2. **are_vs_memory_<algoritmo>.png** - Individual con barras de error
3. **metrics_summary.png** - 4 subplots con todas las métricas
4. **summary_stats.csv** - Tabla con estadísticas agregadas

## 🎓 Qué Buscar en los Resultados

### ✅ Resultados Esperados (según el paper):
- **WaveSketch ARE < Fourier/OmniWindow/PersistCMS**
- La diferencia es MÁS pronunciada con **poca memoria** (8-32 KB)
- **Cosine similarity**: WaveSketch ~0.95+, otros ~0.80-0.90
- **Energy similarity**: WaveSketch ~0.90+, otros ~0.70-0.85

### ⚠️ Si los resultados son raros:
1. **Todos tienen ARE=1.0**: Dataset muy pequeño o todos los flows son triviales
2. **Todos iguales**: Ventanas muy grandes (prueba reducir windowUs)
3. **WaveSketch peor**: Error en implementación o parámetros incorrectos

## 🔧 Si Necesitas Re-ejecutar

### Cambiar parámetros del barrido:

```bash
# Edita scripts/run_full_sweep.sh línea 4:
MEMORIES=(8 16 32 64 128 256 512 1024)  # Añadir más memorias

# Re-ejecutar
./scripts/run_full_sweep.sh websearch25.csv
```

### Usar dataset alternativo:

```bash
# Probar con hadoop15.csv
./scripts/run_full_sweep.sh hadoop15.csv

# Generar gráficos
source .venv/bin/activate
python scripts/visualize_results.py \
    benchmark_results_offline_full.csv \
    out_plots_hadoop
```

### Ejecutar con ventanas más pequeñas:

```bash
# Ventanas de 100ms (más granularidad)
./offline_evaluator websearch25.csv \
    --memories=64 \
    --windowUs=100000 \
    --output=results_100ms.csv

source .venv/bin/activate
python scripts/visualize_results.py results_100ms.csv out_plots_100ms
```

## 📝 Para el Paper/Reporte

### Sección de Resultados:

```markdown
## Resultados Experimentales

### Setup
- **Dataset**: websearch25.csv (1.6M paquetes, 625 flows)
- **Ventana temporal**: 1 segundo (1000000 μs)
- **Memorias evaluadas**: 8, 16, 32, 64, 128, 256 KB
- **Algoritmos**: WaveSketch-Ideal, Fourier, OmniWindow, PersistCMS

### Métricas
- Average Relative Error (ARE)
- Cosine Similarity
- Euclidean Distance
- Energy Similarity

### Hallazgos Clave
[INSERTAR después de ver gráficos]
- WaveSketch alcanza ARE de X% con 64KB vs Y% de Fourier
- La ventaja de WaveSketch es Z% mayor con presupuestos bajos (<32KB)
- Todos los algoritmos convergen con >256KB de memoria
```

### Figuras para incluir:
1. `are_vs_memory.png` - Como Fig 11 del paper
2. `metrics_summary.png` - Resumen de 4 métricas
3. `are_vs_memory_wavesketch_ideal.png` - Detalle WaveSketch

## 🎯 Objetivo Final

Demostrar que **replicaste exitosamente** los resultados del paper μMON (SIGCOMM '24):
- ✅ Implementación correcta de los algoritmos
- ✅ Evaluación sobre datasets reales (websearch25)
- ✅ Gráficos comparativos ARE vs memoria
- ✅ Validación de superioridad de WaveSketch

## ⏰ Timeline

| Tiempo | Tarea |
|--------|-------|
| +5 min | Esperar fin del barrido |
| +2 min | Generar gráficos |
| +10 min | Analizar resultados y escribir hallazgos |
| **Total: ~20 minutos** | **Paper con resultados listos** |

## 🆘 Si Algo Sale Mal

### El barrido se cuelga/crashea:
```bash
# Cancelar con Ctrl+C
# Limpiar
rm -f temp_mem_*.csv

# Probar con muestra más pequeña primero
head -50000 websearch25.csv > muestra_50k.csv
./scripts/run_full_sweep.sh muestra_50k.csv
```

### Error "No module pandas":
```bash
source .venv/bin/activate
# Si no existe el venv:
python3 -m venv .venv
source .venv/bin/activate
pip install pandas matplotlib numpy
```

### Gráficos no se generan:
```bash
# Verificar que el CSV tiene datos
wc -l benchmark_results_offline_full.csv
head benchmark_results_offline_full.csv

# Debug del script
python scripts/visualize_results.py \
    benchmark_results_offline_full.csv \
    out_plots_full 2>&1 | tee viz_log.txt
```

---

## 🎉 ¡Estás a MINUTOS de tener resultados completos!

**TL;DR**: 
1. Espera 5 minutos
2. Ejecuta: `source .venv/bin/activate && python scripts/visualize_results.py benchmark_results_offline_full.csv out_plots_full`
3. Abre: `out_plots_full/are_vs_memory.png`
4. **¡PROFIT!** 🎊
