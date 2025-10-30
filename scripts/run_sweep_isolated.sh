#!/bin/bash
# Memory sweep with ISOLATED builds to ensure no cross-contamination

DATASET=${1:-"websearch25.csv"}
OUTPUT="benchmark_results_offline_full.csv"
MEMORIES=(96 128 192 256)  # Min 96 KB for FULL_DEPTH >= 32

if [ ! -f "$DATASET" ]; then
    echo "Error: Dataset not found: $DATASET"
    exit 1
fi

echo "Running ISOLATED memory sweep on $DATASET"
echo "Memory budgets: ${MEMORIES[@]} KB"
echo "Output: $OUTPUT"

# Remove old output
rm -f "$OUTPUT"
echo "time_s,algorithm,memory_kb,flow_id,k,window_us,packets,are,cosine_sim,euclidean_dist,energy_sim" > "$OUTPUT"

for mem in "${MEMORIES[@]}"; do
    echo ""
    echo "=== Building for memory=$mem KB ==="
    
    # 1. Update parameter.h
    echo "  Setting MEMORY_KB=$mem in parameter.h"
    sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\1${mem}/" Utility/parameter.h
    grep "^#define MEMORY_KB" Utility/parameter.h
    
    # 2. CLEAN BUILD from scratch (nuclear option)
    echo "  Cleaning build directory..."
    rm -rf build
    mkdir -p build
    cd build
    
    # 3. Fresh cmake + compile
    echo "  Running CMake..."
    cmake .. >/dev/null 2>&1
    echo "  Compiling..."
    make offline_evaluator 2>&1 | grep -E "(Building|Linking)"
    
    if [ ! -f offline_evaluator ]; then
        echo "✗ Build failed for memory=$mem KB"
        cd ..
        exit 1
    fi
    
    # 4. Copy binary to parent with unique name
    cp offline_evaluator ../offline_evaluator_mem${mem}
    cd ..
    
    # 5. Run with this specific binary
    # IMPORTANT: Do NOT pass --memories parameter, as the memory is baked into the binary at compile time
    # The --memories parameter only affects labeling, not actual algorithm behavior
    TEMP_OUT="temp_mem_${mem}.csv"
    echo "  Running evaluation with offline_evaluator_mem${mem}..."
    ./offline_evaluator_mem${mem} "$DATASET" \
        --memories="$mem" \
        --output="$TEMP_OUT" \
        --windowUs=1000000 2>&1 | grep -E "(Parsed|Done|finished)"
    
    if [ $? -eq 0 ]; then
        tail -n +2 "$TEMP_OUT" >> "$OUTPUT"
        rm -f "$TEMP_OUT"
        echo "✓ Completed memory=$mem KB"
    else
        echo "✗ Failed at memory=$mem KB"
        exit 1
    fi
done

echo ""
echo "=== Sweep complete ==="
wc -l "$OUTPUT"

# Restore parameter.h
sed -i -E "s/^(#define[[:space:]]+MEMORY_KB[[:space:]]+)[0-9]+/\1256/" Utility/parameter.h
echo "Restored MEMORY_KB=256"

# Keep binaries for inspection
echo ""
echo "Binaries saved:"
ls -lh offline_evaluator_mem* 2>/dev/null
