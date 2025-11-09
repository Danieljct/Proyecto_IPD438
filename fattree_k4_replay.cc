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
#include "ns3/traffic-control-module.h"
#include "ns3/queue-disc.h"
#include "ns3/queue-size.h"
#include "ns3/queue.h"
#include "ns3/red-queue-disc.h"
#include "ns3/random-variable-stream.h"

#include "wavesketch/Wavelet/wavelet.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <map>
#include <limits>
#include <cmath>

using namespace ns3;

namespace {
constexpr uint32_t K_VALUE = 4;
constexpr uint32_t HOSTS_PER_EDGE = K_VALUE / 2;         // 2
constexpr uint32_t EDGES_PER_POD = K_VALUE / 2;          // 2
constexpr uint32_t AGGS_PER_POD = K_VALUE / 2;           // 2
constexpr uint32_t POD_COUNT = K_VALUE;                  // 4
constexpr uint32_t CORE_SWITCHES = (K_VALUE / 2) * (K_VALUE / 2); // 4
constexpr uint32_t TOTAL_HOSTS = POD_COUNT * EDGES_PER_POD * HOSTS_PER_EDGE; // 16
 constexpr uint64_t RED_MIN_TH_BYTES = 20u * 1024u;
 constexpr uint64_t RED_MAX_TH_BYTES = 200u * 1024u;

const uint16_t UDP_PORT = 9000;
const std::string DEFAULT_INPUT_FILE = "hadoop15.csv";
const std::string DEFAULT_FLOW_CSV = "flow_rate.csv";
} // namespace

class CongestionEventTracker {
public:
  using GroundTruthMap = std::map<uint64_t, uint32_t>;

  CongestionEventTracker(uint64_t windowNs, uint64_t thresholdBytes, std::string filename)
      : m_windowNs(windowNs), m_thresholdBytes(thresholdBytes), m_filename(std::move(filename)) {}

  void Record(uint32_t bytes) {
    if (m_windowNs == 0) {
      return;
    }
    uint64_t windowIndex = Simulator::Now().GetNanoSeconds() / m_windowNs;
    uint32_t &stat = m_groundTruth[windowIndex];
    if (bytes > stat) {
      stat = bytes;
    }
  }

  const GroundTruthMap &GroundTruth() const {
    return m_groundTruth;
  }

  uint64_t ThresholdBytes() const {
    return m_thresholdBytes;
  }

  void WriteCsv() const {
    if (m_filename.empty()) {
      return;
    }
    std::ofstream ofs(m_filename);
    if (!ofs.is_open()) {
      std::cerr << "Error: no se pudo abrir " << m_filename << " para escritura" << std::endl;
      return;
    }
    ofs << "time_s,max_queue_bytes\n";
    for (const auto &entry : m_groundTruth) {
      double timeSeconds = static_cast<double>(entry.first * m_windowNs) / 1e9;
      ofs << timeSeconds << ',' << entry.second << '\n';
    }
  }

private:
  uint64_t m_windowNs;
  uint64_t m_thresholdBytes;
  std::string m_filename;
  GroundTruthMap m_groundTruth;
};
class FlowRateLogger {
public:
  FlowRateLogger(uint64_t windowNs, std::string filename, double samplingRatio,
                 Ptr<UniformRandomVariable> samplingRv)
      : m_windowNs(windowNs),
        m_filename(std::move(filename)),
        m_samplingRatio(samplingRatio),
        m_samplingRv(std::move(samplingRv)) {}

  struct RecallMetrics {
    uint32_t totalCongestionWindows = 0;
    uint32_t capturedWindows = 0;
    double recall = 0.0;
  };

  void Record(uint32_t /*flowId*/, uint32_t bytes) {
    uint64_t nowNs = Simulator::Now().GetNanoSeconds();
    uint64_t windowIndex = nowNs / m_windowNs;
    auto &entry = m_windows[windowIndex];
    if (m_samplingRatio <= 0.0) {
      return;
    }
    uint64_t contribution = bytes;
    if (m_samplingRatio < 1.0) {
      if (!m_samplingRv) {
        return;
      }
      if (m_samplingRv->GetValue() > m_samplingRatio) {
        return;
      }
      double scaled = static_cast<double>(bytes) / m_samplingRatio;
      contribution = static_cast<uint64_t>(std::llround(scaled));
    }
    entry.bytes += contribution;
  }

  void RecordEcnMark() {
    uint64_t nowNs = Simulator::Now().GetNanoSeconds();
    uint64_t windowIndex = nowNs / m_windowNs;
    auto &entry = m_windows[windowIndex];
    entry.ecnMarks += 1;
  }

  void WriteCsv() const {
    std::ofstream ofs(m_filename);
    if (!ofs.is_open()) {
      std::cerr << "Error: no se pudo abrir " << m_filename << " para escritura" << std::endl;
      return;
    }
    struct SampleRow {
      uint64_t windowIndex;
      uint64_t bytes;
      int32_t scaledValue;
      uint32_t ecnMarks;
    };

    std::vector<SampleRow> samples;
    samples.reserve(m_windows.size());
    for (const auto &bucket : m_windows) {
      const WindowStats &stats = bucket.second;
      uint64_t rounded = (stats.bytes + kWaveletScale / 2) / kWaveletScale;
      uint64_t clamped = std::min<uint64_t>(rounded, static_cast<uint64_t>(std::numeric_limits<int32_t>::max()));
      if (rounded > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        std::cerr << "Advertencia: Se truncó el valor escalado de la ventana " << bucket.first << " para ajustarlo al rango int32." << std::endl;
      }
      samples.push_back(SampleRow{bucket.first, stats.bytes, static_cast<int32_t>(clamped), stats.ecnMarks});
    }

    ofs << "time_s,total_rate_gbps,reconstructed_rate_gbps,ecn_marks\n";
    if (samples.empty()) {
      return;
    }

    five_tuple syntheticFlow(0);
    wavelet<false> waveletScheme;
    waveletScheme.reset();

    STREAM dict;
    STREAM_QUEUE &queue = dict[syntheticFlow];

    std::vector<int32_t> reconstructedValues(samples.size(), 0);
    for (size_t i = 0; i < samples.size(); ++i) {
      const uint32_t tick = static_cast<uint32_t>(samples[i].windowIndex + 1);
      queue.emplace_back(tick, samples[i].scaledValue);
      waveletScheme.count(syntheticFlow, tick, samples[i].scaledValue);
      reconstructedValues[i] = samples[i].scaledValue;
    }

    waveletScheme.flush();

    STREAM rebuilt = waveletScheme.rebuild(dict);
    auto reconstructedIt = rebuilt.find(syntheticFlow);
    if (reconstructedIt != rebuilt.end()) {
      for (const auto &entry : reconstructedIt->second) {
        if (entry.first == 0) {
          continue;
        }
        size_t index = static_cast<size_t>(entry.first - 1);
        if (index < reconstructedValues.size()) {
          reconstructedValues[index] = entry.second;
        }
      }
    }


    for (size_t i = 0; i < samples.size(); ++i) {
      const SampleRow &row = samples[i];
      double timeSeconds = static_cast<double>(row.windowIndex * m_windowNs) / 1e9;
      double originalRate = static_cast<double>(row.bytes) * 8.0 / static_cast<double>(m_windowNs);
      double reconstructedBytes = std::max(0.0, static_cast<double>(reconstructedValues[i]) * static_cast<double>(kWaveletScale));
      double reconstructedRate = reconstructedBytes * 8.0 / static_cast<double>(m_windowNs);
      ofs << timeSeconds << ',' << originalRate << ',' << reconstructedRate << ',' << row.ecnMarks << '\n';
    }
  }

  RecallMetrics ComputeRecall(const CongestionEventTracker::GroundTruthMap &groundTruth,
                              uint64_t thresholdBytes) const {
    RecallMetrics metrics;
    for (const auto &entry : groundTruth) {
      if (entry.second <= thresholdBytes) {
        continue;
      }
      metrics.totalCongestionWindows += 1;
      auto it = m_windows.find(entry.first);
      if (it != m_windows.end() && it->second.ecnMarks > 0) {
        metrics.capturedWindows += 1;
      }
    }
    if (metrics.totalCongestionWindows > 0) {
      metrics.recall = static_cast<double>(metrics.capturedWindows) /
                       static_cast<double>(metrics.totalCongestionWindows);
    }
    return metrics;
  }

private:
  struct WindowStats {
    uint64_t bytes = 0;
    uint32_t ecnMarks = 0;
  };

  static constexpr uint32_t kWaveletScale = 1000;

  uint64_t m_windowNs;
  std::string m_filename;
  std::map<uint64_t, WindowStats> m_windows;
  double m_samplingRatio = 1.0;
  Ptr<UniformRandomVariable> m_samplingRv;
};

static std::unique_ptr<FlowRateLogger> g_flowLogger;
static std::unique_ptr<CongestionEventTracker> g_congestionTracker;
static Ptr<UniformRandomVariable> g_samplingRv;
static double g_samplingRatio = 1.0;

void OnBytesInQueue(uint32_t oldValue, uint32_t newValue);

void OnQueueDiscMark(Ptr<const QueueDiscItem> /*item*/, const char * /*reason*/) {
  if (!g_flowLogger) {
    return;
  }
  if (g_samplingRatio <= 0.0) {
    return;
  }
  if (g_samplingRatio < 1.0) {
    if (!g_samplingRv) {
      return;
    }
    double sample = g_samplingRv->GetValue();
    if (sample > g_samplingRatio) {
      return;
    }
  }
  g_flowLogger->RecordEcnMark();
}

void AttachEcnTracer(const QueueDiscContainer &container) {
  for (uint32_t i = 0; i < container.GetN(); ++i) {
    Ptr<QueueDisc> qd = container.Get(i);
    if (qd) {
      qd->TraceConnectWithoutContext("Mark", MakeCallback(&OnQueueDiscMark));
      qd->TraceConnectWithoutContext("BytesInQueue", MakeCallback(&OnBytesInQueue));
    }
  }
}

void OnBytesInQueue(uint32_t /*oldValue*/, uint32_t newValue) {
  if (g_congestionTracker) {
    g_congestionTracker->Record(newValue);
  }
}

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
  double samplingRatio = 1.0;
  std::string queueCsv = "queue_ground_truth.csv";

  CommandLine cmd(__FILE__);
  cmd.AddValue("input", "Archivo CSV con la traza (fid,bytes,time,...)", inputFile);
  cmd.AddValue("flowCsv", "Archivo CSV de salida con el flow rate", flowCsv);
  cmd.AddValue("windowNs", "Ventana de agregación en nanosegundos", windowNs);
  cmd.AddValue("samplingRatio", "Probabilidad de registrar un evento ECN (0-1]", samplingRatio);
  cmd.AddValue("queueCsv", "Archivo CSV para registrar la congestión (ground truth)", queueCsv);
  cmd.Parse(argc, argv);

  std::ifstream traceFile(inputFile);
  if (!traceFile.is_open()) {
    std::cerr << "Error: no se pudo abrir el archivo de entrada: " << inputFile << std::endl;
    return 1;
  }

  samplingRatio = std::clamp(samplingRatio, 0.0, 1.0);
  g_samplingRatio = samplingRatio;
  g_samplingRv = CreateObject<UniformRandomVariable>();
  g_samplingRv->SetAttribute("Min", DoubleValue(0.0));
  g_samplingRv->SetAttribute("Max", DoubleValue(1.0));

  g_congestionTracker = std::make_unique<CongestionEventTracker>(windowNs, RED_MAX_TH_BYTES, queueCsv);
  g_flowLogger = std::make_unique<FlowRateLogger>(windowNs, flowCsv, g_samplingRatio, g_samplingRv);

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

  TrafficControlHelper tch;
  tch.SetRootQueueDisc("ns3::RedQueueDisc",
                       "MinTh", DoubleValue(static_cast<double>(RED_MIN_TH_BYTES)),
                       "MaxTh", DoubleValue(static_cast<double>(RED_MAX_TH_BYTES)),
                       "MaxSize", QueueSizeValue(QueueSize("400kB")),
                       "LinkBandwidth", StringValue("100Gbps"),
                       "LinkDelay", StringValue("1us"),
                       "UseEcn", BooleanValue(true),
                       "Gentle", BooleanValue(true));

  Ipv4AddressHelper addressHelper;
  addressHelper.SetBase("10.0.0.0", "255.255.255.0");

  std::vector<Ipv4Address> hostAddresses(TOTAL_HOSTS);
  std::vector<Ptr<Socket>> hostSockets(TOTAL_HOSTS);

  auto installLink = [&](Ptr<Node> a, Ptr<Node> b) {
    NetDeviceContainer devices = p2p.Install(a, b);
    QueueDiscContainer qdiscs = tch.Install(devices);
    AttachEcnTracer(qdiscs);
    return devices;
  };

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
        NetDeviceContainer link = installLink(hostNode, edgeNode);
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
        NetDeviceContainer link = installLink(edgeNode, aggNode);
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
        NetDeviceContainer link = installLink(aggNode, coreNode);
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
    socket->SetIpTos(0x02); // ECT(0) para permitir marcado ECN
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

  if (g_congestionTracker) {
    g_congestionTracker->WriteCsv();
  }

  if (g_flowLogger) {
    FlowRateLogger::RecallMetrics metrics;
    if (g_congestionTracker) {
      metrics = g_flowLogger->ComputeRecall(g_congestionTracker->GroundTruth(),
                                            g_congestionTracker->ThresholdBytes());
    }
    g_flowLogger->WriteCsv();
    if (metrics.totalCongestionWindows > 0) {
      std::cout << "µEvent recall: " << metrics.recall << " ("
                << metrics.capturedWindows << '/' << metrics.totalCongestionWindows
                << ")" << std::endl;
    } else {
      std::cout << "µEvent recall: n/a (sin eventos de congestión)" << std::endl;
    }
    g_flowLogger.reset();
  }

  std::cout << "Simulación completada. Resultados en: " << flowCsv << std::endl;
  return 0;
}
