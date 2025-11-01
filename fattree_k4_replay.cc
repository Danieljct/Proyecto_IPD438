/**
 * fatree_k4_replay.cc
 * --------------------
 * Construye una topología Fat-Tree con k = 4 (16 hosts) usando enlaces
 * de 100 Gbps y 1 µs de latencia por salto. Reproduce la traza de tráfico
 * almacenada en hadoop15.csv y genera un CSV con la tasa de cada flujo
 * en el tiempo (ventanas configurables). Incluye un script Python separado
 * para graficar los resultados.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

using namespace ns3;

namespace {
constexpr uint32_t K_VALUE = 4;
constexpr uint32_t HOSTS_PER_EDGE = K_VALUE / 2;         // 2
constexpr uint32_t EDGES_PER_POD = K_VALUE / 2;          // 2
constexpr uint32_t AGGS_PER_POD = K_VALUE / 2;           // 2
constexpr uint32_t POD_COUNT = K_VALUE;                  // 4
constexpr uint32_t CORE_SWITCHES = (K_VALUE / 2) * (K_VALUE / 2); // 4
constexpr uint32_t TOTAL_HOSTS = POD_COUNT * EDGES_PER_POD * HOSTS_PER_EDGE; // 16

const uint16_t UDP_PORT = 9000;
const std::string DEFAULT_INPUT_FILE = "hadoop15.csv";
const std::string DEFAULT_FLOW_CSV = "flow_rate.csv";
} // namespace

class FlowRateLogger {
public:
  FlowRateLogger(uint64_t windowNs, std::string filename)
      : m_windowNs(windowNs), m_filename(std::move(filename)) {}

  void Record(uint32_t /*flowId*/, uint32_t bytes) {
    uint64_t nowNs = Simulator::Now().GetNanoSeconds();
    uint64_t windowIndex = nowNs / m_windowNs;
    m_total[windowIndex] += bytes;
  }

  void WriteCsv() const {
    std::ofstream ofs(m_filename);
    if (!ofs.is_open()) {
      std::cerr << "Error: no se pudo abrir " << m_filename << " para escritura" << std::endl;
      return;
    }
    ofs << "time_s,total_rate_gbps\n";
    for (const auto &bucket : m_total) {
      uint64_t windowIdx = bucket.first;
      uint64_t bytes = bucket.second;
      double timeSeconds = static_cast<double>(windowIdx * m_windowNs) / 1e9;
      double rateGbps = (bytes * 8.0) / static_cast<double>(m_windowNs); // bytes/ns -> Gbps
      ofs << timeSeconds << ',' << rateGbps << '\n';
    }
  }

private:
  uint64_t m_windowNs;
  std::string m_filename;
  std::map<uint64_t, uint64_t> m_total;
};

static std::unique_ptr<FlowRateLogger> g_flowLogger;

void SendPacket(Ptr<Socket> socket, Ipv4Address destination, uint16_t port,
                uint32_t bytes, uint32_t flowId) {
  Ptr<Packet> packet = Create<Packet>(bytes);
  socket->SendTo(packet, 0, InetSocketAddress(destination, port));
  if (g_flowLogger) {
    g_flowLogger->Record(flowId, bytes);
  }
}

struct FlowEndpoints {
  uint32_t src;
  uint32_t dst;
};

static FlowEndpoints GetEndpointsForFlow(uint32_t fid) {
  static std::unordered_map<uint32_t, FlowEndpoints> cache;
  auto it = cache.find(fid);
  if (it != cache.end()) {
    return it->second;
  }
  uint32_t src = fid % TOTAL_HOSTS;
  uint32_t offset = (fid / TOTAL_HOSTS) % (TOTAL_HOSTS - 1) + 1;
  uint32_t dst = (src + offset) % TOTAL_HOSTS;
  FlowEndpoints endpoints{src, dst};
  cache.emplace(fid, endpoints);
  return endpoints;
}

int main(int argc, char *argv[]) {
  std::string inputFile = DEFAULT_INPUT_FILE;
  std::string flowCsv = DEFAULT_FLOW_CSV;
  uint64_t windowNs = 1'000'000; // 1 ms por defecto

  CommandLine cmd(__FILE__);
  cmd.AddValue("input", "Archivo CSV con la traza (fid,bytes,time,...)", inputFile);
  cmd.AddValue("flowCsv", "Archivo CSV de salida con el flow rate", flowCsv);
  cmd.AddValue("windowNs", "Ventana de agregación en nanosegundos", windowNs);
  cmd.Parse(argc, argv);

  std::ifstream traceFile(inputFile);
  if (!traceFile.is_open()) {
    std::cerr << "Error: no se pudo abrir el archivo de entrada: " << inputFile << std::endl;
    return 1;
  }

  g_flowLogger = std::make_unique<FlowRateLogger>(windowNs, flowCsv);

  NodeContainer hosts;
  hosts.Create(TOTAL_HOSTS);

  NodeContainer edgeSwitches;
  edgeSwitches.Create(POD_COUNT * EDGES_PER_POD);

  NodeContainer aggSwitches;
  aggSwitches.Create(POD_COUNT * AGGS_PER_POD);

  NodeContainer coreSwitches;
  coreSwitches.Create(CORE_SWITCHES);

  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(edgeSwitches);
  stack.Install(aggSwitches);
  stack.Install(coreSwitches);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("100Gbps"));
  p2p.SetChannelAttribute("Delay", StringValue("1us"));

  Ipv4AddressHelper addressHelper;
  addressHelper.SetBase("10.0.0.0", "255.255.255.0");

  std::vector<Ipv4Address> hostAddresses(TOTAL_HOSTS);
  std::vector<Ptr<Socket>> hostSockets(TOTAL_HOSTS);

  auto getEdgeIndex = [](uint32_t pod, uint32_t edge) {
    return pod * EDGES_PER_POD + edge;
  };
  auto getAggIndex = [](uint32_t pod, uint32_t agg) {
    return pod * AGGS_PER_POD + agg;
  };
  auto getHostIndex = [](uint32_t pod, uint32_t edge, uint32_t host) {
    return pod * EDGES_PER_POD * HOSTS_PER_EDGE + edge * HOSTS_PER_EDGE + host;
  };

  // Conectar hosts -> edge switches
  for (uint32_t pod = 0; pod < POD_COUNT; ++pod) {
    for (uint32_t edge = 0; edge < EDGES_PER_POD; ++edge) {
      Ptr<Node> edgeNode = edgeSwitches.Get(getEdgeIndex(pod, edge));
      for (uint32_t h = 0; h < HOSTS_PER_EDGE; ++h) {
        uint32_t hostIdx = getHostIndex(pod, edge, h);
        Ptr<Node> hostNode = hosts.Get(hostIdx);
        NetDeviceContainer link = p2p.Install(hostNode, edgeNode);
        Ipv4InterfaceContainer ifaces = addressHelper.Assign(link);
        hostAddresses[hostIdx] = ifaces.GetAddress(0);
        addressHelper.NewNetwork();
      }
    }
  }

  // Conectar edge -> aggregation switches dentro de cada pod
  for (uint32_t pod = 0; pod < POD_COUNT; ++pod) {
    for (uint32_t edge = 0; edge < EDGES_PER_POD; ++edge) {
      Ptr<Node> edgeNode = edgeSwitches.Get(getEdgeIndex(pod, edge));
      for (uint32_t agg = 0; agg < AGGS_PER_POD; ++agg) {
        Ptr<Node> aggNode = aggSwitches.Get(getAggIndex(pod, agg));
        NetDeviceContainer link = p2p.Install(edgeNode, aggNode);
        addressHelper.Assign(link);
        addressHelper.NewNetwork();
      }
    }
  }

  // Conectar aggregation -> core switches
  uint32_t corePerGroup = K_VALUE / 2; // 2
  for (uint32_t agg = 0; agg < AGGS_PER_POD; ++agg) {
    for (uint32_t pod = 0; pod < POD_COUNT; ++pod) {
      Ptr<Node> aggNode = aggSwitches.Get(getAggIndex(pod, agg));
      for (uint32_t core = 0; core < corePerGroup; ++core) {
        uint32_t coreIdx = agg * corePerGroup + core;
        Ptr<Node> coreNode = coreSwitches.Get(coreIdx);
        NetDeviceContainer link = p2p.Install(aggNode, coreNode);
        addressHelper.Assign(link);
        addressHelper.NewNetwork();
      }
    }
  }

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), UDP_PORT));
  for (uint32_t i = 0; i < TOTAL_HOSTS; ++i) {
    sinkHelper.Install(hosts.Get(i));
    Ptr<Socket> socket = Socket::CreateSocket(hosts.Get(i), UdpSocketFactory::GetTypeId());
    socket->Bind();
    hostSockets[i] = socket;
  }

  std::string line;
  uint64_t maxTimeNs = 0;
  uint64_t lineNumber = 0;

  while (std::getline(traceFile, line)) {
    ++lineNumber;
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string fidStr, byteStr, timeStr;
    if (!std::getline(iss, fidStr, ',')) continue;
    if (!std::getline(iss, byteStr, ',')) continue;
    if (!std::getline(iss, timeStr, ',')) continue;

    uint32_t fid = static_cast<uint32_t>(std::stoul(fidStr));
    uint32_t bytes = static_cast<uint32_t>(std::stoul(byteStr));
    uint64_t timeNs = std::stoull(timeStr);

    FlowEndpoints endpoints = GetEndpointsForFlow(fid);
    Ptr<Socket> srcSocket = hostSockets[endpoints.src];
    Ipv4Address dstAddress = hostAddresses[endpoints.dst];

    Simulator::Schedule(NanoSeconds(timeNs), &SendPacket, srcSocket, dstAddress,
                        UDP_PORT, bytes, fid);
    maxTimeNs = std::max(maxTimeNs, timeNs);
  }
  traceFile.close();

  if (maxTimeNs == 0) {
    std::cout << "Advertencia: la traza no contenía eventos válidos." << std::endl;
  }

  Simulator::Stop(NanoSeconds(maxTimeNs + windowNs));
  Simulator::Run();
  Simulator::Destroy();

  if (g_flowLogger) {
    g_flowLogger->WriteCsv();
    g_flowLogger.reset();
  }

  std::cout << "Simulación completada. Resultados en: " << flowCsv << std::endl;
  return 0;
}
