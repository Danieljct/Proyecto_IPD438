#!/usr/bin/env bash
# Run an offline memory sweep on the small sample using per-packet replay.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SAMPLE="$ROOT_DIR/data_sample.csv"
OUT="benchmark_results_offline_sample_sweep.csv"
# default sweep (KB)
MEMS="8,16,32,64,128,256"

if [ "$#" -ge 1 ]; then
  MEMS="$1"
fi

echo "Building offline_evaluator..."
g++ -std=gnu++20 -O2 "$ROOT_DIR/offline_evaluator.cpp" "$ROOT_DIR/Utility/pffft.c" -o "$ROOT_DIR/offline_evaluator"

echo "Removing previous output: $OUT"
rm -f "$OUT"

echo "Running per-packet sweep on sample: memories=$MEMS"
"$ROOT_DIR/offline_evaluator" "$SAMPLE" --memories="$MEMS" --per-packet --output="$OUT"

echo "Running comparison on sweep output"
"$ROOT_DIR/.venv/bin/python" "$ROOT_DIR/scripts/compare_reports.py" --ours "$OUT" --authors "$ROOT_DIR/../uMon-WaveSketch/cpp_version/build/report.csv" --outdir "$ROOT_DIR/out_compare_sample_sweep"

echo "Sweep complete. Outputs in out_compare_sample_sweep/ and $OUT"
ls -lh out_compare_sample_sweep || true
