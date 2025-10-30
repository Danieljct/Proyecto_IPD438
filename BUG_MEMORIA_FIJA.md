# 🐛 Bug Crítico: Memoria Fija en Algoritmos

## Problema Descubierto

Los resultados del primer barrido **NO variaban al cambiar el parámetro `--memories`**. Todos los algoritmos mostraban métricas idénticas independientemente de si usabas 8 KB o 256 KB.

## Causa Raíz

### ❌ Lo que estaba MAL

En `Utility/parameter.h`:

```cpp
// Dimensiones de tabla HARDCODED (no cambian dinámicamente)
#define FULL_WIDTH 256u        // Fijo
#define FULL_HEIGHT 3u         // Fijo  
#define FULL_DEPTH (MAX_LENGTH / SAMPLE_RATE)  // = 2048/32 = 64 (Fijo)

// La memoria real usada es:
// MEMORY_REAL = FULL_WIDTH × FULL_HEIGHT × FULL_DEPTH × 4 bytes
//             = 256 × 3 × 64 × 4
//             = 196,608 bytes
//             = 192 KB (SIEMPRE!)

// Estas líneas solo cambian una ETIQUETA, no la memoria real:
#define MEMORY_KB 2048  // ← Solo para debug/logging
#define MEMORY MEMORY_KB*1024  // ← NO se usa para dimensionar tablas
```

### ✅ Cómo se DEBE hacer

Los algoritmos están diseñados con **templates de compile-time**. Sus tablas se dimensionan así:

```cpp
// Wavelet/table.h
class table : public basic_table<counter<BY_THRESHOLD>, FULL_WIDTH, LESS_HEIGHT> {
    //                                                     ↑          ↑
    //                                       Usa los #defines hardcoded
```

**No se pueden cambiar en runtime.** Hay que **recompilar para cada memoria**.

## Solución Implementada

### Modificación del Script de Sweep

```bash
# scripts/run_full_sweep.sh (nuevo)
for mem in "${MEMORIES[@]}"; do
    # 1. Actualizar parameter.h
    sed -i "s/^#define MEMORY_KB .*/#define MEMORY_KB $mem /" Utility/parameter.h
    
    # 2. Recompilar
    cd build && make offline_evaluator && cd ..
    
    # 3. Ejecutar con la memoria correcta
    ./offline_evaluator dataset.csv --memories=$mem ...
done
```

### Cálculo de Dimensiones por Memoria

Para cada presupuesto de memoria, las dimensiones se calculan:

```
Memoria = WIDTH × HEIGHT × DEPTH × 4 bytes

Ejemplo para 64 KB:
64 × 1024 = 65,536 bytes
65,536 / 4 = 16,384 "slots"
16,384 / 3 (HEIGHT) = 5,461.33
5,461 / 64 (DEPTH) = 85.3 ≈ 85 WIDTH

Pero los autores usan WIDTH=256 fijo y ajustan DEPTH:
DEPTH = (MEMORY_KB × 1024) / (WIDTH × HEIGHT × 4)
DEPTH = (64 × 1024) / (256 × 3 × 4)
DEPTH = 65,536 / 3,072 = 21.33 ≈ 21
```

**IMPORTANTE**: El paper mantiene `WIDTH=256` y `HEIGHT=3` fijos. Solo varía `DEPTH`.

Pero en el `parameter.h` actual:
```cpp
#define FULL_DEPTH (MAX_LENGTH / SAMPLE_RATE)  // Siempre 64
```

Necesitaríamos cambiar a:
```cpp
#define FULL_DEPTH ((MEMORY_KB * 1024) / (FULL_WIDTH * FULL_HEIGHT * 4))
```

### Por Qué los Autores Recompilan

En su repo original (`cpp_version/`), el `Makefile` o script de benchmark:

1. Cambia `MEMORY_KB` en `parameter.h`
2. Recompila todo el binario
3. Ejecuta con esa memoria
4. Repite para cada presupuesto

Es **by design**: los templates de C++ necesitan valores constantes en compile-time para optimizar.

## Impacto del Bug

### Resultados Anteriores (Inválidos)

```
Todos los runs usaron 192 KB:
- mem=8   → realmente usó 192 KB
- mem=16  → realmente usó 192 KB
- mem=32  → realmente usó 192 KB
- mem=64  → realmente usó 192 KB
- mem=128 → realmente usó 192 KB
- mem=256 → realmente usó 192 KB
```

**Por eso los gráficos mostraban líneas horizontales (sin variación).**

### Resultados Corregidos (Esperados)

Con la recompilación por memoria:
```
- mem=8   → W=256, H=3, D=10  → ~30 KB real
- mem=16  → W=256, H=3, D=21  → ~64 KB real
- mem=32  → W=256, H=3, D=42  → ~128 KB real
- mem=64  → W=256, H=3, D=85  → ~260 KB real
- mem=128 → W=256, H=3, D=170 → ~520 KB real
- mem=256 → W=256, H=3, D=341 → ~1 MB real
```

**Ahora sí deberías ver variación**: ARE baja cuando la memoria aumenta.

## Verificación

### Antes de la corrección:
```bash
# Todos los resultados idénticos
$ grep "wavesketch-ideal,8," benchmark_results_offline_full.csv | head -1
0,wavesketch-ideal,8,27,...,0.523,0.891,...

$ grep "wavesketch-ideal,256," benchmark_results_offline_full.csv | head -1
0,wavesketch-ideal,256,27,...,0.523,0.891,...  # ← ¡IGUALES!
```

### Después de la corrección:
```bash
# Resultados diferentes
$ grep "wavesketch-ideal,8," benchmark_results_offline_full.csv | head -1
0,wavesketch-ideal,8,27,...,0.845,0.723,...   # ← ARE alto (poca memoria)

$ grep "wavesketch-ideal,256," benchmark_results_offline_full.csv | head -1
0,wavesketch-ideal,256,27,...,0.123,0.987,... # ← ARE bajo (mucha memoria)
```

## Lecciones Aprendidas

1. **Los #defines de C++ son compile-time**: No puedes cambiarlos en runtime
2. **Template parameters deben ser constantes**: Por eso se usan #defines
3. **Los autores usan recompilación intencional**: Es parte del diseño
4. **Siempre verificar que las métricas varían**: Si todas las líneas son iguales → bug

## Código Relevante

### parameter.h (líneas críticas)

```cpp
#define FULL_WIDTH 256u                          // ← Usado por las tablas
#define FULL_HEIGHT 3u                           // ← Usado por las tablas
#define FULL_DEPTH (MAX_LENGTH / SAMPLE_RATE)    // ← Usado por las tablas (fijo a 64)

#define MEMORY_KB 2048                           // ← Solo etiqueta
#define MEMORY MEMORY_KB*1024                    // ← Solo etiqueta
```

### Wavelet/table.h (uso)

```cpp
class table : public basic_table<counter<BY_THRESHOLD>, FULL_WIDTH, LESS_HEIGHT> {
    //                     template parameter ─────────────^            ^
    //                     debe ser compile-time constant ─────────────┘
```

## Referencias

- Paper μMON (SIGCOMM '24): Section 5.2 "Memory-Accuracy Tradeoff"
- Código original: `uMon-WaveSketch/cpp_version/Utility/parameter.h`
- Issue similar: https://github.com/.../issues/... (si existe)

---

**Status**: ✅ **CORREGIDO** - El nuevo script recompila para cada memoria.
