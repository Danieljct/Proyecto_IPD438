/**
 * SIMULACIÓN COMPLETA PARA MEDICIÓN DE µFLOW (WAVESKETCH) EN NS-3
 * =============================================================
 * Autor: Gemini (basado en el código del usuario y su documentación)
 *
 * VERSIÓN GENERADORA DE DATOS:
 * - Añade argumentos de línea de comandos (CommandLine) para K, windowUs y outputFile.
 * - Escribe los resultados de precisión (ARE, Coseno, etc.) en un archivo CSV
 * para su posterior análisis y visualización.
 * - Elimina la salida ruidosa de std::cout en favor de la escritura en archivo.
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
#include <fstream> // <-- Para escribir en archivos

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("WaveSketchDataGenerator");

// --- ESTRUCTURAS Y UTILIDADES DE WAVESKETCH (sin cambios) ---
const uint32_t CURVE_DURATION_MS = 1;

struct Coeff {
    uint32_t index;
    double value;
    double magnitude;
    bool operator<(const Coeff& other) const {
        return magnitude > other.magnitude; // Ordenar de mayor a menor magnitud
    }
};

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
}

class WaveSketchAgent : public Object {
public:
    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("ns3::WaveSketchAgent").SetParent<Object>().SetGroupName("Tutorial").AddConstructor<WaveSketchAgent>();
        return tid;
    }

    // Almacena: FlowId -> Ventana -> Contador
    using FlowTimeSeries = std::map<uint32_t, uint32_t>;
    std::map<uint64_t, FlowTimeSeries> m_flowData;
    uint64_t m_lastProcessedTimeUs = 0;

    // Parámetros de la simulación
    uint32_t m_k;
    uint32_t m_windowUs;
    uint32_t m_numWindowsPerCurve;
    
    // Archivo de salida
    std::ofstream m_outputFile;
    std::string m_outputFilename;

    WaveSketchAgent() {}

    // Función para configurar el agente desde main()
    void Setup(uint32_t k, uint32_t windowUs, std::string outputFile) {
        m_k = k;
        m_windowUs = windowUs;
        m_numWindowsPerCurve = (CURVE_DURATION_MS * 1000) / m_windowUs;
        m_outputFilename = outputFile;
        
        // Abrir el archivo de salida en modo de añadir (append)
        m_outputFile.open(m_outputFilename, std::ios_base::app);
        NS_LOG_INFO("Agente configurado: K=" << m_k << ", Window=" << m_windowUs << "us, File=" << m_outputFilename);
    }

    ~WaveSketchAgent() {
        if (m_outputFile.is_open()) {
            m_outputFile.close();
        }
    }

    // --- Funciones de la Transformada (con la corrección matemática) ---
    std::vector<double> HaarTransform(std::vector<double>& inputVector) {
        uint32_t N = inputVector.size();
        if (N == 0) return {};
        if ((N & (N - 1)) != 0) {
            uint32_t nextPowerOf2 = 1;
            while (nextPowerOf2 < N) nextPowerOf2 <<= 1;
            inputVector.resize(nextPowerOf2, 0.0);
            N = nextPowerOf2;
        }
        std::vector<double> currentVector = inputVector;
        std::vector<double> tempVector(N);
        uint32_t currentSize = N;
        const double INV_SQRT2 = 1.0 / std::sqrt(2.0);
        while (currentSize > 1) {
            uint32_t nextSize = currentSize / 2;
            for (uint32_t j = 0; j < nextSize; ++j) {
                double a = currentVector[2 * j];
                double b = currentVector[2 * j + 1];
                tempVector[j] = (a + b) * INV_SQRT2;
                tempVector[j + nextSize] = (a - b) * INV_SQRT2;
            }
            std::copy(tempVector.begin(), tempVector.begin() + currentSize, currentVector.begin());
            currentSize = nextSize;
        }
        return currentVector;
    }

    std::vector<double> InverseHaarTransform(const std::vector<double>& coefficients, uint32_t originalSize) {
        uint32_t N = coefficients.size();
        if (N == 0) return {};
        std::vector<double> currentVector = coefficients;
        std::vector<double> tempVector(N);
        uint32_t currentSize = 1;
        const double SQRT2 = std::sqrt(2.0);
        while (currentSize < N) {
            uint32_t nextSize = currentSize * 2;
            for (uint32_t j = 0; j < currentSize; ++j) {
                double approx = currentVector[j];
                double detail = currentVector[j + currentSize];
                tempVector[2 * j]     = (approx + detail) * SQRT2 / 2.0;
                tempVector[2 * j + 1] = (approx - detail) * SQRT2 / 2.0;
            }
            std::copy(tempVector.begin(), tempVector.begin() + nextSize, currentVector.begin());
            currentSize = nextSize;
        }
        currentVector.resize(originalSize);
        return currentVector;
    }

    std::vector<Coeff> SelectTopK(const std::vector<double>& coefficients) {
        std::vector<Coeff> allCoeffs;
        for (uint32_t i = 0; i < coefficients.size(); ++i) {
            allCoeffs.push_back({i, coefficients[i], std::abs(coefficients[i])});
        }
        std::sort(allCoeffs.begin(), allCoeffs.end());
        uint32_t k = std::min((uint32_t)allCoeffs.size(), m_k); // Usar m_k
        return std::vector<Coeff>(allCoeffs.begin(), allCoeffs.begin() + k);
    }
    
    void OnPacketSent(uint64_t flowId, Ptr<const Packet> /*p*/) {
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint32_t windowIndex = currentTimeUs / m_windowUs; // Usar m_windowUs
        m_flowData[flowId][windowIndex]++;
    }

    void CompressAndAnalyze() {
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

        // Esta salida en consola es útil para saber que la simulación está viva
        std::cout << "Analizando... Tiempo Sim: " << Simulator::Now().GetSeconds() << "s" << std::endl;
        
        for (auto const& [flowId, timeSeries] : m_flowData) {
            std::vector<double> originalCurve(numWindows, 0.0);
            bool hasActivity = false;
            
            for(uint32_t w = startWindow; w < endWindow; ++w) {
                auto it = timeSeries.find(w);
                if (it != timeSeries.end()) {
                    originalCurve[w - startWindow] = it->second;
                    hasActivity = true;
                }
            }

            if (!hasActivity) continue;
            
            double totalPackets = std::accumulate(originalCurve.begin(), originalCurve.end(), 0.0);
            std::vector<double> transformInput = originalCurve;
            std::vector<double> coeffs = HaarTransform(transformInput);
            std::vector<Coeff> topK = SelectTopK(coeffs);
            std::vector<double> compressedCoeffs(coeffs.size(), 0.0);
            for (const auto& c : topK) {
                compressedCoeffs[c.index] = c.value;
            }

            std::vector<double> reconstructedCurve = InverseHaarTransform(compressedCoeffs, numWindows);
            
            double eucDist = WaveSketchMetrics::CalculateEuclideanDistance(originalCurve, reconstructedCurve);
            double are = WaveSketchMetrics::CalculateARE(originalCurve, reconstructedCurve);
            double cosSim = WaveSketchMetrics::CalculateCosineSimilarity(originalCurve, reconstructedCurve);

            // Escribir los resultados en el archivo CSV
            if (m_outputFile.is_open()) {
                m_outputFile << Simulator::Now().GetSeconds() << ","
                             << flowId << ","
                             << m_k << ","
                             << m_windowUs << ","
                             << totalPackets << ","
                             << are << ","
                             << cosSim << ","
                             << eucDist << "\n";
            }
        }
        
        m_lastProcessedTimeUs = analysisBoundaryUs;
        Simulator::Schedule(MilliSeconds(CURVE_DURATION_MS), &WaveSketchAgent::CompressAndAnalyze, this);
    }
};

int main(int argc, char *argv[])
{
    // --- Configuración de Parámetros desde Línea de Comandos ---
    uint32_t k = 4;
    uint32_t windowUs = 50;
    std::string outputFile = "results.csv";

    CommandLine cmd(__FILE__);
    cmd.AddValue("k", "Número de coeficientes Top-K a retener", k);
    cmd.AddValue("windowUs", "Tamaño de la ventana de medición en microsegundos", windowUs);
    cmd.AddValue("outputFile", "Nombre del archivo CSV de salida", outputFile);
    cmd.Parse(argc, argv);

    // --- Limpiar archivo de salida y escribir encabezado ---
    // (Se abre en modo 'trunc' para borrar contenido anterior)
    std::ofstream headerWriter(outputFile, std::ios_base::trunc);
    if (headerWriter.is_open()) {
        headerWriter << "time_s,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist\n";
        headerWriter.close();
    } else {
        std::cerr << "Error: No se pudo abrir el archivo de salida: " << outputFile << std::endl;
        return 1;
    }

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

    // --- INTEGRACIÓN DE WAVESKETCH ---
    Ptr<WaveSketchAgent> wsAgent = CreateObject<WaveSketchAgent>();
    // Configurar el agente con los parámetros de la línea de comandos
    wsAgent->Setup(k, windowUs, outputFile);

    tcpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&WaveSketchAgent::OnPacketSent, wsAgent).Bind((uint64_t)1));
    udpClientApp.Get(0)->TraceConnectWithoutContext("Tx", MakeCallback(&WaveSketchAgent::OnPacketSent, wsAgent).Bind((uint64_t)2));

    // --- PROGRAMAR SIMULACIÓN Y ANÁLISIS ---
    Simulator::Schedule(Seconds(trafficStartTime) + MilliSeconds(CURVE_DURATION_MS), 
                        &WaveSketchAgent::CompressAndAnalyze, wsAgent);

    Simulator::Stop(Seconds(10.0));
    Simulator::Run();
    Simulator::Destroy();

    return 0;
}
