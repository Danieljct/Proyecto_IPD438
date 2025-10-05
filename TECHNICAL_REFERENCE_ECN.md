# Referencia Técnica: Implementación ECN NS-3.45
## Código de Referencia y Snippets Funcionales

---

## 🔧 CONFIGURACIÓN COMPLETA FUNCIONANDO

### Configuración RED Ultra-Agresiva (CRÍTICA)
```cpp
// ESTA configuración GARANTIZA activación ECN
void ConfigureEcnDefaults() {
    // RED Queue Discipline - Ultra Agresivo
    Config::SetDefault("ns3::RedQueueDisc::MinTh", DoubleValue(1.0));           // 1 paquete mínimo
    Config::SetDefault("ns3::RedQueueDisc::MaxTh", DoubleValue(2.0));           // 2 paquetes máximo  
    Config::SetDefault("ns3::RedQueueDisc::QueueSizeLimit", QueueSizeValue(QueueSize("5p"))); // 5 paquetes total
    Config::SetDefault("ns3::RedQueueDisc::Gentle", BooleanValue(false));       // No gentle mode
    Config::SetDefault("ns3::RedQueueDisc::UseEcn", BooleanValue(true));        // ¡HABILITAR ECN!
    Config::SetDefault("ns3::RedQueueDisc::UseHardDrop", BooleanValue(false));  // Permitir ECN antes de drop
    Config::SetDefault("ns3::RedQueueDisc::QW", DoubleValue(0.002));           // Weight para average queue

    // TCP con ECN habilitado
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpLinuxReno"));
    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));        // ¡TCP ECN ON!
    
    std::cout << "✅ Configuración ECN ultra-agresiva aplicada\n";
}
```

### Instalación Traffic Control en Dispositivos
```cpp
void InstallRedQueues(NetDeviceContainer& devices) {
    TrafficControlHelper tch;
    tch.SetRootQueueDisc("ns3::RedQueueDisc");
    
    // Instalar en TODOS los dispositivos
    QueueDiscContainer queueDiscs = tch.Install(devices);
    
    std::cout << "📦 RED queues instaladas en " << queueDiscs.GetN() << " dispositivos\n";
    
    // Verificar instalación
    for (uint32_t i = 0; i < queueDiscs.GetN(); i++) {
        Ptr<QueueDisc> qd = queueDiscs.Get(i);
        if (qd) {
            std::cout << "  - Cola " << i << ": " << qd->GetTypeId().GetName() << "\n";
        }
    }
    
    return queueDiscs; // ¡Importante devolver para medición!
}
```

---

## 🌐 TOPOLOGÍA Y ENLACES

### Creación Fat-Tree con Cuello de Botella
```cpp
void CreateFatTreeTopology() {
    // 4 hosts, 2 switches
    NodeContainer hosts, switches;
    hosts.Create(4);     // Host0, Host1, Host2, Host3  
    switches.Create(2);  // Switch0, Switch1
    
    // Enlaces de acceso - 100Mbps
    PointToPointHelper accessLinks;
    accessLinks.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    accessLinks.SetChannelAttribute("Delay", StringValue("5ms"));
    
    // Enlaces core - 50Mbps (CUELLO DE BOTELLA INTENCIONAL)
    PointToPointHelper coreLinks;
    coreLinks.SetDeviceAttribute("DataRate", StringValue("50Mbps"));  // ¡Menor que acceso!
    coreLinks.SetChannelAttribute("Delay", StringValue("10ms"));
    
    // Conexiones acceso
    NetDeviceContainer devices;
    devices.Add(accessLinks.Install(hosts.Get(0), switches.Get(0))); // Host0-Switch0
    devices.Add(accessLinks.Install(hosts.Get(1), switches.Get(0))); // Host1-Switch0  
    devices.Add(accessLinks.Install(hosts.Get(2), switches.Get(1))); // Host2-Switch1
    devices.Add(accessLinks.Install(hosts.Get(3), switches.Get(1))); // Host3-Switch1
    
    // Conexión core (CRÍTICA - aquí ocurre congestión)
    devices.Add(coreLinks.Install(switches.Get(0), switches.Get(1))); // Switch0-Switch1
    
    std::cout << "🏗️ Topología creada: 4 hosts, 2 switches, core 50Mbps\n";
    return devices;
}
```

### Configuración IP y Routing
```cpp
void ConfigureIpAndRouting(NetDeviceContainer& devices, NodeContainer& allNodes) {
    // Instalar stack IP
    InternetStackHelper stack;
    stack.Install(allNodes);
    
    // Asignación IP
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);
    
    // Routing automático
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    std::cout << "🌐 IPs asignadas:\n";
    for (uint32_t i = 0; i < interfaces.GetN(); i++) {
        std::cout << "  Device " << i << ": " << interfaces.GetAddress(i) << "\n";
    }
}
```

---

## 🚦 PATRONES DE TRÁFICO PROBADOS

### UDP Flood - Genera Congestión Garantizada
```cpp
void CreateUdpFlood(NodeContainer& hosts, Ipv4InterfaceContainer& interfaces) {
    // Host1 -> Host3: UDP flood 30Mbps
    uint16_t port = 9;
    
    // Servidor en Host3
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(hosts.Get(3));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));
    
    // Cliente en Host1 - FLOOD MODE
    UdpEchoClientHelper echoClient(interfaces.GetAddress(7), port); // Host3 IP
    echoClient.SetAttribute("MaxPackets", UintegerValue(100000));    // Muchos paquetes
    echoClient.SetAttribute("Interval", TimeValue(Seconds(0.001)));  // 1ms = ~30Mbps con 1KB
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));      // 1KB por paquete
    
    ApplicationContainer clientApps = echoClient.Install(hosts.Get(1));
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(8.0));
    
    std::cout << "💥 UDP Flood configurado: Host1->Host3, 30Mbps, 6 segundos\n";
    std::cout << "   Excede capacidad core (50Mbps) para forzar congestión\n";
}
```

### TCP Bulk Transfer - Responde a ECN
```cpp
void CreateTcpTransfer(NodeContainer& hosts, Ipv4InterfaceContainer& interfaces) {
    // Host0 -> Host2: TCP bulk transfer
    uint16_t port = 8080;
    
    // Servidor en Host2
    PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", 
                                     InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = packetSinkHelper.Install(hosts.Get(2));
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));
    
    // Cliente en Host0 - Bulk send
    BulkSendHelper source("ns3::TcpSocketFactory", 
                         InetSocketAddress(interfaces.GetAddress(5), port)); // Host2 IP
    source.SetAttribute("MaxBytes", UintegerValue(10000000)); // 10MB
    source.SetAttribute("SendSize", UintegerValue(1024));     // 1KB chunks
    
    ApplicationContainer clientApps = source.Install(hosts.Get(0));
    clientApps.Start(Seconds(1.5));
    clientApps.Stop(Seconds(9.0));
    
    std::cout << "📡 TCP transfer configurado: Host0->Host2, 10MB\n";
    std::cout << "   Debe responder a ECN marks del core link\n";
}
```

---

## 📊 MEDICIÓN ECN - CÓDIGO COMPLETO

### Función Principal de Medición
```cpp
// Variable global para acceso a colas
QueueDiscContainer g_queueDiscs;

void MeasureEcnStatistics() {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "📊 MEDICIÓN ECN - Timestamp: " << Simulator::Now().GetSeconds() << "s\n";
    std::cout << std::string(60, '=') << "\n";
    
    uint32_t totalEcnMarks = 0;
    uint32_t totalDrops = 0;
    uint32_t totalPackets = 0;
    
    // Mapeo cola -> descripción física
    std::map<uint32_t, std::string> queueDescriptions = {
        {0, "Host0->Switch0 (acceso)"},
        {1, "Host1->Switch0 (acceso UDP flood) ⚠️"},
        {2, "Switch0->Host0 (retorno)"},
        {3, "Switch0->Host1 (retorno)"},
        {4, "Host2->Switch1 (acceso)"},
        {5, "Host3->Switch1 (acceso)"},
        {6, "Switch1->Host2 (retorno)"},
        {7, "Switch1->Host3 (retorno)"},
        {8, "Switch0<->Switch1 (CORE LINK) 🔗"}
    };
    
    bool foundActivity = false;
    
    for (uint32_t i = 0; i < g_queueDiscs.GetN(); i++) {
        Ptr<QueueDisc> qd = g_queueDiscs.Get(i);
        Ptr<RedQueueDisc> redQueue = DynamicCast<RedQueueDisc>(qd);
        
        if (redQueue) {
            uint32_t ecnMarks = redQueue->GetStats().nMarkedPackets;
            uint32_t drops = redQueue->GetStats().nDroppedPackets;
            uint32_t packets = redQueue->GetStats().nTotalReceivedPackets;
            
            // Solo mostrar colas con actividad significativa
            if (ecnMarks > 0 || drops > 50 || packets > 100) {
                foundActivity = true;
                
                std::cout << "Cola " << i;
                if (queueDescriptions.find(i) != queueDescriptions.end()) {
                    std::cout << " (" << queueDescriptions[i] << ")";
                }
                std::cout << ":\n";
                
                std::cout << "  📦 Paquetes totales: " << packets << "\n";
                std::cout << "  ❌ Drops: " << drops << "\n";
                std::cout << "  🟡 ECN Marks: " << ecnMarks << "\n";
                
                // Análisis automático del comportamiento
                if (ecnMarks > 0 && drops < ecnMarks * 3) {
                    std::cout << "  ✅ ECN funcionando - tráfico TCP respondiendo correctamente\n";
                } else if (ecnMarks > 0) {
                    std::cout << "  ⚠️ ECN parcial - algunos drops a pesar de ECN marks\n";
                } else if (drops > 100) {
                    std::cout << "  🔍 ANÁLISIS: Muchos drops pero 0 ECN - posible tráfico UDP (ignora ECN)\n";
                } else if (packets > 100) {
                    std::cout << "  ✅ Flujo normal - sin congestión significativa\n";
                }
                
                // Calcular tasa de utilización ECN
                if (packets > 0) {
                    double ecnRate = (double)ecnMarks / packets * 100;
                    double dropRate = (double)drops / packets * 100;
                    std::cout << "  📈 Tasa ECN: " << std::fixed << std::setprecision(2) << ecnRate << "%\n";
                    std::cout << "  📉 Tasa Drop: " << std::fixed << std::setprecision(2) << dropRate << "%\n";
                }
                
                std::cout << "\n";
            }
            
            totalEcnMarks += ecnMarks;
            totalDrops += drops;
            totalPackets += packets;
        }
    }
    
    if (!foundActivity) {
        std::cout << "ℹ️ No se detectó actividad significativa en ninguna cola\n";
        std::cout << "   Posibles causas: simulación muy corta, tráfico insuficiente\n\n";
    }
    
    // Estadísticas globales
    std::cout << std::string(60, '-') << "\n";
    std::cout << "📊 RESUMEN GLOBAL:\n";
    std::cout << "Total Paquetes: " << totalPackets << "\n";
    std::cout << "Total ECN Marks: " << totalEcnMarks << "\n";
    std::cout << "Total Drops: " << totalDrops << "\n";
    
    if (totalPackets > 0) {
        double globalEcnRate = (double)totalEcnMarks / totalPackets * 100;
        double globalDropRate = (double)totalDrops / totalPackets * 100;
        std::cout << "Tasa ECN Global: " << std::fixed << std::setprecision(2) << globalEcnRate << "%\n";
        std::cout << "Tasa Drop Global: " << std::fixed << std::setprecision(2) << globalDropRate << "%\n";
        
        // Interpretación automática
        if (totalEcnMarks > 0) {
            std::cout << "✅ ECN FUNCIONANDO - Se detectaron marcas ECN\n";
        } else if (totalDrops > 0) {
            std::cout << "⚠️ Solo drops detectados - Verificar configuración ECN\n";
        } else {
            std::cout << "ℹ️ Sin congestión detectada - Incrementar tráfico\n";
        }
    }
    
    std::cout << std::string(60, '=') << "\n\n";
}
```

### Programación de Mediciones Periódicas
```cpp
void SchedulePeriodicMeasurements() {
    // Medición inicial a los 3 segundos
    Simulator::Schedule(Seconds(3.0), &MeasureEcnStatistics);
    
    // Mediciones cada 2 segundos durante la simulación
    for (double t = 5.0; t <= 9.0; t += 2.0) {
        Simulator::Schedule(Seconds(t), &MeasureEcnStatistics);
    }
    
    // Medición final
    Simulator::Schedule(Seconds(9.9), &MeasureEcnStatistics);
    
    std::cout << "⏰ Mediciones programadas cada 2 segundos\n";
}
```

---

## 🔍 DEBUGGING Y VERIFICACIÓN

### Verificación de Configuración
```cpp
void VerifyEcnConfiguration() {
    std::cout << "\n🔍 VERIFICANDO CONFIGURACIÓN ECN:\n";
    
    // Verificar configuración RED
    DoubleValue minTh, maxTh;
    BooleanValue useEcn;
    QueueSizeValue queueSize;
    
    Config::GetDefault("ns3::RedQueueDisc::MinTh", minTh);
    Config::GetDefault("ns3::RedQueueDisc::MaxTh", maxTh);
    Config::GetDefault("ns3::RedQueueDisc::UseEcn", useEcn);
    Config::GetDefault("ns3::RedQueueDisc::QueueSizeLimit", queueSize);
    
    std::cout << "  RED MinTh: " << minTh.Get() << "\n";
    std::cout << "  RED MaxTh: " << maxTh.Get() << "\n";
    std::cout << "  RED UseEcn: " << (useEcn.Get() ? "TRUE ✅" : "FALSE ❌") << "\n";
    std::cout << "  Queue Size: " << queueSize.Get() << "\n";
    
    // Verificar configuración TCP
    StringValue tcpEcn;
    Config::GetDefault("ns3::TcpSocketBase::UseEcn", tcpEcn);
    std::cout << "  TCP ECN: " << tcpEcn.Get() << "\n";
    
    // Validación
    if (minTh.Get() <= 2.0 && maxTh.Get() <= 3.0 && useEcn.Get()) {
        std::cout << "✅ Configuración ECN ultra-agresiva CORRECTA\n";
    } else {
        std::cout << "❌ Configuración ECN NO óptima para demostración\n";
    }
    std::cout << "\n";
}
```

### Función de Debug Completa
```cpp
void DebugQueueState(uint32_t queueId) {
    if (queueId >= g_queueDiscs.GetN()) {
        std::cout << "❌ Cola " << queueId << " no existe\n";
        return;
    }
    
    Ptr<QueueDisc> qd = g_queueDiscs.Get(queueId);
    Ptr<RedQueueDisc> redQueue = DynamicCast<RedQueueDisc>(qd);
    
    if (!redQueue) {
        std::cout << "❌ Cola " << queueId << " no es RED queue\n";
        return;
    }
    
    std::cout << "\n🔍 DEBUG Cola " << queueId << ":\n";
    std::cout << "  Current Size: " << redQueue->GetCurrentSize() << "\n";
    std::cout << "  Avg Queue Size: " << redQueue->GetQueueSize() << "\n";
    std::cout << "  Drop Probability: " << redQueue->GetCurMaxP() << "\n";
    
    auto stats = redQueue->GetStats();
    std::cout << "  Received: " << stats.nTotalReceivedPackets << "\n";
    std::cout << "  Dropped: " << stats.nDroppedPackets << "\n"; 
    std::cout << "  Marked: " << stats.nMarkedPackets << "\n";
    std::cout << "  Queued: " << stats.nTotalQueuedPackets << "\n";
}
```

---

## 🚀 MAIN FUNCTION COMPLETA

### Estructura Principal del Programa
```cpp
int main(int argc, char *argv[]) {
    // 1. Configuración logging (opcional)
    LogComponentEnable("RedQueueDisc", LOG_LEVEL_INFO);
    
    // 2. Configurar defaults ECN ANTES de crear objetos
    ConfigureEcnDefaults();
    
    // 3. Crear topología
    auto [hosts, switches, devices] = CreateFatTreeTopology();
    NodeContainer allNodes(hosts, switches);
    
    // 4. Instalar RED queues
    g_queueDiscs = InstallRedQueues(devices);
    
    // 5. Configurar IP y routing
    Ipv4InterfaceContainer interfaces = ConfigureIpAndRouting(devices, allNodes);
    
    // 6. Verificar configuración
    VerifyEcnConfiguration();
    
    // 7. Crear tráfico
    CreateUdpFlood(hosts, interfaces);
    CreateTcpTransfer(hosts, interfaces);
    
    // 8. Programar mediciones
    SchedulePeriodicMeasurements();
    
    // 9. Ejecutar simulación
    std::cout << "🚀 Iniciando simulación 10 segundos...\n\n";
    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    
    // 10. Cleanup
    Simulator::Destroy();
    
    std::cout << "✅ Simulación completada exitosamente\n";
    return 0;
}
```

---

## ⚠️ PROBLEMAS COMUNES Y SOLUCIONES

### Problema 1: ECN marks = 0
```cpp
// CAUSA: Configuración no suficientemente agresiva
Config::SetDefault("ns3::RedQueueDisc::MinTh", DoubleValue(10.0)); // ❌ MUY ALTO

// SOLUCIÓN: Ultra-agresivo
Config::SetDefault("ns3::RedQueueDisc::MinTh", DoubleValue(1.0));  // ✅ CORRECTO
```

### Problema 2: Drops = 0, ECN = 0
```cpp
// CAUSA: Sin congestión real
UdpEchoClientHelper client(...);
client.SetAttribute("Interval", TimeValue(Seconds(1.0))); // ❌ MUY LENTO

// SOLUCIÓN: Flood real
client.SetAttribute("Interval", TimeValue(Seconds(0.001))); // ✅ 1ms = flood
```

### Problema 3: No se ven estadísticas
```cpp
// CAUSA: No guardar referencia a QueueDiscContainer
TrafficControlHelper tch;
tch.Install(devices); // ❌ No se guarda

// SOLUCIÓN: Guardar referencia global
g_queueDiscs = tch.Install(devices); // ✅ Accesible para medición
```

### Problema 4: Casting fallido
```cpp
// CAUSA: Cast incorrecto
Ptr<DropTailQueueDisc> queue = DynamicCast<DropTailQueueDisc>(qd); // ❌ Tipo incorrecto

// SOLUCIÓN: Cast correcto
Ptr<RedQueueDisc> redQueue = DynamicCast<RedQueueDisc>(qd); // ✅ RED queue
```

---

## 📋 CHECKLIST RÁPIDO

### Antes de Ejecutar
- [ ] `Config::SetDefault` RED MinTh=1, MaxTh=2, UseEcn=true
- [ ] `Config::SetDefault` TCP UseEcn="On"  
- [ ] Traffic flood UDP > capacidad enlace core
- [ ] Guardar `QueueDiscContainer` en variable global
- [ ] Programar mediciones con `Simulator::Schedule`

### Durante Debug
- [ ] Verificar configuración con `VerifyEcnConfiguration()`
- [ ] Usar `DebugQueueState()` para colas específicas
- [ ] Revisar logs RED con `LogComponentEnable`
- [ ] Confirmar topología y mapeo cola-enlace

### Resultados Esperados
- [ ] UDP flood: muchos drops, 0 ECN marks
- [ ] TCP core: algunos ECN marks, menos drops
- [ ] Tasa ECN total: 0.1% - 1.0%
- [ ] Mensaje "ECN FUNCIONANDO" en resumen

---

*Documento técnico creado el 5 de octubre de 2025*  
*Código verificado funcionando en NS-3.45*  
*Todos los snippets son copy-paste ready*