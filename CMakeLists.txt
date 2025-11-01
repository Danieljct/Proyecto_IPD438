cmake_minimum_required(VERSION 3.10)
project(MisExperimentosNs3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Ajusta esta ruta a donde tienes compilado ns-3
set(NS3_DIR "/home/daniel/PaperRedes/ns-3-allinone/ns-3.45/build")

# Incluye headers de NS-3
include_directories(${NS3_DIR}/include)

# Busca librerías de NS-3
link_directories(${NS3_DIR}/lib)

# Tu ejemplo original
add_executable(fattree fattree.cc)

# Versión limpia para medición ECN
add_executable(fattree_ecn_clean fattree_ecn_clean.cc)

# Topología Fat-Tree k=4 que reproduce la traza hadoop15.csv
add_executable(fattree_k4_replay fattree_k4_replay.cc)


# Enlaza con NS-3 (todas las libs principales)
target_link_libraries(fattree
  ns3.45-core-default
  ns3.45-network-default
  ns3.45-internet-default
  ns3.45-applications-default
  ns3.45-csma-default
  ns3.45-point-to-point-default
  ns3.45-netanim-default
  ns3.45-traffic-control-default
)

# Enlaza también el ejecutable limpio
target_link_libraries(fattree_ecn_clean
  ns3.45-core-default
  ns3.45-network-default
  ns3.45-internet-default
  ns3.45-applications-default
  ns3.45-point-to-point-default
  ns3.45-traffic-control-default
  ns3.45-flow-monitor-default
)

target_link_libraries(fattree_k4_replay
  ns3.45-core-default
  ns3.45-network-default
  ns3.45-internet-default
  ns3.45-applications-default
  ns3.45-point-to-point-default
  ns3.45-traffic-control-default
)


# Flags de compilación
target_compile_options(fattree PRIVATE ${NS3_CFLAGS_OTHER})
target_compile_options(fattree_ecn_clean PRIVATE ${NS3_CFLAGS_OTHER})
target_compile_options(fattree_k4_replay PRIVATE ${NS3_CFLAGS_OTHER})
target_link_directories(fattree PRIVATE ${NS3_LIBRARY_DIRS})
target_link_directories(fattree_k4_replay PRIVATE ${NS3_LIBRARY_DIRS})
