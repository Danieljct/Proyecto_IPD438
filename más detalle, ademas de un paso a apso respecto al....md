# **Guía de Replicación Detallada: μΜΟΝ (SIGCOMM '24)**

Este documento proporciona un desglose técnico profundo y un análisis paso a paso de los algoritmos presentados en el *paper* "μΜοΝ: Empowering Microsecond-level Network Monitoring with Wavelets", con el fin de replicar sus resultados.

## **1\. Objetivos de la Replicación**

La replicación exitosa de μΜΟΝ implica implementar y validar dos componentes principales:

1. **Medición µFlow (Host):** Implementar el algoritmo **WaveSketch** (versiones C++ y P4) para medir y comprimir curvas de tasa de flujo a nivel de microsegundo (8.192 µs) con bajo *overhead*.  
2. **Detección µEvent (Switch):** Implementar el mecanismo de captura de eventos en un *switch commodity* (usando ACLs) para detectar congestión (paquetes CE) y muestrearlos (basado en PSN) con un *overhead* de ancho de banda aceptable.

## **2\. Arquitectura del Sistema**

Para replicar el sistema, necesitas tres partes funcionales, como se muestra en la Figura 4 del *paper*:

1. **Hosts (Servidores):** Aquí es donde se ejecuta el algoritmo **WaveSketch**. Mide las tasas de flujo de las aplicaciones y envía los *sketches* comprimidos (coeficientes wavelet) al analizador.  
2. **Switches de Red (ToR, Spine):** Aquí se ejecuta la **Detección µEvent**. No ejecutan WaveSketch, sino que usan ACLs para "matchear", "muestrear" y "espejear" paquetes de eventos de congestión (CE) al analizador.  
3. **Analizador µΜΟΝ (Servidor Dedicado):** Recibe datos de *ambas* fuentes. Almacena los WaveSketches (coeficientes) y los paquetes de eventos. Su trabajo es **reconstruir** las curvas de flujo (Algoritmo 2\) y **correlacionar** los eventos con las curvas de flujo para permitir el "replay".

**Requisito Crítico:** Todos los nodos (hosts y switches) deben estar sincronizados con alta precisión (nanosegundos) usando **PTP (Precision Time Protocol)**.

## **3\. Paso a Paso: Algoritmo WaveSketch (Medición µFlow)**

Este es el núcleo de la contribución. Nos centraremos en los algoritmos 1 (actualización) y 2 (reconstrucción).

### **3.1. Fundamentos: Wavelet de Haar**

WaveSketch usa una variante de la **Wavelet de Haar**. Su ventaja clave es que no requiere multiplicaciones complejas.  
Como se ve en la Figura 5, una transformación de Haar toma dos valores adyacentes (ej. 7 y 9\) y los reemplaza por su **aproximación** (promedio o suma, ej. $16$) y su **detalle** (diferencia, ej. $-2$). El algoritmo del *paper* usa suma y resta:

* Approximation \= val1 \+ val2  
* Detail \= val1 \- val2

La reconstrucción es la inversa:

* val1 \= (Approximation \+ Detail) / 2  
* val2 \= (Approximation \- Detail) / 2

El algoritmo 1 realiza esto de forma jerárquica (multinivel) y *online*.

### **3.2. Estructura de Datos (WaveSketch Versión Completa)**

Debes implementar una estructura de dos partes:

1. **Heavy Part:** Una tabla hash simple.  
   * Key: ID del Flujo (5-tupla).  
   * Value: Un "bucket" de WaveSketch (ver abajo).  
   * *Gestión:* Usa **Majority Vote** para detectar y gestionar heavy hitters.  
2. **Light Part:** Un *sketch* Count-Min (CMS) de D filas y W columnas.  
   * Key: ID del Flujo (hash).  
   * Value: Cada celda (d, w) del CMS es un "bucket" de WaveSketch.

**Contenido de un "Bucket" de WaveSketch:**

* w0: int64. Timestamp de la ventana inicial.  
* i: int. Desplazamiento de la ventana actual (contador de ventanas).  
* c: int. Contador de bytes/paquetes para la ventana i.  
* A: array\[\]. Almacén para los coeficientes de **Aproximación** del nivel más profundo (L). Tamaño \= n / 2^L.  
* D: priority\_queue\[\]. Almacén para los **K** coeficientes de **Detalle** más significativos (Top-K).  
* \_details: array\[\]. Almacén temporal para los coeficientes de detalle que se están calculando en cada nivel. Tamaño \= L.

### **3.3. Código Paso a Paso: Actualización (Algorithm 1\)**

Este algoritmo se ejecuta en el *host* por cada paquete que llega.  
// \--- Pseudocódigo Explicado del Algoritmo 1 \---

// Función principal llamada por cada paquete (con su timestamp wj y tamaño v)  
procedure COUNTING(wj, v):  
    // 1\. Inicialización del bucket (si es el primer paquete)  
    if w0 \== 0:  
        w0 \= wj // Fija el timestamp de inicio

    // 2\. Determinar la ventana actual  
    // La granularidad es 8.192 µs (o 2^13 ns)  
    // current\_window\_offset \= (wj \- w0) \>\> 13  
    current\_window\_offset \= (wj \- w0) / 8192

    // 3\. FAST PATH (Ruta Rápida)  
    // Si el paquete pertenece a la MISMA ventana de tiempo que el paquete anterior  
    if current\_window\_offset \== i:  
        c \= c \+ v // Simplemente acumula el contador  
        return    // Termina

    // 4\. SLOW PATH (Ruta Lenta) \- ¡Se cruzó un límite de ventana\!  
    else:  
        // 4a. Procesa el contador (c) de la ventana que ACABA de terminar (i)  
        Transformation(i, c)   
          
        // 4b. Manejo de ventanas vacías (gaps)  
        // Si se saltaron ventanas (ej. de i=2 a i=5),  
        // llama a Transformation() para las ventanas 3 y 4 con c=0.  
        for empty\_i in (i \+ 1\) ... (current\_window\_offset \- 1):  
            Transformation(empty\_i, 0\)

        // 4c. Resetea el contador para la NUEVA ventana  
        c \= v // El valor del paquete actual  
        i \= current\_window\_offset // El offset de la ventana actual

// Procesa un contador 'c' de una ventana terminada 'i'  
procedure TRANSFORMATION(i, c):  
    // 1\. Actualizar Coeficiente de Aproximación (Nivel L)  
    // Esta es la "media" o "base" de la señal  
    pos\_a \= i \>\> L // Posición en el array de aproximación  
    A\[pos\_a\] \= A\[pos\_a\] \+ c

    // 2\. Bucle Multinivel (para coeficientes de Detalle)  
    // Itera desde el nivel 0 (más fino) hasta el L-1 (más grueso)  
    for l from 0 to L-1:  
        // 2a. Calcula la posición del coeficiente de detalle para este nivel  
        pos\_d \= i \>\> (l \+ 1\) 

        // 2b. ¿Hemos completado un nuevo coeficiente de detalle?  
        // Si pos\_d es \> que el índice del último coeficiente calculado en este nivel...  
        if pos\_d \> \_details\[l\].idx:  
            // ...significa que \_details\[l\] (el anterior) está completo.  
            // 2b-i. Comprimir el coeficiente anterior  
            Compression(l, \_details\[l\])   
            // 2b-ii. Resetear para el nuevo coeficiente  
            \_details\[l\] \= {idx: pos\_d, val: 0} 

        // 2c. Aplicar la Wavelet de Haar (Suma/Resta)  
        // Determina si 'c' va en la parte "izquierda" (suma) o "derecha" (resta)  
        // de la wavelet en este nivel 'l'.  
        sign\_d \= (i \>\> l) & 1 // 0 para izquierda, 1 para derecha  
        if sign\_d \== 0:  
            \_details\[l\].val \= \_details\[l\].val \+ c  
        else:  
            \_details\[l\].val \= \_details\[l\].val \- c

// Decide si guardar un coeficiente de detalle completo  
procedure COMPRESSION(l, detail):  
    // 1\. Calcular el peso (magnitud).  
    // El 'paper' usa (1/sqrt(2)^l) \* |detail.val|  
    // En la práctica, se compara |detail.val| con el valor K-ésimo  
    // de la cola de prioridad (D), ajustado por el peso del nivel.  
      
    // 2\. Lógica de Top-K  
    if D.size() \< K:  
        D.push(detail) // Añadir si la cola no está llena  
    else if |detail.val| (ponderado) \> |D.min()| (ponderado):  
        D.pop\_min()  // Saca el más pequeño  
        D.push(detail) // Inserta el nuevo

### **3.4. Código Paso a Paso: Reconstrucción (Algorithm 2\)**

Este algoritmo se ejecuta en el *Analizador µΜΟΝ* cuando se solicita una curva de flujo.  
// \--- Pseudocódigo Explicado del Algoritmo 2 \---

// Recibe los coeficientes almacenados (A y D) y el w0  
procedure RECONSTRUCTION(w0, A, D):  
    // 1\. Manejar el último contador  
    // El último contador (i, c) no fue procesado. Hazlo ahora.  
    Transform(i, c)  
    // También procesa los \_details\[\] temporales restantes  
    for l from 0 to L-1:  
        Compression(l, \_details\[l\])

    // 2\. Inicializar el array de reconstrucción  
    // El array 'A' (coeficientes de aprox. de nivel L) es nuestro punto de partida.  
    current\_level\_approx \= A 

    // 3\. Bucle de Reconstrucción Inversa (de grueso a fino)  
    // Itera desde el nivel L-1 (el más profundo almacenado) hasta el 0\.  
    for l from (L \- 1\) down to 0:  
        next\_level\_approx \= new array\[size \* 2\]  
          
        // 4\. Itera sobre los coeficientes de aproximación del nivel actual  
        for k from 0 to current\_level\_approx.size() \- 1:  
            a\_l\_k \= current\_level\_approx\[k\] // Coef. de Aprox (ej. 25 en Fig 5\)

            // 5\. Buscar el coeficiente de Detalle correspondiente  
            // Busca en la cola 'D' un detalle 'd' con nivel 'l' e índice 'k'  
            d\_l\_k \= D.find(level=l, index=k)   
              
            if d\_l\_k is null:  
                d\_l\_k \= 0 // Si fue descartado (no en Top-K), es 0

            // 6\. Invertir la Wavelet de Haar  
            // (a\_l-1, 2k-1) \= (a\_l,k \+ d\_l,k) / 2  
            // (a\_l-1, 2k)   \= (a\_l,k \- d\_l,k) / 2  
            // NOTA: El paper omite el /2, ya que es una constante  
            // de escala que se puede aplicar al final.  
            // Siguiendo la Fig 5:  
            next\_level\_approx\[2\*k\]     \= (a\_l\_k \+ d\_l\_k) // ej. (25 \+ 9\) / 2 \= 17 \-\> 16  
            next\_level\_approx\[2\*k \+ 1\] \= (a\_l\_k \- d\_l\_k) // ej. (25 \- 9\) / 2 \= 8 \-\> 9  
            // (La discrepancia de 1 se debe al redondeo/simplificación en la Fig 5\)

        // 7\. Pasar al siguiente nivel  
        current\_level\_approx \= next\_level\_approx

    // 8\. Resultado Final  
    // 'current\_level\_approx' es ahora el array de Nivel 0,  
    // que es la curva de tasa de flujo reconstruida.  
    return current\_level\_approx 

### **3.5. Implementación en Hardware (P4)**

Para replicar la versión de hardware (Tofino2), el Algoritmo 1 se modifica (ver Figura 7):

* **Paralelismo:** Los cálculos de \_details\[l\].val para *todos los niveles L* (Etapa 3,4) se realizan en *paralelo* en lugar de en un bucle serie.  
* **Aproximación de Top-K:** La priority\_queue (Top-K) es demasiado compleja para P4. Se reemplaza por un método de "branching" y "thresholding" (Etapas 5-7):  
  * **Branching:** Se usan dos colas/filtros separados: uno para niveles pares (D\_even) y otro para impares (D\_odd). Esto simplifica el cálculo de peso a meros *shifts* (\>\>).  
  * **Thresholding:** En lugar de un Top-K real, se usa un umbral (threshold) simple. Si |detail.val| \> threshold\_l, se mantiene. Este umbral se precalcula *offline*.

## **4\. Paso a Paso: Detección de Eventos µEvent (Switch)**

Esto se implementa en un *switch commodity* (ej. Arista 7060CX) usando su pipeline de ACL.

### **4.1. Lógica de Implementación (Pseudocódigo de ACL)**

Necesitas configurar una ACL con múltiples entradas (términos) que se procesan en orden.  
\# \--- Pseudocódigo de Configuración de ACL en un Switch Commodity \---

\# 1\. Definir la acción de espejado  
monitor session 1  
   destination remote\_ip \<IP\_DEL\_ANALIZADOR\_UMON\>  
   source-port \<VLAN\_O\_PUERTO\_ESPEJADO\>  
exit

\# 2\. Crear la Access Control List (ACL)  
ip access-list UMICRO-EVENT-DETECTION

   \# 3\. Término de Muestreo (SAMPLE)  
   \# Muestrea 1/8 de los paquetes (coincide con '000' en los últimos 3 bits del PSN)  
   \# El offset y la máscara dependen del protocolo (RoCEv2 PSN)  
   \# Este es un ejemplo conceptual:  
   10 match ip any any packet-field 0b000 at offset \<OFFSET\_PSN\> mask 0b111

      \# 4\. Término de Evento (MATCH ECN)  
      \# Si el paquete pasó el muestreo, comprueba si es un evento CE  
      \# El offset y la máscara son para el campo ECN  
      20 match ip any any packet-field 0b11 at offset \<OFFSET\_ECN\> mask 0b11

         \# 5\. Acción (MIRROR)  
         \# Si AMBOS términos coinciden (SAMPLE \+ MATCH), espejea  
         30 action mirror session 1

   \# 6\. Permitir el resto del tráfico  
   100 permit ip any any

\# 7\. Aplicar la ACL a la interfaz de entrada  
interface Ethernet1/1  
   ip access-group UMICRO-EVENT-DETECTION in  
exit

**Nota sobre el Muestreo:** La clave es que el muestreo (Término 10\) debe ser *eficiente*. Hacerlo coincidir con los bits bajos del PSN (o un hash) es mucho más rápido que un muestreo aleatorio. El *paper* logra $p=1/64$ (6 bits) o $p=1/8$ (3 bits).

## **5\. Entorno de Replicación y Cargas de Trabajo**

### **5.1. Entorno de Simulación (NS-3)**

* **Topología:** Fat-tree ($k=4$).  
* **Enlaces:** 100 Gbps, 1 µs de latencia por salto.  
* **Protocolo:** RoCEv2 con DCQCN.  
* **Parámetros DCQCN:** KMin \= 20 KiB, KMax \= 200 KiB, Pmax \= 0.01.

### **5.2. Cargas de Trabajo (Workloads)**

Necesitas generar trazas de flujo basadas en las distribuciones de la Figura 16\.

* **Facebook Hadoop:** Muchos flujos pequeños (cola pesada).  
* **WebSearch:** Flujos de tamaño más bimodales.  
* **RDMA (Testbed):** Usa perftest para generar flujos RDMA.

### **5.3. Parámetros de Simulación**

* **Carga:** Ejecuta simulaciones separadas para 15%, 25% y 35% de carga de enlace.  
* **Duración:** 20 ms por ejecución es suficiente para recolectar datos (ver Tabla 2).  
* **Granularidad:** Fija la ventana de WaveSketch en **8.192 µs** (1 \<\< 13 nanosegundos).

## **6\. Guía de Validación (Resultados Esperados)**

Para confirmar que tu replicación es correcta, tus gráficos deben coincidir con los del *paper*.

* **Validación 1: Precisión vs. Memoria (Fig. 11, 12):**  
  * Ejecuta tus implementaciones de WaveSketch-Ideal (C++) y WaveSketch-HW (P4/simulado) contra los *baselines* (Fourier, OmniWindow).  
  * Mide las 4 métricas (Euclidean, ARE, Cosine, Energy).  
  * **Resultado Esperado:** Tus curvas de WaveSketch deben estar significativamente por debajo (para error) o por encima (para similitud) de los *baselines*, especialmente con poca memoria (ej. 200 KB).  
* **Validación 2: Fidelidad Visual (Fig. 13):**  
  * Toma un flujo RDMA del *testbed*.  
  * Compara la curva *Original* (ground truth) con la *Reconstruida* por WaveSketch y OmniWindow.  
  * **Resultado Esperado:** Tu reconstrucción de WaveSketch debe capturar los picos y valles. La de OmniWindow debe verse "plana" o promediada.  
* **Validación 3: Recall de Eventos (Fig. 14):**  
  * Ejecuta la simulación NS-3 con tu ACL de detección de µEvent.  
  * Varía la tasa de muestreo (p=1/1 a 1/256).  
  * Mide cuántos eventos de congestión (donde QueueLength \> X) fueron capturados.  
  * **Resultado Esperado:** Con $p=1/64$, debes capturar **\>99%** de los eventos donde la cola superó KMax (200 KiB).  
* **Validación 4: "Event Replay" (Fig. 10):**  
  * Este es el *test final*.  
  * Toma un evento de congestión detectado por tu ACL (ej. en el tiempo t=11800 en la Fig. 10).  
  * Toma los IDs de flujo de los paquetes espejeados.  
  * Consulta tu Analizador µΜΟΝ para reconstruir (Algoritmo 2\) las curvas de esos flujos alrededor del tiempo t.  
  * **Resultado Esperado:** Debes poder generar un gráfico como el 10c, mostrando los flujos (rosa, verde) que causaron la congestión y cómo sus tasas cambiaron durante el evento.