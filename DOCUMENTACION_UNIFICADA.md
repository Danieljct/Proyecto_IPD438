# Proyecto IPD438 — Documentación Unificada

Última actualización: 1 de noviembre de 2025

## 1. Visión general

Este repositorio contiene dos simulaciones principales en ns-3 orientadas a estudiar
el comportamiento de ECN (Explicit Congestion Notification) en una topología Fat-Tree
simplificada y, en la versión avanzada, integrar un agente de medición basado en
WaveSketch para capturar contadores de flujo de alta resolución.

### Topologías mantenidas
- `fattree.cc`: versión base de referencia para la topología Fat-Tree y
  verificación de conectividad/aplicaciones sin instrumentación adicional.
- `fattree_ecn_clean.cc`: versión optimizada para la medición de ECN con RED
  ultra-agresivo, mezcla de tráfico TCP/UDP y un agente WaveSketch integrado
  (implementado íntegramente dentro del propio fichero).
- `fattree_k4_replay.cc`: topología Fat-Tree con k = 4 (16 hosts) que reproduce
  la traza `hadoop15.csv` sobre enlaces de 100 Gbps/1 µs y genera un CSV con la
  tasa de cada flujo; acompaña un script Python para graficar la evolución.

Se retiraron implementaciones experimentales adicionales para dejar solo las dos
variantes que demostraron ser útiles y mantenibles.

## 2. Requisitos y compilación

### Dependencias principales
- ns-3.45 compilado localmente (ruta configurada en `CMakeLists.txt` mediante
  la variable `NS3_DIR`).
- Un compilador compatible con C++20 (ej. `g++` >= 10).
- CMake >= 3.10.

### Proceso de compilación
```bash
cd /home/daniel/PaperRedes/Proyecto_IPD438/build
cmake ..
make fattree -j$(nproc)
make fattree_ecn_clean -j$(nproc)
make fattree_k4_replay -j$(nproc)
```

### Ejecución
```bash
./fattree
./fattree_ecn_clean > salida.txt 2>&1  # opcional: guardar salida
./fattree_k4_replay --input=hadoop15.csv --flowCsv=flow_rate.csv --windowNs=1000000
```

La salida de `fattree_ecn_clean` incluye mediciones periódicas de ECN, análisis
de colas RED y reportes del agente WaveSketch.

`fattree_k4_replay` genera el archivo indicado en `--flowCsv` (por defecto
`flow_rate.csv`) con columnas `time_s, total_rate_gbps, reconstructed_rate_gbps,
ecn_marks`. Además de la curva de tasa agregada, se registra cuántas marcas ECN
ocurrieron en cada ventana temporal (si `ecn_marks > 0`, hubo congestión
marcada) y la señal reconstruida aplicando el algoritmo `wavesketch/Wavelet`
(se normaliza por un factor fijo de 1 000 para respetar los límites internos de
WaveSketch). El script `plot_flow_rate.py` permite visualizar ambas curvas y
resaltar en rojo los instantes con marcado ECN. Adicionalmente,
`wavelet_reconstruct.py` sirve como herramienta aislada para reprocesar el CSV y
generar una gráfica comparando la señal original con su reconstrucción:

```bash
cd /home/daniel/PaperRedes/Proyecto_IPD438
./.venv/bin/python plot_flow_rate.py --input flow_rate.csv --output flow_rate.png
# Reconstrucción wavelet (opcional)
./.venv/bin/python wavelet_reconstruct.py --input flow_rate.csv --output wavelet_reconstruction.png
```

> Requiere `pandas` y `matplotlib` (ya presentes en el entorno virtual del
> proyecto). Si se usa otro intérprete, instala estos paquetes previamente.

## 3. Resumen técnico — `fattree_ecn_clean`

### Configuración de la red
- Topología Fat-Tree con 4 hosts y 2 switches interconectados.
- Enlaces punto a punto: 10 Mbps / 2 ms (acceso y core comparten misma capacidad).
- `TrafficControlHelper` instala colas RED en todos los enlaces.

### Parámetros RED por defecto
```
MinTh = 5 paquetes
MaxTh = 15 paquetes
MaxSize = 25 paquetes
UseEcn = true
Gentle = true
```
Los valores se pueden ajustar modificando el constructor del `TrafficControlHelper`
si se necesita una política más o menos agresiva.

### Tráfico generado (por defecto)
- TCP BulkSend: Host0 → Host3 (puerto 5001), tráfico continuo `MaxBytes = 0`.
- UDP OnOff: Host1 → Host3, tasa constante de 30 Mbps y puerto 9999.
- Ambos flujos comparten el enlace core, lo que provoca congestión y activa ECN.

### Reporte de ECN
- ECN está habilitado en TCP y RED marca paquetes cuando la cola alcanza los
  umbrales definidos. La versión actual prioriza la recopilación de métricas para
  WaveSketch, por lo que no imprime estadísticas de colas por defecto.
- Si se requieren métricas de drops/marks, se pueden añadir callbacks o integrar
  `FlowMonitor` en el main para recolectar estadísticas adicionales.

### WaveSketchAgent (integrado)
- Hereda de `ns3::Object` y se conecta a los eventos `Tx` de los generadores TCP
  y UDP (se identifican como flujo 1 y 2 respectivamente).
- Convierte la serie temporal de cada flujo en ventanas configurables (por
  defecto 50 µs) y mantiene los contadores en memoria.
- Cada milisegundo aplica la transformada Haar, conserva los coeficientes Top-K
  (parámetro `k`) y genera métricas de calidad (ARE, similitud coseno, distancia
  euclidiana) que se escriben en un CSV (`results.csv` por omisión).
- Todos los parámetros (`k`, `windowUs`, `outputFile`) pueden modificarse desde la
  línea de comandos:
  ```bash
  ./fattree_ecn_clean --k=8 --windowUs=25 --outputFile=experimento.csv
  ```

## 4. Resultados experimentales destacados

- Con la configuración de 10 Mbps / 2 ms y RED `MinTh=5`, la presencia de un
  flujo UDP agresivo sigue produciendo drops sin respuesta, mientras que el
  flujo TCP reduce su ritmo al detectar marcas ECN (se puede comprobar
  visualizando la evolución de la tasa TCP mediante herramientas externas).
- El CSV generado almacena por cada ciclo de análisis: tiempo simulado,
  identificador de flujo, parámetros `k` y `windowUs`, suma de paquetes en la
  curva, ARE, similitud coseno y distancia euclidiana frente a la reconstrucción.
- Las métricas permiten comparar diferentes configuraciones de `k` y ventana de
  muestreo para estudiar la fidelidad del esquema WaveSketch.

## 5. Historial de cambios relevante

- Se reescribió el agente WaveSketch para que sea autónomo: ya no depende de
  `Ipv4FlowClassifier` y utiliza identificadores de flujo suministrados desde los
  generadores de tráfico (1 = TCP, 2 = UDP). Esto simplifica la integración y 
  evita dependencias con módulos no presentes.
  Se ajustó el `CMakeLists.txt` para enlazar `ns3.45-flow-monitor-default`, la
  librería disponible en ns-3.45 para operaciones de monitoreo.
- Se añadieron rutinas de depuración e interpretación automática en los reportes
  de ECN y se incorporó la escritura directa de métricas de WaveSketch a CSV para
  facilitar el análisis offline.
- Toda la documentación dispersa se consolidó en este archivo, simplificando el
  mantenimiento futuro.

## 6. Buenas prácticas y próximos pasos

- **Ejecución**: redirige la salida a fichero (`> salida.txt 2>&1`) para facilitar
  el análisis posterior y la comparación entre runs.
- **Depuración**: si necesitas revisar comportamientos específicos, añade
  `NS_LOG_INFO` en secciones críticas (por ejemplo en `WaveSketchAgent::OnPacketSent`).
- **Extensiones posibles**:
  - Integrar `FlowMonitor` completo para correlacionar métricas de throughput.
  - Automatizar barridos de parámetros RED para estudiar sensibilidad.
  - Exportar los coeficientes WaveSketch a un fichero para reconstrucciones
    offline.

Con esto queda un repositorio compacto y documentado listo para nuevas
experimentaciones centradas en ECN y WaveSketch sobre la topología Fat-Tree.
