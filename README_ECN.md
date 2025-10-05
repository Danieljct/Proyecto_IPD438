# Simulación ECN - Versión Limpia 🔬

Este proyecto implementa una simulación simplificada para medir paquetes marcados con ECN (Explicit Congestion Notification) en una topología Fat-Tree usando NS-3.45.

## ✨ Características

- **Medición precisa de ECN**: Monitorea marcas ECN en tiempo real
- **Código limpio y comentado**: Fácil de entender y modificar
- **Configuración optimizada**: Parámetros RED ajustados para garantizar marcado ECN
- **Salida clara**: Reportes detallados cada 2 segundos + resumen final

## 🚀 Compilación y Ejecución

### 1. Compilar
```bash
cd build
cmake ..
make fattree_ecn_clean
```

### 2. Ejecutar
```bash
./fattree_ecn_clean
```

## 📊 Resultados Esperados

La simulación debería mostrar:

```
✅ ECN DETECTADO - Funcionando correctamente!

📊 RESUMEN ECN:
Total marcas ECN: 48
Total drops: 24245
Total enqueues: 15788
Tasa ECN: 0.30%
Tasa Drop: 153.57%
```

## 🔧 Configuración Técnica

### Parámetros RED Optimizados
- **MinTh**: 1 (umbral mínimo ultra bajo)
- **MaxTh**: 2 (umbral máximo ultra bajo)  
- **QueueSize**: 5 paquetes (cola pequeña)
- **QW**: 1.0 (peso máximo para respuesta rápida)
- **UseEcn**: true (ECN habilitado)
- **Gentle**: true (Gentle RED activo)

### Topología
```
Host0 ────┐
          ├─ Switch0 ══ Switch1 ─┬─ Host2
Host1 ────┘                     └─ Host3
```

### Tráfico
- **TCP**: Host0 → Host3 (1460 bytes/seg)
- **UDP Agresivo**: Host1 → Host3 (30Mbps)
- **Enlaces**: 5Mbps con 5ms latencia

## 🔍 Cómo Funciona

1. **TCP con ECN habilitado** genera tráfico base
2. **UDP agresivo** fuerza congestión en las colas
3. **RED ultra-sensible** marca paquetes con ECN antes de descartarlos
4. **Monitoreo continuo** mide estadísticas cada 2 segundos

## 📈 Interpretación de Resultados

- **Tasa ECN > 0%**: ✅ ECN funcionando correctamente
- **Tasa ECN = 0%**: ⚠️ Ajustar parámetros de congestión
- **Drops altos**: Normal, indica congestión severa
- **Cola 8**: Generalmente donde ocurren más marcas ECN

## 🛠️ Personalización

Para modificar la simulación:

1. **Cambiar tráfico**: Editar sección "CONFIGURACIÓN DE APLICACIONES"
2. **Ajustar RED**: Modificar parámetros en sección "CONFIGURACIÓN RED"
3. **Topología**: Cambiar conexiones en "CREACIÓN DE CONEXIONES"
4. **Mediciones**: Ajustar frecuencia en "PROGRAMAR MEDICIONES ECN"

## 📝 Archivos

- `fattree_ecn_clean.cc`: Simulación principal (limpia)
- `fattree.cc`: Versión original con más funcionalidades
- `CMakeLists.txt`: Configuración de compilación

## 🎯 Objetivo

Este código demuestra cómo:
- Configurar ECN correctamente en NS-3.45
- Forzar condiciones de congestión para activar ECN
- Medir y reportar estadísticas ECN de manera clara
- Implementar una simulación de red realista con Fat-Tree

---

**Nota**: La clave para que ECN funcione es la combinación de tráfico TCP + UDP agresivo, que crea la presión necesaria en las colas para activar el marcado ECN.