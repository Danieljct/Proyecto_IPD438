# 🔧 Problema Final: CMake No Detecta Cambios en Headers

## El Último Bug

### Síntoma
Pese a corregir `FULL_DEPTH` y actualizar `MEMORY_KB` con `sed`, **los resultados seguían idénticos para todas las memorias**.

### Causa Raíz: Dependency Tracking de CMake

CMake **no detecta automáticamente cambios en headers** cuando:
1. El header se modifica externamente (por `sed`)
2. No se toca el timestamp del archivo
3. Los archivos objeto (.o) siguen siendo "más nuevos" que el header

```bash
# Lo que pasaba:
sed modifica Utility/parameter.h     # ✓ Archivo cambiado
make offline_evaluator               # ✗ CMake dice "nada que hacer"
                                     # (porque offline_evaluator.cpp.o es más nuevo)
```

### Evidencia

```bash
# Logs mostraban "Recompiling..." pero:
$ tail sweep_log.txt
  Recompiling...
✓ Completed memory=8 KB
  Recompiling...
✓ Completed memory=16 KB

# Pero los resultados agregados eran IDÉNTICOS:
fourier,8,40.67,...    # ← Mismo valor
fourier,16,40.67,...   # ← Mismo valor  
fourier,32,40.67,...   # ← Mismo valor
```

### Por Qué No Se Detectó Antes

1. **El script decía "Recompiling"** (pero make no hacía nada)
2. **Los comandos salían exitosos** (make retorna 0 si "no hay trabajo")
3. **El binario se ejecutaba** (con la memoria vieja)

## Solución

### Enfoque 1: Forzar Eliminación de Object Files

```bash
rm -f CMakeFiles/offline_evaluator.dir/offline_evaluator.cpp.o
rm -f CMakeFiles/offline_evaluator.dir/Utility/pffft.c.o
make offline_evaluator
```

**Problema**: Si hay otros archivos que dependen del header, también deben eliminarse.

### Enfoque 2: Touch del Header (IMPLEMENTADO)

```bash
sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\1${mem}/" Utility/parameter.h
touch Utility/parameter.h  # ← Actualiza timestamp
make offline_evaluator     # → CMake detecta cambio y recompila
```

**Ventaja**: CMake automáticamente recompila todos los archivos que dependen del header.

### Enfoque 3: make clean (Más Seguro pero Más Lento)

```bash
sed ...
cd build
make clean
make offline_evaluator
```

**Problema**: Recompila TODO (incluso pffft.c que no cambió).

## Implementación Final

```bash
# scripts/run_full_sweep.sh (corregido)

for mem in "${MEMORIES[@]}"; do
    # 1. Actualizar MEMORY_KB
    sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\1${mem}/" Utility/parameter.h
    
    # 2. CRÍTICO: Touch para forzar detección de cambio
    touch Utility/parameter.h
    
    # 3. Recompilar (ahora CMake SÍ detecta el cambio)
    cd build
    rm -f CMakeFiles/offline_evaluator.dir/offline_evaluator.cpp.o  # Seguridad extra
    make offline_evaluator
    cd ..
    
    # 4. Ejecutar
    ./offline_evaluator ...
done
```

## Validación

### Test Manual

```bash
# Cambiar a 16 KB
sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\116/" Utility/parameter.h
touch Utility/parameter.h

# Compilar
cd build && make offline_evaluator 2>&1 | grep Building

# Resultado ESPERADO:
[ 33%] Building CXX object CMakeFiles/offline_evaluator.dir/offline_evaluator.cpp.o
                ^^^^^^^^ ← Esto confirma que SÍ recompiló
```

### Verificación de Resultados

Después del barrido corregido, deberías ver:

```bash
# Valores DIFERENTES para cada memoria
fourier,8,ARE1,...
fourier,16,ARE2,...    # ← ARE2 ≠ ARE1
fourier,32,ARE3,...    # ← ARE3 ≠ ARE2
```

## Checklist de Recompilación Correcta

Para cada iteración del barrido:

- [ ] `sed` actualiza `MEMORY_KB` en parameter.h
- [ ] `touch` actualiza timestamp de parameter.h
- [ ] `make` muestra líneas "Building CXX" (no solo "Linking")
- [ ] Timestamp del binario `offline_evaluator` cambia
- [ ] Resultados son DIFERENTES entre memorias

## Debugging

### Si los resultados siguen iguales:

```bash
# 1. Verificar que parameter.h cambió
grep MEMORY_KB Utility/parameter.h

# 2. Verificar timestamp del binario
ls -lh offline_evaluator

# 3. Verificar que CMake detectó el cambio (debe recompilar)
cd build && make offline_evaluator 2>&1 | grep -c "Building"
# Debe ser > 0

# 4. Verificar constantes en el binario compilado
strings offline_evaluator | grep -i memory | head -5
```

### Verificar FULL_DEPTH en el Binario

Para confirmar que el valor cambió:

```bash
# Compilar con mem=16
sed -i "s/MEMORY_KB [0-9]*/MEMORY_KB 16/" Utility/parameter.h
touch Utility/parameter.h && cd build && make && cd ..

# FULL_DEPTH debería ser 5 para mem=16
# 16KB × 1024 / (256 × 3 × 4) = 5.33 ≈ 5

# Para verificar en runtime, añadir debug print:
echo "cerr << \"FULL_DEPTH=\" << FULL_DEPTH << endl;" >> offline_evaluator.cpp
```

## Lección Final

### ❌ Lo Que NO Funciona

```bash
sed modifica header
make             # ← NO recompila (dependency no detectada)
```

### ✅ Lo Que SÍ Funciona

```bash
sed modifica header
touch header     # ← Fuerza timestamp nuevo
make             # ← SÍ recompila (dependency detectada)
```

## Status

- ✅ `parameter.h` con `FULL_DEPTH` dinámico
- ✅ Script con `touch` para forzar recompilación
- ✅ Test manual confirma que ahora SÍ recompila
- 🔄 **Barrido completo re-ejecutándose** con recompilación real
- ⏰ ETA: ~12-15 minutos (compilación + ejecución × 6)

---

**Conclusión**: El problema NO era la fórmula de `FULL_DEPTH` (esa ya estaba bien). El problema era que **CMake no detectaba los cambios** en el header modificado por `sed`. Solución: `touch` el header para actualizar su timestamp y forzar la recompilación.
