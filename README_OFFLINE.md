Offline evaluator (websearch25.csv)

This small README explains how to build and run the offline evaluator that
processes the paper dataset (websearch25.csv) and produces a CSV compatible
with the project's visualization scripts.

Prerequisites
- CMake (3.10+)
- A C++ toolchain that supports C++20 (g++/clang++ with -std=gnu++20)
- Python (for the comparison/plots script) with pandas/matplotlib if you want
  to run the comparison script included in scripts/.

Build
1. From the project root (where this README and CMakeLists.txt live):

   mkdir -p build && cd build
   cmake ..
   make offline_evaluator

This will build `offline_evaluator` and compile `Utility/pffft.c` into the
binary so Fourier-related symbols resolve correctly.

Run
From the project root (or the `build/` directory) run the binary. It expects
`uMon-WaveSketch/data/websearch25.csv` by default (path can be adjusted in the
source if needed):

  ./offline_evaluator

Output
- `benchmark_results_offline.csv` — produced by the evaluator. Header:
  time_s,algorithm,memory_kb,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist,energy_sim

Comparison helper
- `scripts/compare_reports.py` is a utility that compares
  `benchmark_results_offline.csv` with the authors' `report.csv` (found at
  `uMon-WaveSketch/cpp_version/build/report.csv`). It computes aggregated
  statistics per algorithm/class and lists the top flow discrepancies. See
  that script for usage details.

Notes
- The project CMake already sets C++20. The offline target links pffft.c so
  the Fourier algorithm can run without extra manual linking.
- If you changed the dataset location, update `offline_evaluator.cpp` or run
  the binary with the appropriate working directory.
