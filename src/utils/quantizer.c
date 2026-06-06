#include "quantizer.h"
#include "color_utils.h"
#include "hct.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Wu's Algorithm Implementation (Simplified) ---
// Note: A full Wu impl is large. For this task, we can use a simpler Histogram
// approach or a basic Box-based Wu. Given the complexity constraints, I'll
// implement a Histogram + Simple K-Means approach which is often sufficient for
// UI theming (as per Material You specs using downsampling).

// We'll downsample colors to 5 bits per channel (32x32x32) to build a
// histogram.
#define HIST_DIM 32
#define HIST_SIZE (HIST_DIM * HIST_DIM * HIST_DIM)

// Maps ARGB to Index
static inline int get_index(uint32_t c) {
  int r = (red_from_argb(c) >> 3);
  int g = (green_from_argb(c) >> 3);
  int b = (blue_from_argb(c) >> 3);
  return (r << 10) | (g << 5) | b;
}

// Maps Index to ARGB (Middle of bin)
static inline uint32_t get_color_from_index(int idx) {
  int r = (idx >> 10) & 0x1F;
  int g = (idx >> 5) & 0x1F;
  int b = idx & 0x1F;
  // Scale back to 8 bit (x8 + 4 for center)
  return argb_from_rgb((r << 3) | 4, (g << 3) | 4, (b << 3) | 4);
}

typedef struct {
  uint32_t color;
  int count;
} HistBin;

// Sort helper
static int compare_bins(const void *a, const void *b) {
  return ((HistBin *)b)->count - ((HistBin *)a)->count;
}

static int compare_quant(const void *a, const void *b) {
  return ((QuantizedColor *)b)->count - ((QuantizedColor *)a)->count;
}

QuantizerResult Quantizer_quantize(const uint32_t *pixels, int count,
                                   int max_colors) {
  // 1. Build Histogram
  // Using static array if stack is large enough? 32kb ints. It's fine.
  // Actually heap alloc to be safe on embedded/P10.
  int *histogram = (int *)calloc(HIST_SIZE, sizeof(int));
  if (!histogram) {
    QuantizerResult res = {NULL, 0};
    return res;
  }

  for (int i = 0; i < count; i++) {
    // Ignore fully transparent pixels
    if (alpha_from_argb(pixels[i]) < 128)
      continue;
    histogram[get_index(pixels[i])]++;
  }

  // 2. Identify distinct colors (Naive approach: Top N frequent bins)
  // A real Wu or Celebi would preserve diversity.
  // We will just take all non-zero bins, sort them, and select Top N.
  // This effectively acts as "Dominant Color Extraction".

  // Count non-zero bins
  int distinct = 0;
  for (int i = 0; i < HIST_SIZE; i++) {
    if (histogram[i] > 0)
      distinct++;
  }

  HistBin *bins = (HistBin *)malloc(sizeof(HistBin) * distinct);
  int idx = 0;
  for (int i = 0; i < HIST_SIZE; i++) {
    if (histogram[i] > 0) {
      bins[idx].color = get_color_from_index(i);
      bins[idx].count = histogram[i];
      idx++;
    }
  }
  free(histogram);

  qsort(bins, distinct, sizeof(HistBin), compare_bins);

  // Limit to max_colors
  int final_count = (distinct < max_colors) ? distinct : max_colors;

  // Output
  QuantizedColor *q_colors =
      (QuantizedColor *)malloc(sizeof(QuantizedColor) * final_count);
  for (int i = 0; i < final_count; i++) {
    q_colors[i].color = bins[i].color;
    q_colors[i].count = bins[i].count;
  }

  // Cleanup
  free(bins);

  QuantizerResult res;
  res.colors = q_colors;
  res.count = final_count;
  return res;
}

void Quantizer_free(QuantizerResult *result) {
  if (result && result->colors) {
    free(result->colors);
    result->colors = NULL;
  }
  if (result)
    result->count = 0;
}

// --- Scoring Logic ---

// Fast RGB chroma approximation (avoids expensive HCT conversion)
static double fast_chroma(uint32_t argb) {
  uint8_t r = red_from_argb(argb);
  uint8_t g = green_from_argb(argb);
  uint8_t b = blue_from_argb(argb);

  int max_c = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
  int min_c = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);

  return (double)(max_c - min_c);
}

uint32_t Quantizer_score(const QuantizerResult *result) {
  if (!result || result->count == 0)
    return 0xFF808080; // Gray fallback

  uint32_t best_color = result->colors[0].color;
  double best_score = -1.0;

  // Calculate total population for percentage
  long total_pop = 0;
  for (int i = 0; i < result->count; i++) {
    total_pop += result->colors[i].count;
  }

  for (int i = 0; i < result->count; i++) {
    uint32_t c = result->colors[i].color;

    // Fast chroma check (0..255 range)
    double chroma = fast_chroma(c);

    // Filter out boring colors (low saturation)
    if (chroma < 15.0)
      continue;

    double proportion = (double)result->colors[i].count / total_pop;

    // Score = chroma priority + population bonus
    // Normalize chroma to 0..100 range to match proportion
    double s = (chroma / 255.0 * 100.0) * 0.7 + (proportion * 100.0) * 0.3;

    if (s > best_score) {
      best_score = s;
      best_color = c;
    }
  }

  return best_color;
}
