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

add_executable(fattree_benchmark fattree_benchmark.cc)

# Include utility C sources required by some algorithm implementations (pffft)
# so that linking works when those headers are used. The file was added to
# Utility/pffft.c by the user.
target_sources(fattree_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/Utility/pffft.c)

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

# Enlaza el ejecutable de benchmark
target_link_libraries(fattree_benchmark
  ns3.45-core-default
  ns3.45-network-default
  ns3.45-internet-default
  ns3.45-applications-default
  ns3.45-point-to-point-default
  ns3.45-traffic-control-default
  ns3.45-flow-monitor-default
)

# Flags de compilación
target_compile_options(fattree PRIVATE ${NS3_CFLAGS_OTHER})
target_compile_options(fattree_ecn_clean PRIVATE ${NS3_CFLAGS_OTHER})
target_compile_options(fattree_benchmark PRIVATE ${NS3_CFLAGS_OTHER})
target_link_directories(fattree PRIVATE ${NS3_LIBRARY_DIRS})

# Offline evaluator: builds the offline evaluator binary that runs the
# authors' algorithms over a CSV trace (websearch25.csv). We must compile
# pffft.c into this target so Fourier symbols link correctly.
add_executable(offline_evaluator offline_evaluator.cpp)
target_sources(offline_evaluator PRIVATE ${CMAKE_SOURCE_DIR}/Utility/pffft.c)

# Ensure the same compile flags as other targets (if NS-3 flags are present)
if(TARGET fattree)
  target_compile_options(offline_evaluator PRIVATE ${NS3_CFLAGS_OTHER})
endif()