# Simulación Fat-Tree con Algoritmos de Sketch en NS-3

## Descripción

Este módulo integra los algoritmos de medición de flujos (WaveSketch, Fourier, OmniWindow, PersistCMS) en una simulación NS-3 de red Fat-Tree con ECN (Explicit Congestion Notification).

## Arquitectura

```
┌──────────────────────────────────────────┐
│     NS-3 Fat-Tree Topology               │
│  ┌────┐     ┌────┐     ┌────┐     ┌────┐ │
│  │ H0 │─────│ S0 │─────│ S1 │─────│ H3 │ │
│  └────┘     └────┘     └────┘     └────┘ │
│  ┌────┐       │           │       ┌────┐ │
│  │ H1 │───────┘           └───────│ H2 │ │
│  └────┘                           └────┘ │
└──────────────────────────────────────────┘
              │
              ↓
    ┌─────────────────────┐
    │ FlowMonitorAgent    │
    │  - Captura paquetes │
    │  - Alimenta sketch  │
    │  - Reconstruye      │
    │  - Calcula métricas │
    └─────────────────────┘
              │
              ↓
      ┌───────────────┐
      │ Sketch Algo   │
      │ • wavesketch  │
      │ • fourier     │
      │ • omniwindow  │
      │ • persistcms  │
      └───────────────┘
```

## Componentes

### 1. `fattree_with_sketches.cc`

Simulador NS-3 que integra:
- **Topología**: Fat-tree con 4 hosts, 2 switches
- **Control de congestión**: RED queues con ECN
- **Tráfico**: TCP BulkSend + OnOff para generar congestión
- **Monitoreo**: FlowMonitorAgent captura paquetes y alimenta algoritmos

### 2. `FlowMonitorAgent`

Clase que:
- Captura paquetes recibidos (`OnPacketReceived`)
- Mantiene ground truth de series temporales
- Alimenta algoritmo de sketch seleccionado (`count()`)
- Realiza reconstrucción periódica (`rebuild()`)
- Calcula métricas: ARE, Cosine Similarity
- Exporta resultados a CSV

### 3. Scripts de automatización

- `run_ns3_sweep.sh`: Ejecuta barrido con todos los algoritmos y memorias
- `analyze_ns3_results.py`: Genera visualizaciones y estadísticas

## Compilación

```bash
cd build
cmake ..
make fattree_with_sketches
```

## Uso

### Ejecución individual

```bash
cd build
./fattree_with_sketches \
  --algorithm=wavesketch \
  --memoryKB=128 \
  --simTime=10.0 \
  --windowUs=1000000 \
  --outputFile=results.csv
```

**Parámetros:**
- `--algorithm`: Algoritmo de sketch (`wavesketch`, `fourier`, `omniwindow`, `persistcms`)
- `--memoryKB`: Memoria del sketch en KB (default: 256)
- `--simTime`: Tiempo de simulación en segundos (default: 10.0)
- `--windowUs`: Ventana temporal en microsegundos (default: 1000000 = 1s)
- `--outputFile`: Archivo CSV de salida (default: sketch_results.csv)

### Barrido completo

```bash
./scripts/run_ns3_sweep.sh
```

Ejecuta todas las combinaciones:
- Algoritmos: wavesketch, fourier, omniwindow, persistcms
- Memorias: 96, 128, 192, 256 KB
- Tiempo: 10 segundos por simulación

**Salida:**
```
results_ns3_sweep/
├── wavesketch_96kb.csv
├── wavesketch_128kb.csv
├── ...
├── persistcms_256kb.csv
└── all_results.csv          # Consolidado
```

### Análisis de resultados

```bash
python3 scripts/analyze_ns3_results.py
```

**Genera:**
```
analysis_ns3/
├── are_vs_memory.png         # ARE vs memoria por algoritmo
├── cosine_vs_memory.png      # Cosine similarity vs memoria
├── heatmap_are.png           # Heatmap de ARE
├── temporal_evolution.png    # Evolución temporal
└── summary_statistics.csv    # Estadísticas detalladas
```

## Formato de salida CSV

```csv
time_s,algorithm,memory_kb,flow_id,packets,are,cosine_sim
1.0,wavesketch,128,12345678,150,0.48,0.95
2.0,wavesketch,128,12345678,320,0.51,0.93
...
```

**Campos:**
- `time_s`: Tiempo de análisis (segundos desde inicio)
- `algorithm`: Nombre del algoritmo
- `memory_kb`: Memoria configurada
- `flow_id`: Identificador único del flujo
- `packets`: Total de paquetes capturados en el flujo
- `are`: Average Relative Error
- `cosine_sim`: Cosine Similarity

## Métricas

### Average Relative Error (ARE)
```
ARE = (1/n) * Σ |original[i] - reconstructed[i]| / original[i]
```
- **Más bajo es mejor**
- Mide error relativo promedio en la reconstrucción

### Cosine Similarity
```
cosine = (A · B) / (||A|| * ||B||)
```
- **Más cercano a 1 es mejor**
- Mide similitud entre vectores original y reconstruido

## Comparación con Evaluación Offline

| Aspecto | Offline (websearch25.csv) | NS-3 (fattree_with_sketches) |
|---------|---------------------------|------------------------------|
| **Entrada** | CSV estático (1.66M paquetes) | Tráfico simulado en tiempo real |
| **Topología** | N/A | Fat-tree con ECN |
| **Realismo** | Datos reales del paper | Tráfico sintético (BulkSend+OnOff) |
| **Control** | Repetible exacto | Variación por seeds de NS-3 |
| **Escala** | 625 flujos fijos | Pocos flujos con alto throughput |

## Resultados Esperados (basados en evaluación offline)

### WaveSketch
- **ARE constante** (~0.48) para todas las memorias
- Mejor desempeño en memorias bajas (<192 KB)

### Fourier
- **ARE varía significativamente** (2.06 @ 96KB → 0.37 @ 256KB)
- Mejora 143% con incremento de memoria
- Mejor desempeño en alta memoria (256 KB)

### OmniWindow
- **ARE casi constante** (~0.78, variación 3.1%)
- Poco sensible a cambios de memoria

### PersistCMS
- **ARE constante** (~1.55)
- Mayor error que otros algoritmos

## Debugging

### Ver estadísticas de simulación
```bash
./fattree_with_sketches --simTime=5.0 2>&1 | grep -E "(ECN marks|Drops|Flow)"
```

### Verificar que algoritmos están capturando
```bash
tail -f build/sketch_results.csv
```

### Compilación con logs detallados
```cpp
LogComponentEnable("FatTreeWithSketches", LOG_LEVEL_ALL);
```

## Extensiones Posibles

1. **Más tráfico**: Agregar UDP, iperf3-style traffic
2. **Topologías mayores**: Aumentar a k=4 o k=8 fat-tree
3. **Traces reales**: Replay de PCAP en lugar de tráfico sintético
4. **Comparación online/offline**: Usar mismo trace en ambas evaluaciones
5. **Overhead**: Medir tiempo de CPU y latencia agregada

## Notas Técnicas

- **Callback de captura**: Conectado a `PacketSink/Rx` para capturar paquetes recibidos
- **Identificación de flujos**: Hash de IP:puerto de origen
- **Ventanas temporales**: Buckets de 1 segundo por defecto
- **Reconstrucción**: Se llama periódicamente con `AnalyzeAndReport()`
- **Flush**: Algoritmos hacen flush antes de rebuild

## Referencias

- **Paper original**: uMon-WaveSketch (ver `../uMon-WaveSketch/README.md`)
- **NS-3 docs**: https://www.nsnam.org/docs/
- **Evaluación offline**: `../offline_evaluator.cpp`
- **Resultados offline**: `../out_detailed_analysis/`
