#ifndef PARAMETER_H
#define PARAMETER_H

#include <algorithm>
#include <bit>

// scale for input time(ns)
#define TIMESCALE 8192u
// binary approximation of sqrt(2)
#define SQRT2F 1.4140625f
#define SQRT2B 0b00110101
#define NOSQRT 0b10000000
// one object can process data in MAX_LENGTH * TIMESCALE ns
#define MAX_LENGTH 2048u
// process data to the third top level, inclusive
#define LEVEL (countr_zero(MAX_LENGTH) - 3)
#define INDEX_MASK ((1u << LEVEL) - 1)
// maximum of data stored in heaps
#define SAMPLE_RATE 32u
// #define HIST_SIZE (MAX_LENGTH / SAMPLE_RATE)
// data in second top level
#define RESERVED 8u
// table dimensions
#define MEMORY_KB 256
#define FULL_WIDTH 256u  // Fixed width (authors used 256 in experiments)
#define HALF_WIDTH (FULL_WIDTH / 2u)
#define FULL_HEIGHT 3u
#define LESS_HEIGHT (FULL_HEIGHT - 1u)
#define PAIR_HEIGHT 2u
// Calculate FULL_DEPTH dynamically based on MEMORY_KB
// Formula: MEMORY = WIDTH × HEIGHT × DEPTH × 4 bytes => DEPTH = MEMORY / (WIDTH × HEIGHT × 4)
// Minimum MEMORY_KB >= 96 required (FULL_DEPTH >= 10 for counter.h formulas)
#define FULL_DEPTH ((MEMORY_KB * 1024) / (FULL_WIDTH * FULL_HEIGHT * 4))
//#define WAVE_DEPTH 55u
//#define PAMS_DEPTH 24u
#define PCMS_DELTA (SAMPLE_RATE * 2)
#define ROUND(a, b) ((a) / (b) + (((b) & 1) == 0 ? (a) % (b) >= (b) / 2 : (a) % (b) > (b) / 2))

#define BUCKET (FULL_WIDTH * FULL_HEIGHT)
#define MEMORY (FULL_WIDTH * FULL_HEIGHT * FULL_DEPTH * 4)
// window for FFT
#define WINDOW (max(32u, bit_ceil(SAMPLE_RATE) * 2u))
// score multiplier for stored flow
#define HIT_RATIO 8u
#define RETAIN_THRESH (FULL_DEPTH * 4u)

#endif //PARAMETER_H
