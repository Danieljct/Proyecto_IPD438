#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"

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

  // Internet stack en hosts
  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(switches);

  // Canal (CSMA como red LAN entre cada switch y hosts)
  CsmaHelper csma;
  csma.SetChannelAttribute("DataRate", StringValue("100Mbps"));
  csma.SetChannelAttribute("Delay", TimeValue(NanoSeconds(6560)));

  // --- Conectar hosts 0 y 1 al switch 0
  NodeContainer net1;
  net1.Add(hosts.Get(0));
  net1.Add(hosts.Get(1));
  net1.Add(switches.Get(0));
  NetDeviceContainer devNet1 = csma.Install(net1);

  // --- Conectar hosts 2 y 3 al switch 1
  NodeContainer net2;
  net2.Add(hosts.Get(2));
  net2.Add(hosts.Get(3));
  net2.Add(switches.Get(1));
  NetDeviceContainer devNet2 = csma.Install(net2);

  // --- Conectar switch 0 <-> switch 1
  NodeContainer core;
  core.Add(switches.Get(0));
  core.Add(switches.Get(1));
  NetDeviceContainer devCore = csma.Install(core);

  // Asignación de direcciones IP
  Ipv4AddressHelper address;

  address.SetBase("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer iface1 = address.Assign(devNet1);

  address.SetBase("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer iface2 = address.Assign(devNet2);

  address.SetBase("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer ifaceCore = address.Assign(devCore);

  // --- Aplicación: host0 envía a host3
  UdpEchoServerHelper echoServer(9);
  ApplicationContainer serverApps = echoServer.Install(hosts.Get(3));
  serverApps.Start(Seconds(1.0));
  serverApps.Stop(Seconds(10.0));

  UdpEchoClientHelper echoClient(iface2.GetAddress(1), 9); // host3 está en net2
  echoClient.SetAttribute("MaxPackets", UintegerValue(5));
  echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
  echoClient.SetAttribute("PacketSize", UintegerValue(1024));

  ApplicationContainer clientApps = echoClient.Install(hosts.Get(0));
  clientApps.Start(Seconds(2.0));
  clientApps.Stop(Seconds(10.0));

  // --- Activar routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  std::cout << "=== Configuración completada, iniciando simulación ===" << std::endl;
  std::cout << "Host0 (10.1.1.1) enviará 5 paquetes a Host3 (10.1.2.2)" << std::endl;

  Simulator::Stop(Seconds(11.0));
  Simulator::Run();
  
  std::cout << "=== Simulación terminada ===" << std::endl;
  Simulator::Destroy();

  return 0;
}
