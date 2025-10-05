/**
 * SIMULACIÓN LIMPIA PARA MEDICIÓN DE ECN
 * =====================================
 * 
 * Este código implementa una simulación simplificada para medir 
 * paquetes marcados con ECN (Explicit Congestion Notification)
 * en una topología Fat-Tree con NS-3.45.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include <iomanip>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EcnMeasurement");

// Variables globales para monitoreo ECN
QueueDiscContainer globalQdiscs;

// FUNCIÓN PRINCIPAL PARA MEDIR ECN
void MeasureEcnStatistics()
{
  std::cout << "\n=== MEDICIÓN ECN ===" << std::endl;
  std::cout << "Tiempo: " << Simulator::Now().GetSeconds() << "s" << std::endl;
  
  uint64_t totalEcnMarks = 0;
  uint64_t totalDrops = 0;
  uint64_t totalEnqueues = 0;
  
  std::cout << "\nEstado de colas RED:" << std::endl;
  
  for (uint32_t i = 0; i < globalQdiscs.GetN(); ++i)
  {
    Ptr<RedQueueDisc> red = DynamicCast<RedQueueDisc>(globalQdiscs.Get(i));
    if (red)
    {
      QueueDisc::Stats stats = red->GetStats();
      
      // Contar marcas ECN
      uint64_t ecnMarks = 0;
      for (const auto& pair : stats.nMarkedPackets) {
        ecnMarks += pair.second;
      }
      
      std::cout << "Cola " << i << ": ECN=" << ecnMarks 
                << ", Drops=" << stats.nTotalDroppedPackets 
                << ", Enqueues=" << stats.nTotalEnqueuedPackets << std::endl;
      
      totalEcnMarks += ecnMarks;
      totalDrops += stats.nTotalDroppedPackets;
      totalEnqueues += stats.nTotalEnqueuedPackets;
    }
  }
  
  std::cout << "\n📊 RESUMEN ECN:" << std::endl;
  std::cout << "Total marcas ECN: " << totalEcnMarks << std::endl;
  std::cout << "Total drops: " << totalDrops << std::endl;
  std::cout << "Total enqueues: " << totalEnqueues << std::endl;
  
  if (totalEnqueues > 0) {
    double ecnRate = (double)totalEcnMarks / totalEnqueues * 100;
    double dropRate = (double)totalDrops / totalEnqueues * 100;
    std::cout << "Tasa ECN: " << std::fixed << std::setprecision(2) << ecnRate << "%" << std::endl;
    std::cout << "Tasa Drop: " << std::fixed << std::setprecision(2) << dropRate << "%" << std::endl;
  }
  
  if (totalEcnMarks > 0) {
    std::cout << "✅ ECN DETECTADO - Funcionando correctamente!" << std::endl;
  } else {
    std::cout << "⚠️  ECN no detectado" << std::endl;
  }
}

int main(int argc, char *argv[])
{
  // =====================================================
  // 1. CONFIGURACIÓN INICIAL
  // =====================================================
  
  Time::SetResolution(Time::NS);
  LogComponentEnable("EcnMeasurement", LOG_LEVEL_INFO);
  
  std::cout << "🔬 SIMULACIÓN ECN - Versión Limpia" << std::endl;
  std::cout << "=================================" << std::endl;

  // =====================================================
  // 2. CONFIGURACIÓN TCP CON ECN
  // =====================================================
  
  // Habilitar ECN en TCP
  Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));
  Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(8192));
  Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(8192));
  Config::SetDefault("ns3::TcpSocket::SegmentSize", UintegerValue(1460));
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
  
  std::cout << "✅ ECN habilitado en TCP" << std::endl;

  // =====================================================
  // 3. CREACIÓN DE TOPOLOGÍA FAT-TREE SIMPLIFICADA
  // =====================================================
  
  NodeContainer hosts;
  hosts.Create(4);  // 4 hosts para simplicidad
  
  NodeContainer switches;
  switches.Create(2);  // 2 switches
  
  // Instalar stack TCP/IP
  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(switches);

  // =====================================================
  // 4. CONFIGURACIÓN DE ENLACES
  // =====================================================
  
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("5ms"));

  // =====================================================
  // 5. CONFIGURACIÓN RED PARA ECN (Parámetros optimizados)
  // =====================================================
  
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "MinTh", DoubleValue(1),      // Umbral mínimo muy bajo
                          "MaxTh", DoubleValue(2),      // Umbral máximo muy bajo  
                          "MaxSize", QueueSizeValue(QueueSize("5p")), // Cola pequeña
                          "UseEcn", BooleanValue(true), // ECN habilitado
                          "QW", DoubleValue(1.0),       // Peso máximo para respuesta rápida
                          "Gentle", BooleanValue(true), // Gentle RED activo
                          "UseHardDrop", BooleanValue(false)); // Preferir marcado sobre drop
  
  std::cout << "✅ RED configurado para ECN (MinTh=1, MaxTh=2, QueueSize=5p)" << std::endl;

  // =====================================================
  // 6. CREACIÓN DE CONEXIONES
  // =====================================================
  
  // Topología: Host0-Switch0-Switch1-Host3, Host1-Switch0, Host2-Switch1
  NetDeviceContainer devHost0ToSw0 = p2p.Install(hosts.Get(0), switches.Get(0));
  NetDeviceContainer devHost1ToSw0 = p2p.Install(hosts.Get(1), switches.Get(0));
  NetDeviceContainer devHost2ToSw1 = p2p.Install(hosts.Get(2), switches.Get(1));
  NetDeviceContainer devHost3ToSw1 = p2p.Install(hosts.Get(3), switches.Get(1));
  NetDeviceContainer devCore = p2p.Install(switches.Get(0), switches.Get(1));

  // Instalar RED en todos los dispositivos
  globalQdiscs = tchRed.Install(devHost0ToSw0);
  globalQdiscs.Add(tchRed.Install(devHost1ToSw0));
  globalQdiscs.Add(tchRed.Install(devHost2ToSw1));
  globalQdiscs.Add(tchRed.Install(devHost3ToSw1));
  globalQdiscs.Add(tchRed.Install(devCore));

  // =====================================================
  // 7. ASIGNACIÓN DE DIRECCIONES IP
  // =====================================================
  
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

  // =====================================================
  // 8. CONFIGURACIÓN DE APLICACIONES
  // =====================================================
  
  // SERVIDOR TCP en Host3
  uint16_t tcpPort = 5201;
  PacketSinkHelper tcpServer("ns3::TcpSocketFactory", 
                            InetSocketAddress(Ipv4Address::GetAny(), tcpPort));
  ApplicationContainer serverApps = tcpServer.Install(hosts.Get(3));
  serverApps.Start(Seconds(0.5));
  serverApps.Stop(Seconds(12.0));

  // CLIENTE TCP: Host0 -> Host3
  BulkSendHelper tcpClient("ns3::TcpSocketFactory", 
                          InetSocketAddress(ifaceHost3.GetAddress(0), tcpPort));
  tcpClient.SetAttribute("MaxBytes", UintegerValue(0)); // Transmisión continua
  tcpClient.SetAttribute("SendSize", UintegerValue(1460));
  
  ApplicationContainer clientApps = tcpClient.Install(hosts.Get(0));
  clientApps.Start(Seconds(1.0));
  clientApps.Stop(Seconds(10.0));

  // TRÁFICO UDP AGRESIVO para forzar congestión (clave para activar ECN)
  OnOffHelper udpFlood("ns3::UdpSocketFactory", 
                       InetSocketAddress(ifaceHost3.GetAddress(0), 9999));
  udpFlood.SetConstantRate(DataRate("30Mbps"));  // 6x capacidad del enlace
  udpFlood.SetAttribute("PacketSize", UintegerValue(1024));
  udpFlood.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=8.0]"));
  udpFlood.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.1]"));

  ApplicationContainer udpApps = udpFlood.Install(hosts.Get(1));
  udpApps.Start(Seconds(1.5));
  udpApps.Stop(Seconds(9.5));

  // Servidor UDP
  PacketSinkHelper udpSink("ns3::UdpSocketFactory",
                          InetSocketAddress(Ipv4Address::GetAny(), 9999));
  ApplicationContainer udpSinkApps = udpSink.Install(hosts.Get(3));
  udpSinkApps.Start(Seconds(0.5));
  udpSinkApps.Stop(Seconds(12.0));
  
  std::cout << "✅ Aplicaciones configuradas (TCP + UDP agresivo)" << std::endl;

  // =====================================================
  // 9. CONFIGURACIÓN DE ROUTING
  // =====================================================
  
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // =====================================================
  // 10. PROGRAMAR MEDICIONES ECN
  // =====================================================
  
  // Medir ECN cada 2 segundos
  for (double t = 2.0; t <= 10.0; t += 2.0) {
    Simulator::Schedule(Seconds(t), &MeasureEcnStatistics);
  }

  // =====================================================
  // 11. EJECUTAR SIMULACIÓN
  // =====================================================
  
  std::cout << "\n🚀 Iniciando simulación..." << std::endl;
  std::cout << "⏱️  Duración: 12 segundos" << std::endl;
  std::cout << "📊 Mediciones ECN cada 2 segundos\n" << std::endl;

  Simulator::Stop(Seconds(12.0));
  Simulator::Run();
  
  // Medición final
  std::cout << "\n" << std::string(50, '=') << std::endl;
  std::cout << "📋 MEDICIÓN FINAL ECN" << std::endl;
  std::cout << std::string(50, '=') << std::endl;
  MeasureEcnStatistics();
  
  Simulator::Destroy();
  
  std::cout << "\n✅ Simulación completada" << std::endl;
  return 0;
}