/**
 * SIMULACIÓN LIMPIA PARA MEDICIÓN DE ECN + WAVESKETCH INTEGRADO
 * =============================================================
 * * Implementa un agente de monitoreo de tráfico (WaveSketchAgent) 
 * que captura contadores de flujo en microsegundos, aplica la 
 * transformada Haar DWT y selecciona coeficientes Top-K.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h" // provides Ipv4FlowClassifier via flow-monitor
#include <iomanip>
#include <map>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EcnMeasurementAndWaveSketch");

// --- CONFIGURACIÓN DE WAVESKETCH ---
const uint64_t WAVE_WINDOW_US = 50;  // Granularidad de medición: 50 µs
const uint32_t TOTAL_MONITOR_TIME_MS = 1; // Duración de la curva de flujo a comprimir (1ms)
const uint32_t MAX_K_COEFFICIENTS = 4; // Parámetro K para la selección Top-K
const uint32_t NUM_WINDOWS_PER_CURVE = (TOTAL_MONITOR_TIME_MS * 1000) / WAVE_WINDOW_US; // 20 ventanas de 50µs en 1ms


// Estructuras y utilidades de WaveSketch (Extraídas de lógica C++)
// -------------------------------------------------------------------

// Estructura para almacenar la tupla (Índice, Valor) de un coeficiente
struct Coeff {
    uint32_t index;
    double value;
    double magnitude; // Valor absoluto
    bool operator<(const Coeff& other) const {
        return magnitude > other.magnitude; // Ordenar de mayor a menor magnitud
    }
};

/**
 * \brief Clase para implementar el agente WaveSketch de medición y compresión.
 * * Simula el proceso de medición in-dataplane y la compresión por Wavelets.
 */
class WaveSketchAgent : public Object {
public:
    static TypeId GetTypeId(void) {
        static TypeId tid = TypeId("ns3::WaveSketchAgent").SetParent<Object>().SetGroupName("Tutorial").AddConstructor<WaveSketchAgent>();
        return tid;
    }
    // Almacena los contadores de paquetes: FlowId -> [Count_w0, Count_w1, ...]
    // Utilizamos uint64_t como FlowId para unificar la clave de flujo.
    using FlowRateBuffer = std::map<uint64_t, std::vector<uint32_t>>;
    FlowRateBuffer m_flowRates;
    
    WaveSketchAgent() {
        NS_LOG_INFO("WaveSketchAgent inicializado. Ventana: " << WAVE_WINDOW_US << "us, K=" << MAX_K_COEFFICIENTS);
    }

    /**
     * \brief Implementa la Transformada Wavelet Discreta de Haar (DWT).
     * * @param inputVector El vector de tasas de paquetes (contadores).
     * @return Vector de coeficientes Wavelet (Approximation + Details).
     */
    std::vector<double> HaarTransform(std::vector<double> inputVector) {
        // La DWT solo funciona con vectores de tamaño potencia de 2.
        uint32_t N = inputVector.size();
        if (N == 0 || (N & (N - 1)) != 0) {
            // Padding simple con ceros si el tamaño no es potencia de 2
            uint32_t nextPowerOf2 = 1;
            while (nextPowerOf2 < N) {
                nextPowerOf2 <<= 1;
            }
            inputVector.resize(nextPowerOf2, 0.0);
            N = nextPowerOf2;
        }

        std::vector<double> currentVector = inputVector;
        std::vector<double> tempVector(N);
        uint32_t currentSize = N;
        const double invSqrt2 = 1.0 / std::sqrt(2.0);

        while (currentSize > 1) {
            uint32_t nextSize = currentSize / 2;
            for (uint32_t j = 0; j < nextSize; ++j) {
                double a = currentVector[2 * j];
                double b = currentVector[2 * j + 1];

                // Coeficientes de Aproximación (Average)
                tempVector[j] = (a + b) * invSqrt2;

                // Coeficientes de Detalle (Difference)
                tempVector[j + nextSize] = (a - b) * invSqrt2;
            }
            
            // Copiar los coeficientes calculados de vuelta al vector actual
            std::copy(tempVector.begin(), tempVector.begin() + currentSize, currentVector.begin());
            currentSize = nextSize;
        }

        return currentVector;
    }

    /**
     * \brief Selecciona los coeficientes Top-K con mayor magnitud.
     * * @param coefficients Vector completo de coeficientes Wavelet.
     * @return Vector de estructuras Coeff con los K mayores.
     */
    std::vector<Coeff> SelectTopK(const std::vector<double>& coefficients) {
        std::vector<Coeff> allCoeffs;
        for (uint32_t i = 0; i < coefficients.size(); ++i) {
            allCoeffs.push_back({i, coefficients[i], std::abs(coefficients[i])});
        }

        // Ordenar por magnitud descendente
        std::sort(allCoeffs.begin(), allCoeffs.end());

        // Devolver solo los Top-K
        uint32_t k = std::min((uint32_t)allCoeffs.size(), MAX_K_COEFFICIENTS);
        return std::vector<Coeff>(allCoeffs.begin(), allCoeffs.begin() + k);
    }
    
    /**
     * \brief Callback que se conecta al Trace de transmisión de paquetes (Tx).
     * * Cuenta el paquete en la ventana de tiempo actual del flujo.
     * @param p Puntero al paquete.
     */
    void OnPacketSent(Ptr<const Packet> p) {
        // Extraer encabezados IP y TCP/UDP del paquete para crear un id de flujo.
        Ptr<Packet> copy = p->Copy();
        Ipv4Header ipv4;
        if (!copy->PeekHeader(ipv4)) {
            // No es IPv4 o no se puede parsear
            return;
        }

        uint8_t proto = ipv4.GetProtocol();
        uint16_t sport = 0, dport = 0;

        if (proto == 6) { // TCP
            TcpHeader tcp;
            if (copy->PeekHeader(tcp)) {
                sport = tcp.GetSourcePort();
                dport = tcp.GetDestinationPort();
            }
        } else if (proto == 17) { // UDP
            UdpHeader udp;
            if (copy->PeekHeader(udp)) {
                sport = udp.GetSourcePort();
                dport = udp.GetDestinationPort();
            }
        }

        // Construir un identificador simple a partir de 5-tupla
        uint64_t src = ipv4.GetSource().Get();
        uint64_t dst = ipv4.GetDestination().Get();
        uint64_t hashId = (src << 32) ^ (dst << 8) ^ (uint64_t(proto) << 0) ^ (uint64_t(sport) << 16) ^ dport;

        // Determinar la ventana de tiempo de 50 us
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint32_t windowIndex = currentTimeUs / WAVE_WINDOW_US;

        // Incrementar el contador de paquetes para esa ventana y flujo
        std::vector<uint32_t>& rateBuffer = m_flowRates[hashId];
        if (windowIndex >= rateBuffer.size()) {
            rateBuffer.resize(windowIndex + 1, 0);
        }
        rateBuffer[windowIndex]++;
    }

    /**
     * \brief Toma las curvas de flujo de 1ms completas, las comprime y reporta.
     * * Se programa para ejecutarse cada TOTAL_MONITOR_TIME_MS.
     */
    void CompressAndAnalyze() {
        uint64_t currentTimeUs = Simulator::Now().GetMicroSeconds();
        uint32_t currentCurveEndWindow = (currentTimeUs / WAVE_WINDOW_US);
        uint32_t targetWindowIndex = (currentCurveEndWindow / NUM_WINDOWS_PER_CURVE) * NUM_WINDOWS_PER_CURVE;
        
        uint32_t startWindow = targetWindowIndex - NUM_WINDOWS_PER_CURVE;
        
        // Si no tenemos suficientes datos para una curva completa, esperar.
        if (targetWindowIndex < NUM_WINDOWS_PER_CURVE) {
            // Reprogramar y salir si aún no hay datos suficientes
            Simulator::Schedule(MilliSeconds(TOTAL_MONITOR_TIME_MS), &WaveSketchAgent::CompressAndAnalyze, this);
            return;
        }

        std::cout << "\n=== WAVESKETCH: ANÁLISIS PERIÓDICO ===" << std::endl;
        std::cout << "Tiempo de Simulación: " << Simulator::Now().GetSeconds() << "s" << std::endl;
        std::cout << "Ventanas analizadas: " << startWindow << " a " << targetWindowIndex - 1 << std::endl;
        
        uint32_t totalFlowsCompressed = 0;

        for (auto it = m_flowRates.begin(); it != m_flowRates.end(); ) {
            uint64_t flowId = it->first;
            std::vector<uint32_t>& fullBuffer = it->second;

            // 1. Verificar si la curva de flujo tiene la longitud suficiente
            if (fullBuffer.size() >= targetWindowIndex) {
                // Extraer el segmento de la curva (e.g., las últimas 20 ventanas)
                std::vector<uint32_t> subBuffer(fullBuffer.begin() + startWindow, fullBuffer.begin() + targetWindowIndex);
                
                // Convertir a double para la transformada
                std::vector<double> inputWave(subBuffer.begin(), subBuffer.end());

                // 2. Aplicar WaveSketch (Haar DWT + Top-K)
                std::vector<double> coeffs = HaarTransform(inputWave);
                std::vector<Coeff> topK = SelectTopK(coeffs);
                
                // 3. Reporte
                std::cout << "  Flow Hash " << flowId << ": Paquetes en curva=" 
                          << std::accumulate(subBuffer.begin(), subBuffer.end(), 0)
                          << ", Coeficientes Top-" << MAX_K_COEFFICIENTS << " seleccionados:" << std::endl;
                          
                for (const auto& c : topK) {
                    std::cout << "    [i=" << c.index << ", val=" << std::fixed << std::setprecision(3) << c.value << "]" << std::endl;
                }
                
                // 4. Limpieza del buffer (Simular envío y borrado)
                // Se eliminan los datos de las ventanas procesadas
                fullBuffer.erase(fullBuffer.begin(), fullBuffer.begin() + targetWindowIndex);
                if (fullBuffer.empty()) {
                    it = m_flowRates.erase(it); // Eliminar el flujo si el buffer está vacío
                } else {
                    ++it;
                }
                totalFlowsCompressed++;
            } else {
                ++it;
            }
        }
        
        std::cout << "✅ Flows comprimidos en " << Simulator::Now().GetSeconds() << "s: " << totalFlowsCompressed << std::endl;

        // Reprogramar la función para la siguiente iteración
        Simulator::Schedule(MilliSeconds(TOTAL_MONITOR_TIME_MS), &WaveSketchAgent::CompressAndAnalyze, this);
    }
};

// --- Variables globales para monitoreo ECN (Mantenidas del original) ---
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
  
  std::vector<std::string> colaDescripcion = {
    "Host0->Switch0 (TCP client)",     // Cola 0
    "Switch0->Host0",                  // Cola 1  
    "Host1->Switch0 (UDP flood)",      // Cola 2
    "Switch0->Host1",                  // Cola 3
    "Host2->Switch1",                  // Cola 4
    "Switch1->Host2",                  // Cola 5
    "Host3->Switch1 (TCP+UDP dest)",   // Cola 6
    "Switch1->Host3",                  // Cola 7
    "Switch0->Switch1 (Core link)",    // Cola 8
    "Switch1->Switch0"                 // Cola 9
  };
  
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
      
      std::cout << "Cola " << i << " (" << colaDescripcion[i] << "):" << std::endl;
      std::cout << "  ECN=" << ecnMarks 
                << ", Drops=" << stats.nTotalDroppedPackets 
                << ", Enqueues=" << stats.nTotalEnqueuedPackets << std::endl;
      
      // Análisis especial para colas problemáticas
      if (stats.nTotalDroppedPackets > 1000 && ecnMarks == 0) {
        std::cout << "  🔍 ANÁLISIS: Muchos drops pero 0 ECN - posible tráfico UDP" << std::endl;
      }
      if (ecnMarks > 0) {
        std::cout << "  ✅ ECN activo - tráfico TCP detectando congestión" << std::endl;
      }
      
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
  
  std::cout << "\n💡 EXPLICACIÓN:" << std::endl;
  std::cout << "- WaveSketch ahora está midiendo los flujos de tráfico en microsegundos." << std::endl;
}


// FUNCIÓN PARA OBTENER EL CLASIFICADOR DE FLUJOS (CRÍTICO PARA ns-3)
// No se utiliza el Ipv4FlowClassifier en esta versión; se extraen headers directamente del paquete.


int main(int argc, char *argv[])
{
  // =====================================================
  // 1. CONFIGURACIÓN INICIAL
  // =====================================================
  
  Time::SetResolution(Time::NS);
  LogComponentEnable("EcnMeasurementAndWaveSketch", LOG_LEVEL_INFO);
  //LogComponentEnable("WaveSketchAgent", LOG_LEVEL_INFO);
  
  std::cout << "🔬 SIMULACIÓN ECN + WAVESKETCH" << std::endl;
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
  hosts.Create(4);  // 4 hosts
  
  NodeContainer switches;
  switches.Create(2);  // 2 switches
  
  // Instalar stack TCP/IP
  InternetStackHelper stack;
  stack.Install(hosts);
  stack.Install(switches);

  // =====================================================
  // 4. CONFIGURACIÓN DE ENLACES y 5. RED
  // =====================================================
  
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
  p2p.SetChannelAttribute("Delay", StringValue("5ms"));

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

  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // =====================================================
  // 8. CONFIGURACIÓN DE APLICACIONES (Original)
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
  // 9. INTEGRACIÓN DE WAVESKETCH
  // =====================================================
  
    Ptr<WaveSketchAgent> wsAgent = Create<WaveSketchAgent>();

    // Conectar el agente a los traces de paquetes salientes (Tx) en los hosts
  // Monitoreamos Host 0 (TCP) y Host 1 (UDP)
  
  // Host 0 -> Switch 0: Interfaz 0
  devHost0ToSw0.Get(0)->TraceConnectWithoutContext(
      "Tx", MakeCallback(&WaveSketchAgent::OnPacketSent, wsAgent));
      
  // Host 1 -> Switch 0: Interfaz 0
  devHost1ToSw0.Get(0)->TraceConnectWithoutContext(
      "Tx", MakeCallback(&WaveSketchAgent::OnPacketSent, wsAgent));

  std::cout << "✅ WaveSketch conectado a Host 0 (TCP) y Host 1 (UDP)." << std::endl;
  std::cout << "   Capturando paquetes salientes en ventanas de " << WAVE_WINDOW_US << "us." << std::endl;

  // =====================================================
  // 10. PROGRAMAR MEDICIONES ECN Y WAVESKETCH
  // =====================================================
  
  // Medir ECN cada 2 segundos (original)
  for (double t = 2.0; t <= 10.0; t += 2.0) {
    Simulator::Schedule(Seconds(t), &MeasureEcnStatistics);
  }
  
  // Programar la compresión WaveSketch (cada 1ms)
  Simulator::Schedule(MilliSeconds(TOTAL_MONITOR_TIME_MS), &WaveSketchAgent::CompressAndAnalyze, wsAgent);

  // =====================================================
  // 11. EJECUTAR SIMULACIÓN
  // =====================================================
  
  std::cout << "\n🚀 Iniciando simulación..." << std::endl;
  std::cout << "⏱️  Duración: 12 segundos" << std::endl;

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
