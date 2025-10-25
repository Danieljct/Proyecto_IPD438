#!/usr/bin/env bash
# Build and run a quick offline evaluator on a small sample (head) of the dataset.
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DATASET="$ROOT_DIR/../uMon-WaveSketch/data/websearch25.csv"
SAMPLE_CSV="$ROOT_DIR/data_sample.csv"
OUT_SAMPLE="benchmark_results_offline_sample.csv"

echo "Creating small sample (first 2000 lines) -> $SAMPLE_CSV"
head -n 2000 "$DATASET" > "$SAMPLE_CSV"

echo "Compiling offline_evaluator..."
g++ -std=gnu++20 -O2 "$ROOT_DIR/offline_evaluator.cpp" "$ROOT_DIR/Utility/pffft.c" -o "$ROOT_DIR/offline_evaluator"

echo "Running offline_evaluator on sample (memories=64)"
"$ROOT_DIR/offline_evaluator" "$SAMPLE_CSV" --memories=64 --output="$OUT_SAMPLE"

echo "Running compare script on sample"
"$ROOT_DIR/.venv/bin/python" "$ROOT_DIR/scripts/compare_reports.py" --ours "$OUT_SAMPLE" --authors "$ROOT_DIR/../uMon-WaveSketch/cpp_version/build/report.csv" --outdir "$ROOT_DIR/out_compare_sample"

echo "Sample run complete. Outputs:" 
ls -lh "$OUT_SAMPLE" || true
ls -lh "$ROOT_DIR/out_compare_sample" || true
