/**
 * SIMULACIÓN COMPLETA PARA BENCHMARK DE ALGORITMOS DE MEDICIÓN (µFlow)
 * ===================================================================
 * Autor: Gemini (basado en el código del usuario y su documentación)
 *
 * VERSIÓN CORREGIDA (ERRORES DE COMPILACIÓN):
 * - Corrige errores de plantilla para wavelet (`wavelet::wavelet<false>`).
 * - Corrige nombres de clases (ej. `fourier::fourier`).
 * - Corrige la inicialización de objetos de algoritmos en los constructores Agent
 * (usa constructor por defecto + reasignación en Setup).
 * - Añade destructores virtuales `override`.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"

#include <iomanip>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream> // Para escribir en archivos

// --- INCLUDES PARA LOS ALGORITMOS DEL AUTOR ---
#include "Wavelet/wavelet.h"
#include "Fourier/fourier.h"
#include "OmniWindow/omniwindow.h"
#include "PersistCMS/persistCMS.h"


using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WaveSketchBenchmarkHarness");

// --- CONFIGURACIÓN GLOBAL ---
const uint32_t CURVE_DURATION_MS = 1;

// --- ESTRUCTURAS Y UTILIDADES (Métricas) ---
namespace WaveSketchMetrics {
    double CalculateEuclideanDistance(const std::vector<double>& original, const std::vector<double>& reconstructed) {
        double distance = 0.0;
        for (size_t i = 0; i < original.size(); ++i) {
            distance += std::pow(original[i] - reconstructed[i], 2);
        }
        return std::sqrt(distance);
    }
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
        double dotProduct = 0.0;
        double magA = 0.0;
        double magB = 0.0;
        for (size_t i = 0; i < original.size(); ++i) {
            dotProduct += original[i] * reconstructed[i];
            magA += std::pow(original[i], 2);
            magB += std::pow(reconstructed[i], 2);
        }
        magA = std::sqrt(magA);
        magB = std::sqrt(magB);
        if (magA < 1e-9 || magB < 1e-9) return 1.0;
        return dotProduct / (magA * magB);
    }
    double CalculateEnergySimilarity(const std::vector<double>& original, const std::vector<double>& reconstructed) {
        double energyOriginal = 0.0;
        double energyReconstructed = 0.0;
        for (size_t i = 0; i < original.size(); ++i) {
            energyOriginal += std::pow(original[i], 2);
            energyReconstructed += std::pow(reconstructed[i], 2);
        }
        if (energyOriginal < 1e-9) {
            return (energyReconstructed < 1e-9) ? 1.0 : 0.0;
        }
        double ratio = energyReconstructed / energyOriginal;
        return (ratio > 1.0) ? 1.0 / ratio : ratio;
    }
}

// --- INTERFAZ ABSTRACTA BÁSICA ---
class MeasurementAgent : public Object {
public:
    // <-- DESTRUCTOR VIRTUAL EN BASE -->
    virtual ~MeasurementAgent() override = default;
    virtual void Setup(uint32_t memoryKB, uint32_t windowUs, std::string outputFile) = 0;
    virtual void OnPacketSent(uint64_t flowId, Ptr<Packet> p) = 0;
    virtual void CompressAndAnalyze() = 0;
protected:
    using FlowTimeSeries = std::map<uint32_t, uint32_t>;
    std::map<uint64_t, FlowTimeSeries> m_flowData;
    uint64_t m_lastProcessedTimeUs = 0;
    uint32_t m_memoryKB;
    uint32_t m_k; // K ahora se calcula en Setup
    uint32_t m_windowUs;
    uint32_t m_numWindowsPerCurve;
    std::ofstream m_outputFile;
    std::string m_outputFilename;
    std::string m_algorithmName;

    void WriteToCsv(double time_s, uint64_t flowId, double totalPackets, double are, double cosSim, double eucDist, double energySim) {
        if (m_outputFile.is_open()) {
            m_outputFile << time_s << ","
                         << m_algorithmName << ","
                         << m_memoryKB << ","
                         << flowId << ","
                         << m_k << "," // <-- Reportar el K calculado
                         << m_windowUs << ","
                         << totalPackets << ","
                         << are << ","
                         << cosSim << ","
                         << eucDist << ","
                         << energySim << "\n";
        }
    }
};

// --- ALGORITMO 1: WAVESKETCH AGENT (Ideal) ---
class WaveSketchAgent : public MeasurementAgent {
public:
    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("ns3::WaveSketchAgent").SetParent<MeasurementAgent>().SetGroupName("Tutorial").AddConstructor<WaveSketchAgent>();
        return tid;
    }

    // <-- CORRECCIÓN: Especificar template -->
    wavelet::wavelet<false> m_wavelet_algo;

    // <-- CORRECCIÓN: Usar constructor por defecto -->
    WaveSketchAgent() {
        m_algorithmName = "wavesketch-ideal";
    }

    virtual void Setup(uint32_t memoryKB, uint32_t windowUs, std::string outputFile) override {
        m_memoryKB = memoryKB;
        uint32_t memoryBytes = m_memoryKB * 1024;
        m_windowUs = windowUs;
        m_numWindowsPerCurve = (CURVE_DURATION_MS * 1000) / m_windowUs;
        m_outputFilename = outputFile;
        m_k = memoryBytes / 12; // Asumir 12 bytes por coeficiente

        m_outputFile.open(m_outputFilename, std::ios_base::app);

        // <-- CORRECCIÓN: Reasignar con parámetros correctos -->
        m_wavelet_algo = wavelet::wavelet<false>(m_k, memoryBytes, m_numWindowsPerCurve);

        NS_LOG_INFO(m_algorithmName << " configurado: Memoria=" << m_memoryKB << "KB -> K=" << m_k);
    }

    // <-- DESTRUCTOR VIRTUAL OVERRIDE -->
    ~WaveSketchAgent() override {
        if (m_outputFile.is_open()) m_outputFile.close();
    }

    virtual void OnPacketSent(uint64_t flowId, Ptr<Packet> /*p*/) override {
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint32_t windowIndex = currentTimeUs / m_windowUs;
        m_flowData[flowId][windowIndex]++;
        m_wavelet_algo.run(flowId, windowIndex, 1);
    }

    virtual void CompressAndAnalyze() override {
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint64_t analysisBoundaryUs = (currentTimeUs / (CURVE_DURATION_MS * 1000)) * (CURVE_DURATION_MS * 1000);

        if (analysisBoundaryUs <= m_lastProcessedTimeUs) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &WaveSketchAgent::CompressAndAnalyze, this);
             return;
        }

        uint32_t startWindow = m_lastProcessedTimeUs / m_windowUs;
        uint32_t endWindow = analysisBoundaryUs / m_windowUs;
        uint32_t numWindows = endWindow - startWindow;

        if (numWindows == 0) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &WaveSketchAgent::CompressAndAnalyze, this);
             return;
        }

        for (auto const& [flowId, timeSeries] : m_flowData) {
            std::vector<double> originalCurve(numWindows, 0.0);
            std::vector<double> reconstructedCurve(numWindows, 0.0);
            bool hasActivity = false;

            for(uint32_t w = 0; w < numWindows; ++w) {
                uint32_t currentWindowIndex = startWindow + w;
                auto it = timeSeries.find(currentWindowIndex);
                if (it != timeSeries.end()) {
                    originalCurve[w] = it->second;
                    hasActivity = true;
                }
                reconstructedCurve[w] = m_wavelet_algo.query(flowId, currentWindowIndex);
            }

            if (!hasActivity) continue;

            double totalPackets = std::accumulate(originalCurve.begin(), originalCurve.end(), 0.0);
            double eucDist = WaveSketchMetrics::CalculateEuclideanDistance(originalCurve, reconstructedCurve);
            double are = WaveSketchMetrics::CalculateARE(originalCurve, reconstructedCurve);
            double cosSim = WaveSketchMetrics::CalculateCosineSimilarity(originalCurve, reconstructedCurve);
            double energySim = WaveSketchMetrics::CalculateEnergySimilarity(originalCurve, reconstructedCurve);

            WriteToCsv(Simulator::Now().GetSeconds(), flowId, totalPackets, are, cosSim, eucDist, energySim);
        }

        m_lastProcessedTimeUs = analysisBoundaryUs;
        Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &WaveSketchAgent::CompressAndAnalyze, this);
    }
};

// --- ALGORITMO 2: FOURIER AGENT ---
class FourierAgent : public MeasurementAgent {
public:
    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("ns3::FourierAgent").SetParent<MeasurementAgent>().SetGroupName("Tutorial").AddConstructor<FourierAgent>();
        return tid;
    }

    // <-- CORRECCIÓN: Nombre de clase minúscula -->
    fourier::fourier m_fourier_algo;

    // <-- CORRECCIÓN: Constructor por defecto -->
    FourierAgent() {
        m_algorithmName = "fourier";
    }

    virtual void Setup(uint32_t memoryKB, uint32_t windowUs, std::string outputFile) override {
        m_memoryKB = memoryKB;
        uint32_t memoryBytes = m_memoryKB * 1024;
        m_windowUs = windowUs;
        m_numWindowsPerCurve = (CURVE_DURATION_MS * 1000) / m_windowUs;
        m_outputFilename = outputFile;
        m_k = memoryBytes / 12; // Asumir mismo costo

        m_outputFile.open(m_outputFilename, std::ios_base::app);

        // <-- CORRECCIÓN: Reasignar -->
        m_fourier_algo = fourier::fourier(m_k, memoryBytes, m_numWindowsPerCurve);

        NS_LOG_INFO(m_algorithmName << " configurado: Memoria=" << m_memoryKB << "KB -> K=" << m_k);
    }

    // <-- DESTRUCTOR VIRTUAL OVERRIDE -->
    ~FourierAgent() override {
        if (m_outputFile.is_open()) m_outputFile.close();
    }

    virtual void OnPacketSent(uint64_t flowId, Ptr<Packet> /*p*/) override {
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint32_t windowIndex = currentTimeUs / m_windowUs;
        m_flowData[flowId][windowIndex]++;
        m_fourier_algo.run(flowId, windowIndex, 1);
    }

    virtual void CompressAndAnalyze() override {
        // ... (IDÉNTICO a WaveSketchAgent, solo cambian las llamadas al algo) ...
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint64_t analysisBoundaryUs = (currentTimeUs / (CURVE_DURATION_MS * 1000)) * (CURVE_DURATION_MS * 1000);
        if (analysisBoundaryUs <= m_lastProcessedTimeUs) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &FourierAgent::CompressAndAnalyze, this);
             return;
        }
        uint32_t startWindow = m_lastProcessedTimeUs / m_windowUs;
        uint32_t endWindow = analysisBoundaryUs / m_windowUs;
        uint32_t numWindows = endWindow - startWindow;
        if (numWindows == 0) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &FourierAgent::CompressAndAnalyze, this);
             return;
        }

        for (auto const& [flowId, timeSeries] : m_flowData) {
            std::vector<double> originalCurve(numWindows, 0.0);
            std::vector<double> reconstructedCurve(numWindows, 0.0);
            bool hasActivity = false;

            for(uint32_t w = 0; w < numWindows; ++w) {
                uint32_t currentWindowIndex = startWindow + w;
                auto it = timeSeries.find(currentWindowIndex);
                if (it != timeSeries.end()) {
                    originalCurve[w] = it->second;
                    hasActivity = true;
                }
                reconstructedCurve[w] = m_fourier_algo.query(flowId, currentWindowIndex);
            }

            if (!hasActivity) continue;

            double totalPackets = std::accumulate(originalCurve.begin(), originalCurve.end(), 0.0);
            double eucDist = WaveSketchMetrics::CalculateEuclideanDistance(originalCurve, reconstructedCurve);
            double are = WaveSketchMetrics::CalculateARE(originalCurve, reconstructedCurve);
            double cosSim = WaveSketchMetrics::CalculateCosineSimilarity(originalCurve, reconstructedCurve);
            double energySim = WaveSketchMetrics::CalculateEnergySimilarity(originalCurve, reconstructedCurve);

            WriteToCsv(Simulator::Now().GetSeconds(), flowId, totalPackets, are, cosSim, eucDist, energySim);
        }
        m_lastProcessedTimeUs = analysisBoundaryUs;
        Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &FourierAgent::CompressAndAnalyze, this);
    }
};

// --- ALGORITMO 3: OMNIWINDOW AGENT ---
class OmniWindowAgent : public MeasurementAgent {
public:
    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("ns3::OmniWindowAgent").SetParent<MeasurementAgent>().SetGroupName("Tutorial").AddConstructor<OmniWindowAgent>();
        return tid;
    }

    omniwindow::OmniWindow m_omni_algo;

    OmniWindowAgent() { // <-- Usa constructor por defecto si existe
        m_algorithmName = "omniwindow";
    }

    virtual void Setup(uint32_t memoryKB, uint32_t windowUs, std::string outputFile) override {
        m_memoryKB = memoryKB;
        uint32_t memoryBytes = m_memoryKB * 1024;
        m_windowUs = windowUs;
        m_numWindowsPerCurve = (CURVE_DURATION_MS * 1000) / m_windowUs;
        m_outputFilename = outputFile;
        m_k = memoryBytes / 12; // Asumir mismo costo

        m_outputFile.open(m_outputFilename, std::ios_base::app);

        // <-- CORRECCIÓN: Reasignar -->
        // Nota: Asegúrate de que el constructor de OmniWindow coincida
        m_omni_algo = omniwindow::OmniWindow(m_k, memoryBytes, m_numWindowsPerCurve);

        NS_LOG_INFO(m_algorithmName << " configurado: Memoria=" << m_memoryKB << "KB -> K=" << m_k);
    }

    // <-- DESTRUCTOR VIRTUAL OVERRIDE -->
    ~OmniWindowAgent() override {
        if (m_outputFile.is_open()) m_outputFile.close();
    }

    virtual void OnPacketSent(uint64_t flowId, Ptr<Packet> /*p*/) override {
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint32_t windowIndex = currentTimeUs / m_windowUs;
        m_flowData[flowId][windowIndex]++;
        m_omni_algo.run(flowId, windowIndex, 1);
    }

    virtual void CompressAndAnalyze() override {
        // ... (IDÉNTICO a FourierAgent, solo cambian las llamadas al algo) ...
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint64_t analysisBoundaryUs = (currentTimeUs / (CURVE_DURATION_MS * 1000)) * (CURVE_DURATION_MS * 1000);
        if (analysisBoundaryUs <= m_lastProcessedTimeUs) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &OmniWindowAgent::CompressAndAnalyze, this);
             return;
        }
        uint32_t startWindow = m_lastProcessedTimeUs / m_windowUs;
        uint32_t endWindow = analysisBoundaryUs / m_windowUs;
        uint32_t numWindows = endWindow - startWindow;
        if (numWindows == 0) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &OmniWindowAgent::CompressAndAnalyze, this);
             return;
        }

        for (auto const& [flowId, timeSeries] : m_flowData) {
            std::vector<double> originalCurve(numWindows, 0.0);
            std::vector<double> reconstructedCurve(numWindows, 0.0);
            bool hasActivity = false;

            for(uint32_t w = 0; w < numWindows; ++w) {
                uint32_t currentWindowIndex = startWindow + w;
                auto it = timeSeries.find(currentWindowIndex);
                if (it != timeSeries.end()) {
                    originalCurve[w] = it->second;
                    hasActivity = true;
                }
                reconstructedCurve[w] = m_omni_algo.query(flowId, currentWindowIndex);
            }

            if (!hasActivity) continue;

            double totalPackets = std::accumulate(originalCurve.begin(), originalCurve.end(), 0.0);
            double eucDist = WaveSketchMetrics::CalculateEuclideanDistance(originalCurve, reconstructedCurve);
            double are = WaveSketchMetrics::CalculateARE(originalCurve, reconstructedCurve);
            double cosSim = WaveSketchMetrics::CalculateCosineSimilarity(originalCurve, reconstructedCurve);
            double energySim = WaveSketchMetrics::CalculateEnergySimilarity(originalCurve, reconstructedCurve);

            WriteToCsv(Simulator::Now().GetSeconds(), flowId, totalPackets, are, cosSim, eucDist, energySim);
        }
        m_lastProcessedTimeUs = analysisBoundaryUs;
        Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &OmniWindowAgent::CompressAndAnalyze, this);
    }
};

// --- ALGORITMO 4: PERSISTCMS AGENT ---
class PersistCMSAgent : public MeasurementAgent {
public:
    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("ns3::PersistCMSAgent").SetParent<MeasurementAgent>().SetGroupName("Tutorial").AddConstructor<PersistCMSAgent>();
        return tid;
    }

    // <-- CORRECCIÓN: Nombre de clase correcto -->
    persistcms::PersistCMS m_cms_algo;

    // <-- CORRECCIÓN: Constructor por defecto -->
    PersistCMSAgent() {
        m_algorithmName = "persistcms";
    }

    virtual void Setup(uint32_t memoryKB, uint32_t windowUs, std::string outputFile) override {
        m_memoryKB = memoryKB;
        uint32_t memoryBytes = m_memoryKB * 1024;
        m_windowUs = windowUs;
        m_numWindowsPerCurve = (CURVE_DURATION_MS * 1000) / m_windowUs;
        m_outputFilename = outputFile;
        m_k = memoryBytes / 12;

        m_outputFile.open(m_outputFilename, std::ios_base::app);

        // <-- CORRECCIÓN: Reasignar -->
        m_cms_algo = persistcms::PersistCMS(m_k, memoryBytes, m_numWindowsPerCurve);

        NS_LOG_INFO(m_algorithmName << " configurado: Memoria=" << m_memoryKB << "KB -> K=" << m_k);
    }

    // <-- DESTRUCTOR VIRTUAL OVERRIDE -->
    ~PersistCMSAgent() override {
        if (m_outputFile.is_open()) m_outputFile.close();
    }

    virtual void OnPacketSent(uint64_t flowId, Ptr<Packet> /*p*/) override {
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint32_t windowIndex = currentTimeUs / m_windowUs;
        m_flowData[flowId][windowIndex]++;
        m_cms_algo.run(flowId, windowIndex, 1);
    }

    virtual void CompressAndAnalyze() override {
        // ... (IDÉNTICO a FourierAgent, solo cambian las llamadas al algo) ...
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint64_t analysisBoundaryUs = (currentTimeUs / (CURVE_DURATION_MS * 1000)) * (CURVE_DURATION_MS * 1000);
        if (analysisBoundaryUs <= m_lastProcessedTimeUs) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &PersistCMSAgent::CompressAndAnalyze, this);
             return;
        }
        uint32_t startWindow = m_lastProcessedTimeUs / m_windowUs;
        uint32_t endWindow = analysisBoundaryUs / m_windowUs;
        uint32_t numWindows = endWindow - startWindow;
        if (numWindows == 0) {
             Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &PersistCMSAgent::CompressAndAnalyze, this);
             return;
        }

        for (auto const& [flowId, timeSeries] : m_flowData) {
            std::vector<double> originalCurve(numWindows, 0.0);
            std::vector<double> reconstructedCurve(numWindows, 0.0);
            bool hasActivity = false;

            for(uint32_t w = 0; w < numWindows; ++w) {
                uint32_t currentWindowIndex = startWindow + w;
                auto it = timeSeries.find(currentWindowIndex);
                if (it != timeSeries.end()) {
                    originalCurve[w] = it->second;
                    hasActivity = true;
                }
                reconstructedCurve[w] = m_cms_algo.query(flowId, currentWindowIndex);
            }

            if (!hasActivity) continue;

            double totalPackets = std::accumulate(originalCurve.begin(), originalCurve.end(), 0.0);
            double eucDist = WaveSketchMetrics::CalculateEuclideanDistance(originalCurve, reconstructedCurve);
            double are = WaveSketchMetrics::CalculateARE(originalCurve, reconstructedCurve);
            double cosSim = WaveSketchMetrics::CalculateCosineSimilarity(originalCurve, reconstructedCurve);
            double energySim = WaveSketchMetrics::CalculateEnergySimilarity(originalCurve, reconstructedCurve);

            WriteToCsv(Simulator::Now().GetSeconds(), flowId, totalPackets, are, cosSim, eucDist, energySim);
        }
        m_lastProcessedTimeUs = analysisBoundaryUs;
        Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &PersistCMSAgent::CompressAndAnalyze, this);
    }
};


// --- FUNCIÓN PRINCIPAL (main) ---
int main(int argc, char *argv[])
{
    // --- Configuración de Parámetros desde Línea de Comandos ---
    std::string algorithm = "wavesketch-ideal";
    uint32_t memoryKB = 128;
    uint32_t windowUs = 50;
    std::string outputFile = "benchmark_results.csv";

    CommandLine cmd(__FILE__);
    cmd.AddValue("algorithm", "Algoritmo a probar (wavesketch-ideal, fourier, omniwindow, persistcms)", algorithm);
    cmd.AddValue("memoryKB", "Presupuesto de memoria en KB", memoryKB);
    cmd.AddValue("windowUs", "Tamaño de la ventana de medición en microsegundos", windowUs);
    cmd.AddValue("outputFile", "Nombre del archivo CSV de salida (se añadirá info)", outputFile);
    cmd.Parse(argc, argv);

    // Escribir encabezado si el archivo no existe
    std::ofstream headerWriter(outputFile, std::ios_base::app);
    if (headerWriter.tellp() == 0) {
        headerWriter << "time_s,algorithm,memory_kb,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist,energy_sim\n";
    }
    headerWriter.close();

    Time::SetResolution(Time::NS);

    // --- Configuración de Red y Topología (sin cambios) ---
    Config::SetDefault("ns3::TcpSocketBase::UseEcn", StringValue("On"));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));

    NodeContainer hosts; hosts.Create(4);
    NodeContainer switches; switches.Create(2);

    InternetStackHelper stack;
    stack.Install(hosts);
    stack.Install(switches);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    TrafficControlHelper tchRed;
    tchRed.SetRootQueueDisc("ns3::RedQueueDisc",
                          "MinTh", DoubleValue(5), "MaxTh", DoubleValue(15),
                          "MaxSize", QueueSizeValue(QueueSize("25p")),
                          "UseEcn", BooleanValue(true), "Gentle", BooleanValue(true));

    NetDeviceContainer d_h0_s0 = p2p.Install(hosts.Get(0), switches.Get(0));
    NetDeviceContainer d_h1_s0 = p2p.Install(hosts.Get(1), switches.Get(0));
    NetDeviceContainer d_h2_s1 = p2p.Install(hosts.Get(2), switches.Get(1));
    NetDeviceContainer d_h3_s1 = p2p.Install(hosts.Get(3), switches.Get(1));
    NetDeviceContainer d_core  = p2p.Install(switches.Get(0), switches.Get(1));
    tchRed.Install(d_h0_s0); tchRed.Install(d_h1_s0); tchRed.Install(d_h2_s1);
    tchRed.Install(d_h3_s1); tchRed.Install(d_core);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0"); address.Assign(d_h0_s0);
    address.SetBase("10.1.2.0", "255.255.255.0"); address.Assign(d_h1_s0);
    address.SetBase("10.1.3.0", "255.255.255.0"); address.Assign(d_h2_s1);
    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer i_h3_s1 = address.Assign(d_h3_s1);
    address.SetBase("10.1.5.0", "255.255.255.0"); address.Assign(d_core);
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // --- APLICACIONES ---
    double trafficStartTime = 1.0;
    double trafficStopTime = 9.0;

    uint16_t tcpPort = 5001;
    PacketSinkHelper tcpSink("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), tcpPort));
    ApplicationContainer serverApps = tcpSink.Install(hosts.Get(3));
    serverApps.Start(Seconds(trafficStartTime - 0.5));
    serverApps.Stop(Seconds(trafficStopTime + 1.0));

    BulkSendHelper tcpClientHelper("ns3::TcpSocketFactory", InetSocketAddress(i_h3_s1.GetAddress(1), tcpPort));
    tcpClientHelper.SetAttribute("MaxBytes", UintegerValue(0));
    ApplicationContainer tcpClientApp = tcpClientHelper.Install(hosts.Get(0));
    tcpClientApp.Start(Seconds(trafficStartTime));
    tcpClientApp.Stop(Seconds(trafficStopTime));

    OnOffHelper udpClientHelper("ns3::UdpSocketFactory", InetSocketAddress(i_h3_s1.GetAddress(1), 9999));
    udpClientHelper.SetConstantRate(DataRate("30Mbps"));
    ApplicationContainer udpClientApp = udpClientHelper.Install(hosts.Get(1));
    udpClientApp.Start(Seconds(trafficStartTime + 0.5));
    udpClientApp.Stop(Seconds(trafficStopTime - 0.5));

    // --- INTEGRACIÓN Y SELECCIÓN DE ALGORITMO ---

    // Puntero genérico al agente activo
    Ptr<MeasurementAgent> activeAgent;

    if (algorithm == "wavesketch-ideal") {
        Ptr<WaveSketchAgent> wsAgent = CreateObject<WaveSketchAgent>();
        wsAgent->Setup(memoryKB, windowUs, outputFile);
        tcpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&WaveSketchAgent::OnPacketSent, wsAgent).Bind((uint64_t)1));
        udpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&WaveSketchAgent::OnPacketSent, wsAgent).Bind((uint64_t)2));
        activeAgent = wsAgent; // Guardar puntero genérico
    }
    else if (algorithm == "fourier") {
        Ptr<FourierAgent> fAgent = CreateObject<FourierAgent>();
        fAgent->Setup(memoryKB, windowUs, outputFile);
        tcpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&FourierAgent::OnPacketSent, fAgent).Bind((uint64_t)1));
        udpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&FourierAgent::OnPacketSent, fAgent).Bind((uint64_t)2));
        activeAgent = fAgent;
    }
    else if (algorithm == "omniwindow") {
        Ptr<OmniWindowAgent> oAgent = CreateObject<OmniWindowAgent>();
        oAgent->Setup(memoryKB, windowUs, outputFile);
        tcpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&OmniWindowAgent::OnPacketSent, oAgent).Bind((uint64_t)1));
        udpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&OmniWindowAgent::OnPacketSent, oAgent).Bind((uint64_t)2));
        activeAgent = oAgent;
    }
    else if (algorithm == "persistcms") {
        Ptr<PersistCMSAgent> cAgent = CreateObject<PersistCMSAgent>();
        cAgent->Setup(memoryKB, windowUs, outputFile);
        tcpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&PersistCMSAgent::OnPacketSent, cAgent).Bind((uint64_t)1));
        udpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&PersistCMSAgent::OnPacketSent, cAgent).Bind((uint64_t)2));
        activeAgent = cAgent;
    }
    // <-- Puedes añadir un "else if (algorithm == "wavesketch-hw")" aquí -->
    else {
        NS_LOG_ERROR("Algoritmo desconocido: " << algorithm);
        return 1;
    }

    // Programar la primera llamada a CompressAndAnalyze usando el puntero genérico
    Simulator::Schedule(Seconds(trafficStartTime) + MilliSeconds(CURVE_DURATION_MS),
                        &MeasurementAgent::CompressAndAnalyze, activeAgent);


    std::cout << "Iniciando simulación: Algoritmo=" << algorithm << ", Memoria=" << memoryKB << "KB" << std::endl;

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    std::cout << "Simulación completada. Resultados guardados en " << outputFile << std::endl;
    return 0;
}

