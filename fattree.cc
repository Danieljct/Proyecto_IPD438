#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-module.h"

using namespace ns3;

int main(int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse(argc, argv);

  // Habilitar logging
  LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
  LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

  std::cout << "=== Iniciando simulación Fat-Tree ===" << std::endl;

  // 4 hosts + 2 switches
  NodeContainer hosts;
  hosts.Create(4);

  NodeContainer switches;
  switches.Create(2);

  // Habilitar ECN globalmente
  Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
  Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));

  // Internet stack en hosts
  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(switches);

  // Configurar Point-to-Point con soporte ECN
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("2ms"));

  // Configurar cola RED con ECN
  TrafficControlHelper tchRed;
  tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "LinkBandwidth", StringValue("100Mbps"),
                          "LinkDelay", StringValue("2ms"),
                          "UseEcn", BooleanValue(true),
                          "MinTh", DoubleValue(5),
                          "MaxTh", DoubleValue(15));

  // --- Conectar hosts al switch 0 (enlaces separados P2P)
  NetDeviceContainer devHost0ToSw0 = p2p.Install(hosts.Get(0), switches.Get(0));
  NetDeviceContainer devHost1ToSw0 = p2p.Install(hosts.Get(1), switches.Get(0));
  
  // Instalar colas RED en enlaces hacia switch 0
  tchRed.Install(devHost0ToSw0);
  tchRed.Install(devHost1ToSw0);

  // --- Conectar hosts al switch 1 (enlaces separados P2P)
  NetDeviceContainer devHost2ToSw1 = p2p.Install(hosts.Get(2), switches.Get(1));
  NetDeviceContainer devHost3ToSw1 = p2p.Install(hosts.Get(3), switches.Get(1));
  
  // Instalar colas RED en enlaces hacia switch 1
  tchRed.Install(devHost2ToSw1);
  tchRed.Install(devHost3ToSw1);

  // --- Conectar switch 0 <-> switch 1
  NetDeviceContainer devCore = p2p.Install(switches.Get(0), switches.Get(1));
  tchRed.Install(devCore);

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

  // --- Aplicación: host0 envía a host3
  UdpEchoServerHelper echoServer(9);
  ApplicationContainer serverApps = echoServer.Install(hosts.Get(3));
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(10.0));

  UdpEchoClientHelper echoClient(ifaceHost3.GetAddress(0), 9); // Dirección actualizada
  echoClient.SetAttribute("MaxPackets", UintegerValue(5));
  echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  echoClient.SetAttribute("PacketSize", UintegerValue(1024));

  ApplicationContainer clientApps = echoClient.Install(hosts.Get(0));
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(10.0));

  // --- Activar routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();
  
  // Configurar animación y posiciones de nodos
  AnimationInterface anim("fattree.xml");  // archivo de salida
  
  // Posiciones de hosts y switches para la visualización
  anim.SetConstantPosition(hosts.Get(0), 10, 50);   // Host 0
  anim.SetConstantPosition(hosts.Get(1), 10, 90);   // Host 1
  anim.SetConstantPosition(switches.Get(0), 50, 70); // Switch 0
  
  anim.SetConstantPosition(hosts.Get(2), 90, 50);   // Host 2
  anim.SetConstantPosition(hosts.Get(3), 90, 90);   // Host 3
  anim.SetConstantPosition(switches.Get(1), 130, 70); // Switch 1

  std::cout << "=== Configuración completada, iniciando simulación ===" << std::endl;
  std::cout << "Host0 (" << ifaceHost0.GetAddress(0) << ") enviará 5 paquetes a Host3 (" << ifaceHost3.GetAddress(0) << ")" << std::endl;

  Simulator::Stop(Seconds(11.0));
  Simulator::Run();
  
  std::cout << "=== Simulación terminada ===" << std::endl;
  Simulator::Destroy();

  return 0;
}
