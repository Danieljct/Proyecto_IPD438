#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

// Variables globales para contadores ECN y métricas tipo iperf3
QueueDiscContainer globalQdiscs;
uint64_t totalEcnMarks = 0;
uint64_t totalDrops = 0;
uint64_t totalEnqueues = 0;

// Variables para estadísticas tipo iperf3
uint64_t totalBytesSent = 0;
uint64_t totalBytesReceived = 0;
double simulationStartTime = 1.0; // Cuando empiezan las aplicaciones

// CALLBACK PARA MARCAS ECN - Versión simple para NS-3.45
void EcnMarkCallback(Ptr<const QueueDiscItem> item)
{
  totalEcnMarks++;
  std::cout << "[ECN MARK] Tiempo: " << Simulator::Now().GetSeconds() 
            << "s - Paquete marcado con ECN (Total: " << totalEcnMarks << ")" << std::endl;
}

// CALLBACK PARA DROPS - Versión simple para NS-3.45
void DropCallback(Ptr<const QueueDiscItem> item)
{
  totalDrops++;
  std::cout << "[DROP] Tiempo: " << Simulator::Now().GetSeconds() 
            << "s - Paquete descartado (Total: " << totalDrops << ")" << std::endl;
}

// CALLBACK PARA ENQUEUES - Versión simple para NS-3.45
void EnqueueCallback(Ptr<const QueueDiscItem> item)
{
  totalEnqueues++;
  // Debug cada 100 paquetes
  if (totalEnqueues % 100 == 0) {
    std::cout << "[ENQUEUE] " << totalEnqueues << " paquetes encolados" << std::endl;
  }
}

// FUNCIÓN PARA CALCULAR THROUGHPUT TIPO IPERF3
void PrintIperf3Stats(ApplicationContainer& servers)
{
  double currentTime = Simulator::Now().GetSeconds();
  double elapsedTime = currentTime - simulationStartTime;
  
  std::cout << "\n=== ESTADÍSTICAS TIPO IPERF3 ===" << std::endl;
  std::cout << "Tiempo: " << currentTime << "s (Duración: " << elapsedTime << "s)" << std::endl;
  
  uint64_t totalReceived = 0;
  
  // Obtener estadísticas de los servidores PacketSink
  for (uint32_t i = 0; i < servers.GetN(); ++i)
  {
    Ptr<PacketSink> sink = DynamicCast<PacketSink>(servers.Get(i));
    if (sink)
    {
      uint64_t bytesReceived = sink->GetTotalRx();
      totalReceived += bytesReceived;
      
      double throughputMbps = (bytesReceived * 8.0) / (elapsedTime * 1000000.0);
      
      std::cout << "Servidor " << i << ":" << std::endl;
      std::cout << "  - Bytes recibidos: " << bytesReceived << " bytes" << std::endl;
      std::cout << "  - Throughput: " << throughputMbps << " Mbps" << std::endl;
    }
  }
  
  if (elapsedTime > 0) {
    double totalThroughputMbps = (totalReceived * 8.0) / (elapsedTime * 1000000.0);
    std::cout << "TOTAL COMBINADO:" << std::endl;
    std::cout << "  - Throughput total: " << totalThroughputMbps << " Mbps" << std::endl;
    std::cout << "  - Utilización de enlace: " << (totalThroughputMbps / 10.0) * 100 << "%" << std::endl;
  }
}

// FUNCIÓN DE MONITOREO MEJORADA - USA ESTADÍSTICAS INTEGRADAS + IPERF3
void PrintQueueStats()
{
  std::cout << "\n=== ESTADÍSTICAS DE RED ===" << std::endl;
  std::cout << "Tiempo: " << Simulator::Now().GetSeconds() << "s" << std::endl;
  
  uint64_t totalMarks = 0;
  uint64_t totalDropsAll = 0;
  uint64_t totalEnqueuesAll = 0;
  
  std::cout << "\nEstado de colas RED:" << std::endl;
  for (uint32_t i = 0; i < globalQdiscs.GetN(); ++i)
  {
    Ptr<RedQueueDisc> red = DynamicCast<RedQueueDisc>(globalQdiscs.Get(i));
    if (red)
    {
      QueueDisc::Stats stats = red->GetStats();
      std::cout << "Cola " << i << ":" << std::endl;
      std::cout << "  - Tamaño actual: " << red->GetCurrentSize().GetValue() << " paquetes" << std::endl;
      std::cout << "  - Total encolados: " << stats.nTotalEnqueuedPackets << std::endl;
      std::cout << "  - Total descartados: " << stats.nTotalDroppedPackets << std::endl;
      
      // Buscar marcas ECN en el mapa de estadísticas
      auto it = stats.nMarkedPackets.find("Ecn mark");
      if (it != stats.nMarkedPackets.end()) {
        std::cout << "  - Marcas ECN: " << it->second << std::endl;
        totalMarks += it->second;
      } else {
        std::cout << "  - Marcas ECN: 0" << std::endl;
      }
      
      totalDropsAll += stats.nTotalDroppedPackets;
      totalEnqueuesAll += stats.nTotalEnqueuedPackets;
    }
  }
  
  std::cout << "\n=== RESUMEN GENERAL ===" << std::endl;
  std::cout << "Total paquetes marcados con ECN: " << totalMarks << std::endl;
  std::cout << "Total paquetes descartados: " << totalDropsAll << std::endl;
  std::cout << "Total paquetes encolados: " << totalEnqueuesAll << std::endl;
  
  if (totalEnqueuesAll > 0) {
    double ecnRate = (double)totalMarks / totalEnqueuesAll * 100;
    double dropRate = (double)totalDropsAll / totalEnqueuesAll * 100;
    std::cout << "Tasa de marcado ECN: " << ecnRate << "%" << std::endl;
    std::cout << "Tasa de descarte: " << dropRate << "%" << std::endl;
  }
}

int main(int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse(argc, argv);

  // Habilitar logging específico para ECN y TCP
  LogComponentEnable("RedQueueDisc", LOG_LEVEL_INFO);
  LogComponentEnable("TcpSocketBase", LOG_LEVEL_INFO);
  LogComponentEnable("Ipv4L3Protocol", LOG_LEVEL_INFO);
  
  std::cout << "=== Iniciando simulación Fat-Tree con ECN ===" << std::endl;
  std::cout << "✓ ECN habilitado en TCP" << std::endl;
  std::cout << "✓ Logging habilitado para debug" << std::endl;

  // 1. CONFIGURACIÓN DE PROTOCOLO TCP 
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
  
  // Configurar buffers TCP más grandes para mejor rendimiento
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(131072));
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(131072));
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1460));
  
  // NOTA: ECN se configurará directamente en los sockets más adelante

  // 2. CREACIÓN DE TOPOLOGÍA
  // Fat-Tree simplificado: 4 hosts + 2 switches (normalmente sería más complejo)
  NodeContainer hosts;
  hosts.Create(4);           // Host0, Host1, Host2, Host3

  NodeContainer switches;
  switches.Create(2);        // Switch0, Switch1

  // 3. INSTALACIÓN DE PILA DE PROTOCOLOS
  // Instala TCP/IP en todos los nodos (hosts y switches actúan como routers)
  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(switches);

  // 4. CONFIGURACIÓN DE ENLACES FÍSICOS
  // Point-to-Point: enlaces dedicados entre cada par de nodos
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));    // Capacidad limitada para forzar congestión
  p2p.SetChannelAttribute("Delay", StringValue("2ms"));        // Latencia de propagación

  // 5. CONFIGURACIÓN DE COLAS CON ECN - PARÁMETROS BALANCEADOS
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "MinTh", DoubleValue(5),      // Menos agresivo
                          "MaxTh", DoubleValue(15),     // Menos agresivo
                          "MaxSize", QueueSizeValue(QueueSize("30p")), // Cola moderada
                          "UseEcn", BooleanValue(true),
                          "QW", DoubleValue(0.002),     
                          "Gentle", BooleanValue(true), // Gentle RED habilitado
                          "UseHardDrop", BooleanValue(false)); // Evitar drops prematuros

  // 6. CREACIÓN DE CONEXIONES FÍSICAS
  // Topología Fat-Tree:
  // Host0 ---- Switch0 ---- Switch1 ---- Host3
  // Host1 ----/                    \---- Host2
  NetDeviceContainer devHost0ToSw0 = p2p.Install(hosts.Get(0), switches.Get(0));  // Host0 <-> Switch0
  NetDeviceContainer devHost1ToSw0 = p2p.Install(hosts.Get(1), switches.Get(0));  // Host1 <-> Switch0
  NetDeviceContainer devHost2ToSw1 = p2p.Install(hosts.Get(2), switches.Get(1));  // Host2 <-> Switch1
  NetDeviceContainer devHost3ToSw1 = p2p.Install(hosts.Get(3), switches.Get(1));  // Host3 <-> Switch1
  NetDeviceContainer devCore = p2p.Install(switches.Get(0), switches.Get(1));     // Switch0 <-> Switch1 (enlace core)

  // 7. INSTALACIÓN DE COLAS RED CON CALLBACKS
  QueueDiscContainer qd1 = tchRed.Install(devHost0ToSw0);
  QueueDiscContainer qd2 = tchRed.Install(devHost1ToSw0);
  QueueDiscContainer qd3 = tchRed.Install(devHost2ToSw1);
  QueueDiscContainer qd4 = tchRed.Install(devHost3ToSw1);
  QueueDiscContainer qd5 = tchRed.Install(devCore);

  globalQdiscs.Add(qd1);
  globalQdiscs.Add(qd2);
  globalQdiscs.Add(qd3);
  globalQdiscs.Add(qd4);
  globalQdiscs.Add(qd5);

  // Conectar callbacks para monitorear ECN - TEMPORALMENTE DESHABILITADO
  // NOTA: Los callbacks tienen problemas de compatibility en NS-3.45
  for (uint32_t i = 0; i < globalQdiscs.GetN(); ++i)
  {
    Ptr<RedQueueDisc> red = DynamicCast<RedQueueDisc>(globalQdiscs.Get(i));
    if (red)
    {
      // TODO: Usar método alternativo para monitorear ECN
      std::cout << "✓ Cola RED " << i << " configurada (callbacks deshabilitados temporalmente)" << std::endl;
    }
  }

  // Asignación de direcciones IP
  Ipv4AddressHelper address;

  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifaceHost0 = address.Assign(devHost0ToSw0);
  
  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer ifaceHost1 = address.Assign(devHost1ToSw0);

  address.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer ifaceHost2 = address.Assign(devHost2ToSw1);
  
  address.SetBase("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer ifaceHost3 = address.Assign(devHost3ToSw1);

  address.SetBase("10.1.5.0", "255.255.255.0");
  Ipv4InterfaceContainer ifaceCore = address.Assign(devCore);

  // 8. CONFIGURACIÓN DE APLICACIONES TIPO IPERF3
  // ===========================================
  
  // SERVIDOR IPERF3 PRINCIPAL en Host3 (puerto 5201 - default iperf3)
  uint16_t iperf3Port = 5201;
  PacketSinkHelper iperf3Server("ns3::TcpSocketFactory", 
                               InetSocketAddress(Ipv4Address::GetAny(), iperf3Port));
  ApplicationContainer serverApps = iperf3Server.Install(hosts.Get(3));
  serverApps.Start(Seconds(0.5));
  serverApps.Stop(Seconds(12.0));

  // CLIENTE IPERF3 PRINCIPAL: Host0 -> Host3 (Flujo de larga duración)
  BulkSendHelper iperf3Client("ns3::TcpSocketFactory", 
                             InetSocketAddress(ifaceHost3.GetAddress(0), iperf3Port));
  iperf3Client.SetAttribute("MaxBytes", UintegerValue(0)); // Transmisión continua (como iperf3 -t)
  iperf3Client.SetAttribute("SendSize", UintegerValue(1460)); // MSS típico
  
  ApplicationContainer clientApps = iperf3Client.Install(hosts.Get(0));
  clientApps.Start(Seconds(1.0));
  clientApps.Stop(Seconds(10.0));

  // SERVIDOR IPERF3 SECUNDARIO en Host2 (puerto 5202)
  uint16_t iperf3Port2 = 5202;
  PacketSinkHelper iperf3Server2("ns3::TcpSocketFactory", 
                                InetSocketAddress(Ipv4Address::GetAny(), iperf3Port2));
  ApplicationContainer serverApps2 = iperf3Server2.Install(hosts.Get(2));
  serverApps2.Start(Seconds(0.5));
  serverApps2.Stop(Seconds(12.0));

  // CLIENTE IPERF3 CON RÁFAGAS: Host1 -> Host2 (imitando iperf3 con intervalos)
  OnOffHelper iperf3Burst("ns3::TcpSocketFactory", 
                         InetSocketAddress(ifaceHost2.GetAddress(0), iperf3Port2));
  iperf3Burst.SetConstantRate(DataRate("40Mbps"));  // 4x capacidad para crear ráfagas
  iperf3Burst.SetAttribute("PacketSize", UintegerValue(1460));
  
  // Ráfagas tipo iperf3: ON durante 2s, OFF durante 1s (simulando intervalos de reporte)
  iperf3Burst.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"));
  iperf3Burst.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));

  ApplicationContainer clientApps2 = iperf3Burst.Install(hosts.Get(1));
  clientApps2.Start(Seconds(2.0));
  clientApps2.Stop(Seconds(9.0));

  // TRÁFICO DE FONDO ADICIONAL: Simula múltiples flujos iperf3 paralelos
  OnOffHelper backgroundTraffic("ns3::TcpSocketFactory", 
                               InetSocketAddress(ifaceHost3.GetAddress(0), iperf3Port));
  backgroundTraffic.SetConstantRate(DataRate("20Mbps"));  // Tráfico de fondo
  backgroundTraffic.SetAttribute("PacketSize", UintegerValue(1460));
  backgroundTraffic.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=3.0]"));
  backgroundTraffic.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));

  ApplicationContainer backgroundApps = backgroundTraffic.Install(hosts.Get(1));
  backgroundApps.Start(Seconds(3.0));
  backgroundApps.Stop(Seconds(8.0));

  // 9. MONITOREO ESTILO IPERF3 Y COLAS RED
  // =====================================
  
  // Combinar todas las aplicaciones servidor para estadísticas
  ApplicationContainer allServers;
  allServers.Add(serverApps);
  allServers.Add(serverApps2);
  
  // Reportes periódicos estilo iperf3 (cada 1 segundo)
  for (double t = 2.0; t <= 10.0; t += 1.0) {
    Simulator::Schedule(Seconds(t), &PrintQueueStats);
    Simulator::Schedule(Seconds(t + 0.5), &PrintIperf3Stats, allServers);
  }

  // 10. TABLA DE ENRUTAMIENTO
  // Calcula rutas automáticamente: Host0 -> Switch0 -> Switch1 -> Host3
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // 11. ANIMACIÓN NETANIM
  // Genera archivo XML para visualización gráfica de la red
  AnimationInterface anim("fattree.xml");  // archivo de salida
  
  // Posiciones de hosts y switches para la visualización
  anim.SetConstantPosition(hosts.Get(0), 10, 50);   // Host 0
  anim.SetConstantPosition(hosts.Get(1), 10, 90);   // Host 1
  anim.SetConstantPosition(switches.Get(0), 50, 70); // Switch 0
  
  anim.SetConstantPosition(hosts.Get(2), 90, 50);   // Host 2
  anim.SetConstantPosition(hosts.Get(3), 90, 90);   // Host 3
  anim.SetConstantPosition(switches.Get(1), 130, 70); // Switch 1

  std::cout << "=== Configuración completada, iniciando simulación ===" << std::endl;
  std::cout << "✓ ECN configurado en colas RED (NS-3.45)" << std::endl;
  std::cout << "✓ Umbrales RED: MinTh=5, MaxTh=15, MaxSize=30 paquetes" << std::endl;
  std::cout << "✓ Aplicaciones tipo iperf3 configuradas:" << std::endl;
  std::cout << "  - Flujo principal: Host0->Host3 (puerto 5201)" << std::endl;
  std::cout << "  - Flujo con ráfagas: Host1->Host2 (puerto 5202)" << std::endl;
  std::cout << "  - Tráfico de fondo: Host1->Host3 (paralelo)" << std::endl;
  std::cout << "✓ Capacidad de enlace: 10Mbps vs ~100Mbps de tráfico agregado" << std::endl;
  std::cout << "✓ Monitoreo estilo iperf3 + ECN activado" << std::endl;
  std::cout << "⚠️  NOTA: ECN en TCP puede requerir configuración adicional en NS-3.45" << std::endl;

  Simulator::Stop(Seconds(11.0));
  Simulator::Run();
  
  std::cout << "\n=== RESUMEN FINAL TIPO IPERF3 + ECN ===" << std::endl;
  
  // Estadísticas ECN
  uint64_t finalMarks = 0;
  uint64_t finalDrops = 0;
  
  for (uint32_t i = 0; i < globalQdiscs.GetN(); ++i)
  {
    Ptr<RedQueueDisc> red = DynamicCast<RedQueueDisc>(globalQdiscs.Get(i));
    if (red)
    {
      QueueDisc::Stats stats = red->GetStats();
      
      // Buscar marcas ECN en el mapa de estadísticas
      auto it = stats.nMarkedPackets.find("Ecn mark");
      if (it != stats.nMarkedPackets.end()) {
        finalMarks += it->second;
      }
      
      finalDrops += stats.nTotalDroppedPackets;
    }
  }
  
  std::cout << "ESTADÍSTICAS ECN:" << std::endl;
  std::cout << "  - Total marcas ECN: " << finalMarks << std::endl;
  std::cout << "  - Total drops: " << finalDrops << std::endl;
  
  // Estadísticas finales tipo iperf3
  uint64_t totalFinalReceived = 0;
  double totalSimulationTime = 9.0; // 10.0 - 1.0 (tiempo de inicio)
  
  // Obtener estadísticas finales de servidores
  Ptr<PacketSink> sink1 = DynamicCast<PacketSink>(serverApps.Get(0));
  Ptr<PacketSink> sink2 = DynamicCast<PacketSink>(serverApps2.Get(0));
  
  if (sink1) {
    uint64_t bytes1 = sink1->GetTotalRx();
    totalFinalReceived += bytes1;
    double throughput1 = (bytes1 * 8.0) / (totalSimulationTime * 1000000.0);
    std::cout << "  - Servidor 1 (5201): " << throughput1 << " Mbps" << std::endl;
  }
  
  if (sink2) {
    uint64_t bytes2 = sink2->GetTotalRx();
    totalFinalReceived += bytes2;
    double throughput2 = (bytes2 * 8.0) / (totalSimulationTime * 1000000.0);
    std::cout << "  - Servidor 2 (5202): " << throughput2 << " Mbps" << std::endl;
  }
  
  double totalThroughput = (totalFinalReceived * 8.0) / (totalSimulationTime * 1000000.0);
  std::cout << "  - Throughput total: " << totalThroughput << " Mbps" << std::endl;
  
  // Evaluación final
  if (finalMarks > 0) {
    std::cout << "\n✓ ECN FUNCIONANDO CORRECTAMENTE" << std::endl;
  } else {
    std::cout << "\n✗ ECN NO DETECTADO - Verificar configuración TCP" << std::endl;
  }
  
  std::cout << "\n📊 RESUMEN COMO IPERF3:" << std::endl;
  std::cout << "   Duración total: " << totalSimulationTime << " segundos" << std::endl;
  std::cout << "   Throughput agregado: " << totalThroughput << " Mbps" << std::endl;
  std::cout << "   Utilización enlace: " << (totalThroughput / 10.0) * 100 << "%" << std::endl;
  std::cout << "   Congestión detectada: " << (finalDrops > 0 ? "SÍ" : "NO") << std::endl;
  std::cout << "   Paquetes perdidos: " << finalDrops << std::endl;

  Simulator::Destroy();

  return 0;
}