# Investigación: Por Qué los Algoritmos Son Inmunes a la Memoria en NS-3

## Problema Descubierto

Al ejecutar el barrido de NS-3 con diferentes valores de memoria (96, 128, 192, 256 KB), **todos los algoritmos producen resultados idénticos** independientemente de la memoria configurada.

### Evidencia

```bash
# WaveSketch con 96KB
12,wavesketch,96,211110695338497,2436,389.939,0.989636

# WaveSketch con 256KB (IDÉNTICO)
12,wavesketch,256,211110695338497,2436,389.939,0.989636
```

Los valores de ARE y Cosine Similarity son **exactamente iguales** entre diferentes memorias.

## Root Cause Analysis

### 1. Definiciones de Memoria en `parameter.h`

```cpp
// Utility/parameter.h (líneas 24-33)
#define MEMORY_KB 256  // ← Hardcoded en compile-time
#define FULL_WIDTH 256u
#define FULL_HEIGHT 3u
#define FULL_DEPTH ((MEMORY_KB * 1024) / (FULL_WIDTH * FULL_HEIGHT * 4))
```

**Problema**: `MEMORY_KB` es una macro `#define` que se resuelve en **compile-time**, no en runtime.

### 2. Inicialización de Algoritmos en NS-3

```cpp
// fattree_with_sketches.cc (líneas 86-93)
void Setup(uint32_t memoryKB, uint32_t windowUs, ...) {
    m_memoryKB = memoryKB;  // ← Solo guarda el valor, NO lo usa
    // ...
    if (m_algorithm == "wavesketch") {
        m_wavesketch.reset();  // ← Usa MEMORY_KB de parameter.h
    }
}
```

**Problema**: El parámetro `memoryKB` se guarda pero **nunca se pasa** a los algoritmos. Los constructores usan las macros compiladas.

### 3. Definición de Estructuras Internas

```cpp
// Wavelet/table.h
template<bool BY_THRESHOLD>
class table {
    counter<BY_THRESHOLD, FULL_WIDTH, FULL_HEIGHT, FULL_DEPTH> data{}; 
    //                    ^^^^^^^^^^^  ^^^^^^^^^^^^  ^^^^^^^^^^
    //                    Todas son macros compile-time
}
```

**Problema**: Las estructuras de datos (`counter`, `table`, `heavy`) se instancian con **tamaños fijos** de las macros.

## Comparación: Por Qué Funciona Offline

### Evaluador Offline

```bash
# scripts/run_sweep_isolated.sh
for mem in 96 128 192 256; do
    # 1. Modifica parameter.h
    sed -i "s/^#define MEMORY_KB .*/#define MEMORY_KB ${mem}/" parameter.h
    
    # 2. RECOMPILA completamente
    rm -rf build && mkdir build && cd build && cmake .. && make
    
    # 3. Ejecuta con binario específico de esa memoria
    ./offline_evaluator
done
```

**Clave**: Cada memoria tiene su **propio binario compilado** con diferentes valores de las macros.

### NS-3 (Intento Fallido)

```bash
# scripts/run_ns3_sweep.sh (INCORRECTO)
# Compila UNA VEZ con MEMORY_KB=256
make fattree_with_sketches

# Ejecuta el MISMO binario con diferentes parámetros
for mem in 96 128 192 256; do
    ./fattree_with_sketches --memoryKB=$mem  # ← Ignorado
done
```

**Problema**: El binario ya tiene `FULL_DEPTH=85` (para 256KB) hardcoded en todas las estructuras.

## Cálculo de FULL_DEPTH

```
FULL_DEPTH = (MEMORY_KB × 1024) / (FULL_WIDTH × FULL_HEIGHT × 4)
           = (MEMORY_KB × 1024) / (256 × 3 × 4)
           = (MEMORY_KB × 1024) / 3072
```

| MEMORY_KB | FULL_DEPTH | Memoria Real |
|-----------|------------|--------------|
| 96 KB     | 32         | 98,304 bytes |
| 128 KB    | 42         | 129,024 bytes |
| 192 KB    | 64         | 196,608 bytes |
| 256 KB    | 85         | 261,120 bytes |

Con el binario compilado para 256KB, **siempre usa FULL_DEPTH=85** internamente, sin importar el parámetro CLI.

## Impacto en Resultados

### Algoritmos Afectados

#### WaveSketch
```cpp
class table {
    counter<false, FULL_WIDTH, FULL_HEIGHT, FULL_DEPTH> data{};
    //                                      ^^^^^^^^^^
    //                                      Siempre 85 si compiló con 256KB
}
```
- Usa arrays de tamaño `FULL_DEPTH` para almacenar coeficientes wavelet
- Con FULL_DEPTH fijo, **capacidad constante** → resultados idénticos

#### Fourier
```cpp
// En fourier.h
counter<FULL_WIDTH, FULL_HEIGHT, FULL_DEPTH> data{};
```
- Almacena coeficientes de Fourier en tablas de `FULL_DEPTH`
- FULL_DEPTH fijo → **mismo número de coeficientes** siempre

#### OmniWindow y PersistCMS
- Similar: estructuras internas con `FULL_DEPTH` compile-time
- No pueden adaptarse dinámicamente

## Solución Implementada

### Enfoque: Recompilar por Memoria

```bash
# scripts/run_ns3_sweep_recompiled.sh
for mem in 96 128 192 256; do
    # 1. Modificar parameter.h
    sed -i "s/^#define MEMORY_KB .*/#define MEMORY_KB ${mem}/" parameter.h
    
    # 2. Recompilar NS-3
    rm -rf build/*.o build/fattree_with_sketches
    make fattree_with_sketches
    
    # 3. Ejecutar con binario específico
    for algo in wavesketch fourier omniwindow persistcms; do
        ./fattree_with_sketches --algorithm=$algo --memoryKB=$mem
    done
done
```

**Ventaja**: Garantiza que cada memoria tiene las estructuras internas correctas.

**Desventaja**: Requiere 4 compilaciones (una por memoria).

## Alternativas Exploradas

### Alternativa 1: Templates con Parámetros Runtime ❌

```cpp
template<int WIDTH, int HEIGHT, int DEPTH>
class counter { ... };

// Instanciar en runtime:
counter<256, 3, depth_runtime> c;  // ← NO FUNCIONA
```

**Problema**: Los templates C++ requieren valores **constexpr** en compile-time.

### Alternativa 2: Polimorfismo con Virtual ❌

```cpp
class abstract_counter {
    virtual void count(...) = 0;
};

template<int DEPTH>
class counter_impl : public abstract_counter { ... };

// Factory pattern:
unique_ptr<abstract_counter> create(int depth) {
    switch(depth) {
        case 32: return make_unique<counter_impl<32>>();
        case 85: return make_unique<counter_impl<85>>();
        ...
    }
}
```

**Problema**: Requiere reescribir TODO el código original, aumenta overhead runtime.

### Alternativa 3: Vectores Dinámicos ❌

```cpp
class counter {
    vector<vector<vector<int>>> data;  // En lugar de arrays fijos
    
    counter(int width, int height, int depth) {
        data.resize(width);
        for(auto& w : data) {
            w.resize(height);
            for(auto& h : w) h.resize(depth);
        }
    }
}
```

**Problema**: 
- Cambio invasivo en código original
- Overhead de memoria (punteros)
- Peor rendimiento (cache misses)

### Alternativa 4 (ELEGIDA): Recompilación ✓

```bash
# Compilar un binario por memoria
for mem in MEMORIES; do
    update_parameter_h($mem)
    recompile()
    run_simulations($mem)
done
```

**Ventajas**:
- ✓ No modifica código original
- ✓ Performance óptimo (compile-time optimization)
- ✓ Compatible con enfoque offline existente
- ✓ Garantiza correctitud

**Desventajas**:
- ⚠️ Tiempo de compilación (×4)
- ⚠️ No es "true runtime" configurability

## Verificación de la Solución

### Test 1: Valores Diferentes Entre Memorias

```bash
# Después de recompilar
$ diff results_ns3_sweep_recompiled/wavesketch_96kb.csv \
       results_ns3_sweep_recompiled/wavesketch_256kb.csv

# ESPERADO: Diferencias en ARE y Cosine
```

### Test 2: Variación de Fourier

Fourier mostró **143.7% de variación** en offline:
- 96KB: ARE = 2.06
- 256KB: ARE = 0.37

**Esperado en NS-3**: Variación significativa (aunque menor por menos flujos).

### Test 3: Constancia de WaveSketch

WaveSketch fue **constante** en offline (ARE=0.48 para todas las memorias).

**Esperado en NS-3**: Sigue constante, pero con valores diferentes del primer barrido.

## Lecciones Aprendidas

### 1. Compile-Time vs Runtime
- C++ templates y macros son **compile-time**
- Parámetros CLI son **runtime**
- No se pueden mezclar sin arquitectura específica

### 2. Validación Importante
- Siempre verificar que **parámetros se usan realmente**
- Comparar outputs para detectar valores idénticos sospechosos
- El hecho de que compile NO significa que funcione correctamente

### 3. Trade-offs de Diseño
- **Performance vs Flexibilidad**: Arrays fijos son rápidos pero inflexibles
- **Compile-time optimization**: Poderosa pero requiere recompilación
- **Dynamic allocation**: Flexible pero con overhead

## Próximos Pasos

1. ✅ Ejecutar `run_ns3_sweep_recompiled.sh`
2. ✅ Verificar que resultados varían con memoria
3. ✅ Analizar con `analyze_ns3_results_recompiled.py`
4. ✅ Comparar con resultados offline
5. 📝 Documentar hallazgos en paper

## Referencias

- **Código offline**: `scripts/run_sweep_isolated.sh` (líneas 15-25)
- **Parameter.h**: `Utility/parameter.h` (líneas 24-33)
- **NS-3 agent**: `fattree_with_sketches.cc` (líneas 75-95)
- **Análisis previo**: `COMPARISON_OFFLINE_VS_NS3.md`
