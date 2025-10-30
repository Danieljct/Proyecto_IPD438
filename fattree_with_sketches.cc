/**
 * SIMULACIÓN FAT-TREE CON INTEGRACIÓN DE ALGORITMOS DE SKETCH
 * ===========================================================
 * 
 * Combina:
 * - Topología Fat-Tree básica con ECN
 * - Algoritmos de medición: WaveSketch, Fourier, OmniWindow, PersistCMS
 * - Monitoreo de flujos en tiempo real
 * - Exportación de métricas para análisis offline
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/netanim-module.h"
#include "ns3/traffic-control-module.h"

// Includes para los algoritmos de sketch
#include "Wavelet/wavelet.h"
#include "Fourier/fourier.h"
#include "OmniWindow/omniwindow.h"
#include "PersistCMS/persistCMS.h"

#include <fstream>
#include <map>
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("FatTreeWithSketches");

// ==================== UTILIDADES DE MÉTRICAS ====================

namespace SketchMetrics {
    double CalculateARE(const std::vector<double>& original, const std::vector<double>& reconstructed) {
        double are = 0.0;
        uint32_t nonZeroPoints = 0;
        for (size_t i = 0; i < original.size(); ++i) {
            if (original[i] > 1e-9) {
                are += std::abs(original[i] - reconstructed[i]) / original[i];
                nonZeroPoints++;
            }
        }
        return (nonZeroPoints == 0) ? 0.0 : are / nonZeroPoints;
    }

    double CalculateCosineSimilarity(const std::vector<double>& original, const std::vector<double>& reconstructed) {
        double dotProduct = 0.0, magA = 0.0, magB = 0.0;
        for (size_t i = 0; i < original.size(); ++i) {
            dotProduct += original[i] * reconstructed[i];
            magA += std::pow(original[i], 2);
            magB += std::pow(reconstructed[i], 2);
        }
        magA = std::sqrt(magA);
        magB = std::sqrt(magB);
        return (magA < 1e-9 || magB < 1e-9) ? 1.0 : dotProduct / (magA * magB);
    }
}

// ==================== AGENTE DE MEDICIÓN DE FLUJOS ====================

class FlowMonitorAgent : public Object {
public:
    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("FlowMonitorAgent")
            .SetParent<Object>()
            .SetGroupName("Applications")
            .AddConstructor<FlowMonitorAgent>();
        return tid;
    }

    FlowMonitorAgent() : m_windowUs(1000000), m_memoryKB(256) {}
    
    void Setup(uint32_t memoryKB, uint32_t windowUs, std::string algorithm, std::string outputFile) {
        m_memoryKB = memoryKB;
        m_windowUs = windowUs;
        m_algorithm = algorithm;
        m_outputFilename = outputFile;
        
        m_outputFile.open(m_outputFilename);
        if (m_outputFile.is_open()) {
            m_outputFile << "time_s,algorithm,memory_kb,flow_id,packets,are,cosine_sim\n";
        }
        
        // Inicializar el algoritmo correspondiente
        if (m_algorithm == "wavesketch") {
            m_wavesketch.reset();
        } else if (m_algorithm == "fourier") {
            m_fourier.reset();
        } else if (m_algorithm == "omniwindow") {
            m_omniwindow.reset();
        } else if (m_algorithm == "persistcms") {
            m_persistcms.reset();
        }
        
        NS_LOG_INFO("FlowMonitorAgent configurado: " << m_algorithm 
                    << ", memoria=" << m_memoryKB << "KB, window=" << m_windowUs << "us");
    }

    void OnPacketReceived(Ptr<const Packet> packet, const Address& from) {
        // Extraer información del flujo (simplificado)
        uint64_t flowId = HashAddress(from);
        uint64_t timeNs = Simulator::Now().GetNanoSeconds();
        
        // Registrar en datos ground truth
        uint32_t timeBucket = timeNs / (m_windowUs * 1000); // ns a us
        m_flowData[flowId][timeBucket]++;
        
        // Alimentar algoritmo de sketch
        five_tuple ft = CreateFiveTuple(flowId);
        
        if (m_algorithm == "wavesketch") {
            m_wavesketch.count(ft, timeNs / 1000, 1); // Convertir a microsegundos
        } else if (m_algorithm == "fourier") {
            m_fourier.count(ft, timeNs / 1000, 1);
        } else if (m_algorithm == "omniwindow") {
            m_omniwindow.count(ft, timeNs / 1000, 1);
        } else if (m_algorithm == "persistcms") {
            m_persistcms.count(ft, timeNs / 1000, 1);
        }
    }

    void AnalyzeAndReport() {
        double currentTime = Simulator::Now().GetSeconds();
        
        NS_LOG_INFO("Analizando flujos en t=" << currentTime << "s...");
        
        // Flush de los algoritmos
        if (m_algorithm == "wavesketch") {
            m_wavesketch.flush();
        } else if (m_algorithm == "fourier") {
            m_fourier.flush();
        } else if (m_algorithm == "omniwindow") {
            m_omniwindow.flush();
        } else if (m_algorithm == "persistcms") {
            m_persistcms.flush();
        }
        
        // Analizar cada flujo
        for (auto& flowPair : m_flowData) {
            uint64_t flowId = flowPair.first;
            auto& timeSeries = flowPair.second;
            
            if (timeSeries.empty()) continue;
            
            // Crear vector ground truth
            uint32_t maxBucket = timeSeries.rbegin()->first;
            std::vector<double> original(maxBucket + 1, 0.0);
            for (auto& bucket : timeSeries) {
                original[bucket.first] = bucket.second;
            }
            
            // Reconstruir con el sketch
            std::vector<double> reconstructed = ReconstructFlow(flowId, maxBucket);
            
            // Calcular métricas
            double are = SketchMetrics::CalculateARE(original, reconstructed);
            double cosine = SketchMetrics::CalculateCosineSimilarity(original, reconstructed);
            
            uint32_t totalPackets = 0;
            for (auto& bucket : timeSeries) totalPackets += bucket.second;
            
            // Guardar en CSV
            if (m_outputFile.is_open()) {
                m_outputFile << currentTime << ","
                            << m_algorithm << ","
                            << m_memoryKB << ","
                            << flowId << ","
                            << totalPackets << ","
                            << are << ","
                            << cosine << "\n";
            }
            
            NS_LOG_INFO("  Flow " << flowId << ": packets=" << totalPackets 
                        << ", ARE=" << are << ", cosine=" << cosine);
        }
        
        m_outputFile.flush();
    }

    void Finalize() {
        AnalyzeAndReport();
        if (m_outputFile.is_open()) {
            m_outputFile.close();
        }
        NS_LOG_INFO("FlowMonitorAgent finalizado. Resultados en: " << m_outputFilename);
    }

private:
    uint32_t m_windowUs;
    uint32_t m_memoryKB;
    std::string m_algorithm;
    std::string m_outputFilename;
    std::ofstream m_outputFile;
    
    // Ground truth data
    std::map<uint64_t, std::map<uint32_t, uint32_t>> m_flowData;
    
    // Algoritmos de sketch
    wavelet<false> m_wavesketch;
    fourier m_fourier;
    omniwindow m_omniwindow;
    persistCMS m_persistcms;
    
    uint64_t HashAddress(const Address& from) {
        // Simple hash para identificar flujos
        InetSocketAddress fromAddr = InetSocketAddress::ConvertFrom(from);
        
        uint64_t hash = 0;
        hash ^= fromAddr.GetIpv4().Get();
        hash ^= ((uint64_t)fromAddr.GetPort() << 32);
        
        return hash;
    }
    
    five_tuple CreateFiveTuple(uint64_t flowId) {
        five_tuple ft;
        // Simplificado: usar flowId como identificador único
        ft.src_ip = (uint32_t)(flowId & 0xFFFFFFFF);
        ft.dst_ip = (uint32_t)(flowId >> 32);
        ft.src_port = 0;
        ft.dst_port = 0;
        ft.protocol = 6; // TCP
        return ft;
    }
    
    std::vector<double> ReconstructFlow(uint64_t flowId, uint32_t maxBucket) {
        std::vector<double> reconstructed(maxBucket + 1, 0.0);
        
        five_tuple ft = CreateFiveTuple(flowId);
        
        // Construir STREAM para el rebuild
        auto& timeSeries = m_flowData[flowId];
        STREAM dict;
        STREAM_QUEUE q;
        
        for (uint32_t w = 0; w <= maxBucket; ++w) {
            auto it = timeSeries.find(w);
            if (it != timeSeries.end() && it->second > 0) {
                TIME tt = static_cast<TIME>(w * m_windowUs);
                q.emplace_back(tt, static_cast<DATA>(it->second));
            }
        }
        
        if (!q.empty()) {
            dict[ft] = q;
        }
        
        // Hacer rebuild
        STREAM result;
        if (m_algorithm == "wavesketch") {
            result = m_wavesketch.rebuild(dict);
        } else if (m_algorithm == "fourier") {
            result = m_fourier.rebuild(dict);
        } else if (m_algorithm == "omniwindow") {
            result = m_omniwindow.rebuild(dict);
        } else if (m_algorithm == "persistcms") {
            result = m_persistcms.rebuild(dict);
        }
        
        // Extraer resultados para este flujo
        auto itRec = result.find(ft);
        if (itRec != result.end()) {
            for (auto& point : itRec->second) {
                uint32_t bucket = point.first / m_windowUs;
                if (bucket <= maxBucket) {
                    reconstructed[bucket] += point.second;
                }
            }
        }
        
        return reconstructed;
    }
};

// ==================== VARIABLES GLOBALES ====================

QueueDiscContainer g_queueDiscs;
Ptr<FlowMonitorAgent> g_flowMonitor;

// ==================== CALLBACKS ====================

void PacketReceivedCallback(Ptr<const Packet> packet, const Address& from) {
    if (g_flowMonitor) {
        g_flowMonitor->OnPacketReceived(packet, from);
    }
}

void PrintQueueStats() {
    uint64_t totalMarks = 0, totalDrops = 0;
    
    for (uint32_t i = 0; i < g_queueDiscs.GetN(); ++i) {
        Ptr<RedQueueDisc> red = DynamicCast<RedQueueDisc>(g_queueDiscs.Get(i));
        if (red) {
            QueueDisc::Stats stats = red->GetStats();
            auto it = stats.nMarkedPackets.find("Ecn mark");
            if (it != stats.nMarkedPackets.end()) {
                totalMarks += it->second;
            }
            totalDrops += stats.nTotalDroppedPackets;
        }
    }
    
    std::cout << "[t=" << Simulator::Now().GetSeconds() 
              << "s] ECN marks: " << totalMarks 
              << ", Drops: " << totalDrops << std::endl;
}

// ==================== MAIN ====================

int main(int argc, char *argv[]) {
    // Parámetros configurables
    std::string algorithm = "wavesketch";
    uint32_t memoryKB = 256;
    uint32_t windowUs = 1000000;
    std::string outputFile = "sketch_results.csv";
    double simTime = 10.0;
    
    CommandLine cmd;
    cmd.AddValue("algorithm", "Algoritmo de sketch: wavesketch|fourier|omniwindow|persistcms", algorithm);
    cmd.AddValue("memoryKB", "Memoria del sketch en KB", memoryKB);
    cmd.AddValue("windowUs", "Ventana temporal en microsegundos", windowUs);
    cmd.AddValue("outputFile", "Archivo de salida CSV", outputFile);
    cmd.AddValue("simTime", "Tiempo de simulación en segundos", simTime);
    cmd.Parse(argc, argv);
    
    LogComponentEnable("FatTreeWithSketches", LOG_LEVEL_INFO);
    
    std::cout << "\n=== Simulación Fat-Tree con Algoritmos de Sketch ===" << std::endl;
    std::cout << "Algoritmo: " << algorithm << std::endl;
    std::cout << "Memoria: " << memoryKB << " KB" << std::endl;
    std::cout << "Ventana: " << windowUs << " us" << std::endl;
    std::cout << "Salida: " << outputFile << std::endl;
    std::cout << "Tiempo sim: " << simTime << " s" << std::endl;
    
    // ==================== CREAR NODOS ====================
    
    NodeContainer hosts, switches;
    hosts.Create(4);
    switches.Create(2);
    
    // ==================== CONFIGURAR RED ====================
    
    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));
    p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("10p"));
    
    InternetStackHelper stack;
    stack.InstallAll();
    
    // Configurar RED con ECN
    TrafficControlHelper tchRed;
    tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                           "MinTh", DoubleValue(5),
                           "MaxTh", DoubleValue(15),
                           "MaxSize", QueueSizeValue(QueueSize("30p")),
                           "UseEcn", BooleanValue(true),
                           "QW", DoubleValue(0.002));
    
    // Crear enlaces
    NetDeviceContainer devH0S0 = p2p.Install(hosts.Get(0), switches.Get(0));
    NetDeviceContainer devH1S0 = p2p.Install(hosts.Get(1), switches.Get(0));
    NetDeviceContainer devH2S1 = p2p.Install(hosts.Get(2), switches.Get(1));
    NetDeviceContainer devH3S1 = p2p.Install(hosts.Get(3), switches.Get(1));
    NetDeviceContainer devCore = p2p.Install(switches.Get(0), switches.Get(1));
    
    // Instalar colas RED
    g_queueDiscs.Add(tchRed.Install(devH0S0));
    g_queueDiscs.Add(tchRed.Install(devH1S0));
    g_queueDiscs.Add(tchRed.Install(devH2S1));
    g_queueDiscs.Add(tchRed.Install(devH3S1));
    g_queueDiscs.Add(tchRed.Install(devCore));
    
    // Asignar IPs
    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifH0 = address.Assign(devH0S0);
    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifH1 = address.Assign(devH1S0);
    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer ifH2 = address.Assign(devH2S1);
    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer ifH3 = address.Assign(devH3S1);
    address.SetBase("10.1.5.0", "255.255.255.0");
    address.Assign(devCore);
    
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // ==================== CONFIGURAR MONITOR DE FLUJOS ====================
    
    g_flowMonitor = CreateObject<FlowMonitorAgent>();
    g_flowMonitor->Setup(memoryKB, windowUs, algorithm, outputFile);
    
    // ==================== CONFIGURAR APLICACIONES ====================
    
    // Servidor TCP principal en Host3
    uint16_t port = 5201;
    PacketSinkHelper server("ns3::TcpSocketFactory", 
                           InetSocketAddress(Ipv4Address::GetAny(), port));
    ApplicationContainer serverApps = server.Install(hosts.Get(3));
    serverApps.Start(Seconds(0.5));
    serverApps.Stop(Seconds(simTime + 1.0));
    
    // Cliente TCP: Host0 -> Host3
    BulkSendHelper client("ns3::TcpSocketFactory",
                         InetSocketAddress(ifH3.GetAddress(0), port));
    client.SetAttribute("MaxBytes", UintegerValue(0));
    client.SetAttribute("SendSize", UintegerValue(1460));
    ApplicationContainer clientApps = client.Install(hosts.Get(0));
    clientApps.Start(Seconds(1.0));
    clientApps.Stop(Seconds(simTime));
    
    // Tráfico OnOff adicional: Host1 -> Host2
    uint16_t port2 = 5202;
    PacketSinkHelper server2("ns3::TcpSocketFactory",
                            InetSocketAddress(Ipv4Address::GetAny(), port2));
    ApplicationContainer server2Apps = server2.Install(hosts.Get(2));
    server2Apps.Start(Seconds(0.5));
    
    OnOffHelper onoff("ns3::TcpSocketFactory",
                     InetSocketAddress(ifH2.GetAddress(0), port2));
    onoff.SetConstantRate(DataRate("15Mbps"));
    onoff.SetAttribute("PacketSize", UintegerValue(1460));
    onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"));
    onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
    ApplicationContainer onoffApps = onoff.Install(hosts.Get(1));
    onoffApps.Start(Seconds(2.0));
    onoffApps.Stop(Seconds(simTime));
    
    // ==================== CONECTAR CALLBACKS ====================
    
    // Conectar callback para capturar paquetes recibidos
    Config::ConnectWithoutContext("/NodeList/*/ApplicationList/*/$ns3::PacketSink/Rx",
                                 MakeCallback(&PacketReceivedCallback));
    
    // Monitoreo periódico
    for (double t = 1.0; t <= simTime; t += 1.0) {
        Simulator::Schedule(Seconds(t), &PrintQueueStats);
        Simulator::Schedule(Seconds(t), &FlowMonitorAgent::AnalyzeAndReport, g_flowMonitor);
    }
    
    // ==================== EJECUTAR SIMULACIÓN ====================
    
    std::cout << "\n✓ Configuración completada. Iniciando simulación...\n" << std::endl;
    
    Simulator::Stop(Seconds(simTime + 2.0));
    Simulator::Run();
    
    // Análisis final
    g_flowMonitor->Finalize();
    
    std::cout << "\n=== Simulación completada ===" << std::endl;
    std::cout << "Resultados guardados en: " << outputFile << std::endl;
    
    Simulator::Destroy();
    return 0;
}
