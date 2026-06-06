#ifndef VISUALIZER_INTERFACE_H
#define VISUALIZER_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>
/* For Color struct and ThemeColors. If not available in plugin SDK,
 * plugins should define their own identical struct or include this.
 * We will assume plugins can include our headers, or we define a simple struct
 * here.
 */
#include "color_extractor.h"
#include "color_utils.h"

/* Forward declare SDL_Renderer to avoid hard dependency in header */
typedef struct SDL_Renderer SDL_Renderer;

/*
 * Visualizer Plugin Interface
 * ----------------------------
 * A strict ABI-compatible struct that modules must populate and return.
 */

// Function pointer typedefs
typedef void (*VisInitFunc)(int width, int height);
typedef void (*VisUpdateFunc)(const float *audio_data, size_t sample_count);
// Note: We might separate Update (data processing) from Render (drawing).
// But for simplicity, we pass data in Render or have a separate Update call.
// The request asks for: render(buffer_data, context, theme_colors).
// Let's stick to that.

typedef void (*VisRenderFunc)(SDL_Renderer *renderer, const float *audio_data,
                              size_t sample_count, int x, int y, int w, int h,
                              const ThemeColors *theme);

typedef void (*VisResizeFunc)(int width,
                              int height); // Optional, if resize happens
typedef void (*VisCleanupFunc)(void);

/* Parameter Logic */
typedef enum {
  VIS_PARAM_FLOAT,
  VIS_PARAM_INT,
  VIS_PARAM_BOOL,
  VIS_PARAM_ENUM,
} VisParamType;

typedef struct {
  const char *name;
  VisParamType type;
  void *value_ptr; /* Pointer to the actual static variable in the plugin */
  float min;
  float max;
  const char **options; /* NULL-terminated list of strings for ENUM type */
} VisParam;

typedef int (*VisGetParamCountFunc)(void);
typedef const VisParam *(*VisGetParamFunc)(int index);

typedef struct {
  const char *name;
  const char *author;

  VisInitFunc init;
  VisRenderFunc render;
  VisCleanupFunc cleanup;

  // Optional
  VisResizeFunc resize;

  // Parameters
  VisGetParamCountFunc get_param_count;
  VisGetParamFunc get_param;
} VisPlugin;

/*
 * Plugins must export a function named `visualizer_get_info`
 * that returns a pointer to a static VisPlugin struct.
 */
typedef const VisPlugin *(*VisGetInfoFunc)(void);

#endif // VISUALIZER_INTERFACE_H
