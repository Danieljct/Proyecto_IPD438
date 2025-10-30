#!/bin/bash
# Memory sweep on full dataset with recompilation for each memory budget
# This ensures the algorithms actually use the specified memory

DATASET=${1:-"websearch25.csv"}
OUTPUT="benchmark_results_offline_full.csv"
MEMORIES=(32 64 128 256)  # Min 32 KB due to DEPTH formula constraints

if [ ! -f "$DATASET" ]; then
    echo "Error: Dataset not found: $DATASET"
    exit 1
fi

echo "Running memory sweep on $DATASET"
echo "Memory budgets: ${MEMORIES[@]} KB"
echo "Output: $OUTPUT"
echo "NOTE: Will recompile for each memory budget (required by design)"

# Remove old output and create with header
rm -f "$OUTPUT"
echo "time_s,algorithm,memory_kb,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist,energy_sim" > "$OUTPUT"

# Process each memory budget separately with recompilation
for mem in "${MEMORIES[@]}"; do
    echo ""
    echo "=== Processing memory=$mem KB ==="
    
    # 1. Update parameter.h with the target memory
    echo "  Updating Utility/parameter.h to MEMORY_KB=$mem"
    # Use the same approach as the authors: replace only the value, preserving comments
    sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\1${mem}/" Utility/parameter.h
    
    # Force CMake to detect the change by touching the header
    touch Utility/parameter.h
    
    # Verify the change
    grep "MEMORY_KB" Utility/parameter.h | head -1
    
    # 2. Recompile offline_evaluator
    echo "  Recompiling..."
    cd build
    # Force recompilation by removing object files
    rm -f CMakeFiles/offline_evaluator.dir/offline_evaluator.cpp.o
    rm -f CMakeFiles/offline_evaluator.dir/Utility/pffft.c.o
    make offline_evaluator 2>&1 | grep -E "(Scanning|Building|Linking|Error)"
    if [ ${PIPESTATUS[0]} -ne 0 ]; then
        echo "✗ Compilation failed for memory=$mem KB"
        cd ..
        exit 1
    fi
    cd ..
    
    # 3. Run evaluator
    TEMP_OUT="temp_mem_${mem}.csv"
    echo "  Running evaluation..."
    ./offline_evaluator "$DATASET" \
        --memories="$mem" \
        --output="$TEMP_OUT" \
        --windowUs=1000000 2>&1 | grep -E "(Parsed|Done|finished)"
    
    if [ $? -eq 0 ]; then
        # Append results (skip header)
        tail -n +2 "$TEMP_OUT" >> "$OUTPUT"
        rm -f "$TEMP_OUT"
        echo "✓ Completed memory=$mem KB"
    else
        echo "✗ Failed at memory=$mem KB"
        rm -f "$TEMP_OUT"
        exit 1
    fi
done

echo ""
echo "=== Sweep complete ==="
wc -l "$OUTPUT"

# Restore parameter.h to a safe default (256 KB)
echo ""
echo "Restoring Utility/parameter.h to default (MEMORY_KB=256)"
sed -i "s/^#define MEMORY_KB .*/#define MEMORY_KB 256 \/\/ Default/" Utility/parameter.h

echo ""
echo "Generate plots with:"
echo "  source .venv/bin/activate"
echo "  python scripts/visualize_results.py $OUTPUT out_plots_full"
