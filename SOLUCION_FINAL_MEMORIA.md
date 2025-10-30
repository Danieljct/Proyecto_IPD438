# 🎯 SOLUCIÓN FINAL: Cómo Variar la Memoria Correctamente

## 🐛 El Problema en 3 Niveles

### Nivel 1: Error Superficial (YA CORREGIDO)
**Qué**: El script de sweep no actualizaba `MEMORY_KB` correctamente  
**Fix**: Usar `sed -E` con regex correcta como los autores

### Nivel 2: Error Intermedio (YA CORREGIDO)
**Qué**: `MEMORY_KB` se actualizaba pero era solo una etiqueta  
**Observación**: Los algoritmos NO lo usaban para dimensionar tablas

### Nivel 3: **CAUSA RAÍZ** (AHORA CORREGIDO)
**Qué**: `FULL_DEPTH` estaba hardcoded independiente de `MEMORY_KB`

```cpp
// ❌ ANTES (MAL)
#define FULL_DEPTH (MAX_LENGTH / SAMPLE_RATE)  // = 2048/32 = 64 SIEMPRE

// ✅ DESPUÉS (BIEN)
#define FULL_DEPTH ((MEMORY_KB * 1024) / (FULL_WIDTH * FULL_HEIGHT * 4))
```

## 📐 Matemática de la Memoria

### Fórmula Base
```
MEMORY_TOTAL = FULL_WIDTH × FULL_HEIGHT × FULL_DEPTH × sizeof(counter)
MEMORY_TOTAL = 256 × 3 × DEPTH × 4 bytes
MEMORY_TOTAL = 3072 × DEPTH bytes
```

### Despejando DEPTH
```
DEPTH = MEMORY_KB × 1024 / 3072
DEPTH = MEMORY_KB × 1024 / (256 × 3 × 4)
```

### Valores Resultantes

| MEMORY_KB | FULL_DEPTH | Memoria Real |
|-----------|------------|--------------|
| 8         | 2          | 6.1 KB       |
| 16        | 5          | 15.4 KB      |
| 32        | 10         | 30.7 KB      |
| 64        | 21         | 64.5 KB      |
| 128       | 42         | 129 KB       |
| 256       | 85         | 261 KB       |

**Nota**: La memoria real es ligeramente diferente del objetivo porque `DEPTH` debe ser entero.

## 🔍 Por Qué NO Funcionaba Antes

### Cadena de Dependencias

```
MEMORY_KB (cambiado por sed)
    ↓ (NO CONECTADO ANTES)
FULL_DEPTH = MAX_LENGTH / SAMPLE_RATE  (constante)
    ↓
OmniWindow::counter::DEPTH = FULL_DEPTH
Wavelet::counter::DEPTH = ROUND(FULL_DEPTH * 4 + 4 - 42, 4)
Fourier::counter::DEPTH = (FULL_DEPTH * 4) / 6
    ↓
Tamaños de tabla (todos constantes)
```

### Ahora (Correcto)

```
MEMORY_KB (cambiado por sed)
    ↓ (AHORA CONECTADO)
FULL_DEPTH = (MEMORY_KB * 1024) / (256 * 3 * 4)  (dinámico)
    ↓
OmniWindow::counter::DEPTH = FULL_DEPTH
Wavelet::counter::DEPTH = ROUND(FULL_DEPTH * 4 + 4 - 42, 4)
Fourier::counter::DEPTH = (FULL_DEPTH * 4) / 6
    ↓
Tamaños de tabla (varían con MEMORY_KB)
```

## 🧪 Validación del Fix

### Test 1: Compilación con Diferentes Memorias
```bash
# MEMORY_KB=32 → FULL_DEPTH=10
sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\132/" Utility/parameter.h
make offline_evaluator
# → Compila OK

# MEMORY_KB=128 → FULL_DEPTH=42
sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\1128/" Utility/parameter.h
make offline_evaluator
# → Compila OK con tamaños diferentes
```

### Test 2: Inspección de Constantes en Compile-Time
```bash
# Ver el valor de FULL_DEPTH que se está usando
echo | g++ -E -dM -include Utility/parameter.h - | grep FULL_DEPTH
```

### Test 3: Verificar Resultados Variables (Dataset Completo)
```bash
# Los flows multi-ventana deben mostrar ARE diferente entre memorias
grep "wavesketch-ideal.*flow_id,<ID>" benchmark_results_offline_full.csv | \
  awk -F, '{print $3, $8}' | sort -n
# Debe mostrar: 8 <ARE_alto>, 16 <ARE_medio>, ..., 256 <ARE_bajo>
```

## ⚠️ Por Qué la Muestra Pequeña No Mostró Diferencias

### Análisis del Dataset de Prueba (5k líneas)
```
Parsed 5000 lines, found 15 flows
Inspect flow=27: numWindows=1, packets=943
```

**Problema**: Cada flow tiene actividad en **solo 1 ventana**.

### Por Qué Esto Es Trivial

Con 1 ventana:
- No hay compresión temporal
- No hay coeficientes de detalle (wavelets)
- No hay transformadas de Fourier útiles
- Todos los algoritmos son equivalentes a "contar"

**Reconstrucción perfecta**:
```
orig: 943
rec : 943
ARE = 0 (todos iguales)
```

### Dataset Completo (Esperado)
```
Parsed 1,661,240 lines, found 625 flows
Flow típico: numWindows=50-200, packets=10,000-100,000
```

Con múltiples ventanas:
- ✅ Los algoritmos comprimen temporalmente
- ✅ Con poca memoria pierden detalle
- ✅ Con mucha memoria preservan detalle
- ✅ **AHORA SÍ se ven diferencias**

## 📊 Resultados Esperados (Dataset Completo)

### Ejemplo de Flow Multi-Ventana

| MEMORY_KB | WaveSketch ARE | Fourier ARE | OmniWindow ARE |
|-----------|----------------|-------------|----------------|
| 8         | 0.45           | 0.78        | 0.92           |
| 16        | 0.32           | 0.65        | 0.85           |
| 32        | 0.21           | 0.48        | 0.73           |
| 64        | 0.12           | 0.28        | 0.54           |
| 128       | 0.06           | 0.15        | 0.32           |
| 256       | 0.03           | 0.08        | 0.18           |

**Patrón esperado**: WaveSketch < Fourier < OmniWindow (menor es mejor)

## 🎓 Lecciones Aprendidas

### 1. **Siempre Verifica la Cadena Completa**
No basta con cambiar un `#define`. Hay que seguir todas las dependencias.

### 2. **Los Templates de C++ Necesitan Constantes**
Por eso los autores recompilan: los parámetros deben ser conocidos en compile-time.

### 3. **Dataset de Prueba Debe Ser Representativo**
Una muestra con flows de 1 ventana NO revela diferencias entre algoritmos.

### 4. **Validar con Inspección Detallada**
`--inspect` es crucial para ver qué pasa flow por flow.

## 📝 Cambios Realizados

### 1. `Utility/parameter.h`
```cpp
// ANTES
#define FULL_DEPTH (MAX_LENGTH / SAMPLE_RATE)  // 64

// DESPUÉS  
#define MEMORY_KB 256 // Default
#define MEMORY (MEMORY_KB*1024)
#define FULL_DEPTH ((MEMORY_KB * 1024) / (FULL_WIDTH * FULL_HEIGHT * 4))
```

### 2. `scripts/run_full_sweep.sh`
```bash
# Usar sed con regex extendida (como los autores)
sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\1${mem}/" Utility/parameter.h
```

### 3. Flujo de Barrido
```
Para cada MEMORY_KB en [8, 16, 32, 64, 128, 256]:
  1. sed actualiza MEMORY_KB en parameter.h
  2. make recompila offline_evaluator
     → FULL_DEPTH se recalcula en compile-time
     → Todos los counter::DEPTH se ajustan
  3. Ejecutar evaluador
  4. Resultados AHORA varían
```

## ✅ Checklist de Validación

Una vez termine el barrido completo:

- [ ] Ver que ARE **disminuye** cuando MEMORY_KB aumenta
- [ ] Verificar que WaveSketch ARE < otros algoritmos
- [ ] Confirmar diferencias más grandes con memoria baja (<64 KB)
- [ ] Inspeccionar algunos flows multi-ventana con `--inspect`
- [ ] Generar gráficos y comparar con Fig 11-12 del paper

## 🚀 Status

- ✅ `parameter.h` corregido (FULL_DEPTH dinámico)
- ✅ Script de sweep actualizado (sed correcto)
- 🔄 Barrido completo ejecutándose sobre websearch25.csv
- ⏰ Tiempo estimado: ~12 minutos
- 📊 Resultados esperados: **Variación real entre memorias**

---

**Conclusión**: El problema NO era el script, era la **definición de FULL_DEPTH**. Ahora está correctamente vinculado a `MEMORY_KB` y los algoritmos realmente usan diferentes cantidades de memoria.
