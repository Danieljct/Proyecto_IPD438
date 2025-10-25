#!/usr/bin/env bash
# Cleanup generated artifacts from offline evaluation and comparison
set -euo pipefail
echo "Cleaning generated files..."
rm -f benchmark_results_offline*.csv
rm -f benchmark_results_offline_debug.csv
rm -f inspect_flows.txt
rm -f offline_evaluator
rm -rf out_compare || true
echo "Done. Removed benchmark CSVs, inspect files, out_compare/ and binary."
