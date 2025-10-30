# Comparación: Evaluación Offline vs NS-3 en Tiempo Real

## Resumen Ejecutivo

| Aspecto | Evaluación Offline | NS-3 Tiempo Real |
|---------|-------------------|------------------|
| **Dataset** | websearch25.csv (1.66M paquetes, 625 flujos) | Tráfico TCP sintético (2 flujos principales) |
| **Duración** | Estático (completo) | 10 segundos simulados |
| **Flujos** | 625 flujos únicos | 2 flujos únicos |
| **Paquetes** | 1,660,000 total | ~104,024 total |
| **Mejor algoritmo** | WaveSketch (ARE=0.48) | Fourier (ARE=0.11) |

## Resultados por Algoritmo

### WaveSketch
| Métrica | Offline | NS-3 | Observación |
|---------|---------|------|-------------|
| ARE promedio | **0.48** ✓ | **289.13** ✗ | Degradación significativa en tiempo real |
| Variación con memoria | 0% (constante) | 0% (constante) | Comportamiento similar |
| Mejor memoria | Todas iguales | Todas iguales | No sensible a memoria |

**Análisis**: WaveSketch tiene excelente desempeño offline pero falla en NS-3. Posibles causas:
- Problema con captura de paquetes en tiempo real
- Error en reconstrucción con tráfico sintético
- Incompatibilidad con series temporales cortas (10s vs dataset completo)

### Fourier
| Métrica | Offline | NS-3 | Observación |
|---------|---------|------|-------------|
| ARE promedio | 0.37 @ 256KB | **0.11** ✓ | Mejor en NS-3 |
| Variación con memoria | **143.7%** (2.06→0.37) | 0% | Pierde sensibilidad a memoria |
| Mejor memoria | 256 KB | Todas iguales | No distingue en tiempo real |

**Análisis**: Fourier es el **mejor en NS-3** pero pierde la característica de mejorar con más memoria.

### OmniWindow
| Métrica | Offline | NS-3 | Observación |
|---------|---------|------|-------------|
| ARE promedio | 0.78 | **1.00** | Peor en NS-3 |
| Variación con memoria | 3.1% (mínima) | 0% | Consistente |
| Cosine Similarity | Variable | 1.0 (perfecto) | Contradicción métrica |

**Análisis**: ARE=1.0 constante con Cosine=1.0 sugiere que está devolviendo valores idénticos al ground truth pero con error unitario. Posible bug en reconstrucción.

### PersistCMS
| Métrica | Offline | NS-3 | Observación |
|---------|---------|------|-------------|
| ARE promedio | 1.55 | **0.79** ✓ | Mejor en NS-3 |
| Variación con memoria | 0% | 0% | Consistente |
| Mejor memoria | Todas iguales | Todas iguales | No sensible |

**Análisis**: PersistCMS mejora en NS-3 (50% menos error) pero sigue siendo el peor algoritmo offline.

## Hallazgos Clave

### 1. Pérdida de Sensibilidad a Memoria
**Offline**: Fourier varía 143% entre 96KB y 256KB
**NS-3**: **Todos los algoritmos son insensibles a memoria**

**Posibles causas**:
- Pocos flujos (2 vs 625) no saturan estructuras
- Duración corta (10s) no acumula suficientes datos
- Tráfico sintético menos diverso que dataset real

### 2. Inversión de Rankings

**Ranking Offline** (mejor a peor):
1. WaveSketch (ARE=0.48)
2. Fourier (ARE=0.37 @ 256KB)
3. OmniWindow (ARE=0.78)
4. PersistCMS (ARE=1.55)

**Ranking NS-3** (mejor a peor):
1. **Fourier (ARE=0.11)** ✓
2. **PersistCMS (ARE=0.79)** ✓
3. **OmniWindow (ARE=1.00)** ⚠️
4. **WaveSketch (ARE=289.13)** ✗

### 3. Características del Tráfico

| Característica | Offline (websearch25) | NS-3 (fattree) |
|----------------|----------------------|----------------|
| Tipo de flujos | HTTP, búsquedas web | TCP bulk + OnOff |
| Distribución | Heavy-tail real | Sintético uniforme |
| Tamaños de paquetes | Variable (web) | Fijo (1460 bytes) |
| Ráfagas | Aleatorio realista | Controlado (OnOff periods) |
| Flujos concurrentes | 625 activos | 2 activos |

## Posibles Problemas Identificados

### WaveSketch en NS-3
```
ARE = 289.13 (vs 0.48 offline)
```
**Hipótesis**:
1. Bug en reconstrucción temporal con ventanas de 1s
2. Algoritmo wavelet no maneja bien flujos largos (10s continuos)
3. Error en mapeo de five_tuple desde NS-3 a estructura interna

**Acción requerida**: Debug con logs detallados de `count()` y `rebuild()`

### OmniWindow en NS-3
```
ARE = 1.00 constante, Cosine = 1.00 perfecto
```
**Hipótesis**:
1. Reconstrucción devuelve valores exactos pero normalizados incorrectamente
2. Algoritmo colapsa a identidad en tráfico simple
3. Bug en cálculo de ARE (división por valor original)

**Acción requerida**: Verificar implementación de `rebuild()` y métricas

## Validación de Resultados NS-3

### Flujos Capturados
```bash
$ grep flow_id results_ns3_sweep/all_results.csv | sort -u
211110695338241  # Flujo principal 1
211110695338497  # Flujo principal 2
```
✓ Correctamente capturados 2 flujos únicos

### Paquetes Totales
```bash
104,024 paquetes en 10 segundos = ~10,400 paquetes/s
```
✓ Coherente con tráfico BulkSend + OnOff

### Estadísticas RED
```
[t=10s] ECN marks: 0, Drops: 129
```
⚠️ **0 marcas ECN** pero **129 drops** - RED no está marcando ECN correctamente
- Posible: Configuración de umbrales MinTh/MaxTh demasiado alta
- Posible: ECN no habilitado en conexiones TCP

## Conclusiones

### Para Producción
**Recomendación**: Usar **Fourier** en NS-3 (ARE=0.11)
- Mejor desempeño en tiempo real
- Estable y predecible
- No requiere tuning de memoria

### Para Investigación Offline
**Recomendación**: Usar **WaveSketch** (ARE=0.48)
- Mejor precisión con datasets completos
- Constante con memoria (bajo costo)
- Probado con 625 flujos

### Gaps a Cerrar
1. **Debugging de WaveSketch en NS-3**: ¿Por qué falla en tiempo real?
2. **Restaurar sensibilidad a memoria**: Aumentar número de flujos o duración
3. **Validar OmniWindow**: Investigar contradicción ARE=1 con Cosine=1
4. **Habilitar ECN**: Arreglar configuración RED para obtener marcas ECN

## Próximos Pasos

### Corto Plazo
1. [ ] Ejecutar NS-3 con 100 flujos en lugar de 2
2. [ ] Aumentar duración a 60 segundos
3. [ ] Habilitar logs detallados en WaveSketch
4. [ ] Verificar cálculo de ARE en OmniWindow

### Mediano Plazo
1. [ ] Replay de websearch25.csv en NS-3 (comparación directa)
2. [ ] Benchmark de overhead (CPU, latencia)
3. [ ] Topología Fat-Tree k=4 o k=8
4. [ ] Integrar con FlowMonitor de NS-3

### Largo Plazo
1. [ ] Paper comparativo online/offline
2. [ ] Implementación en P4 para switches programables
3. [ ] Dataset propio con iperf3 + caracterización

## Referencias Cruzadas
- Evaluación offline: `out_detailed_analysis/analysis_summary.txt`
- Resultados NS-3: `results_ns3_sweep/all_results.csv`
- Código offline: `offline_evaluator.cpp`
- Código NS-3: `fattree_with_sketches.cc`
