#!/bin/bash
# Chunked evaluator: splits large dataset and processes in chunks to avoid OOM
# Usage: ./run_chunked_evaluation.sh <input.csv> <output.csv> <memories> <chunk_size>

INPUT_CSV=${1:-"websearch25.csv"}
OUTPUT_CSV=${2:-"benchmark_results_offline_full.csv"}
MEMORIES=${3:-"64,128,256"}
CHUNK_SIZE=${4:-10000}  # lines per chunk

if [ ! -f "$INPUT_CSV" ]; then
    echo "Error: Input file not found: $INPUT_CSV"
    exit 1
fi

# Count total lines
TOTAL_LINES=$(wc -l < "$INPUT_CSV")
echo "Processing $INPUT_CSV ($TOTAL_LINES lines) in chunks of $CHUNK_SIZE"
echo "Output: $OUTPUT_CSV"
echo "Memory budgets: $MEMORIES"

# Remove old output
rm -f "$OUTPUT_CSV"

# Process in chunks
START=1
CHUNK_NUM=1

while [ $START -le $TOTAL_LINES ]; do
    END=$((START + CHUNK_SIZE - 1))
    if [ $END -gt $TOTAL_LINES ]; then
        END=$TOTAL_LINES
    fi
    
    CHUNK_FILE="chunk_${CHUNK_NUM}.csv"
    echo ""
    echo "=== Chunk $CHUNK_NUM: lines $START-$END ==="
    
    # Extract chunk
    sed -n "${START},${END}p" "$INPUT_CSV" > "$CHUNK_FILE"
    
    # Process chunk (without --per-packet to save memory)
    ./offline_evaluator "$CHUNK_FILE" \
        --memories="$MEMORIES" \
        --output="$OUTPUT_CSV" \
        --windowUs=1000000
    
    if [ $? -ne 0 ]; then
        echo "Error processing chunk $CHUNK_NUM"
        rm -f "$CHUNK_FILE"
        exit 1
    fi
    
    # Cleanup
    rm -f "$CHUNK_FILE"
    
    # Next chunk
    START=$((END + 1))
    CHUNK_NUM=$((CHUNK_NUM + 1))
done

echo ""
echo "✓ Processing complete: $OUTPUT_CSV"
echo "Total chunks processed: $((CHUNK_NUM - 1))"
wc -l "$OUTPUT_CSV"
