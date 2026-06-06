#ifndef VISUALIZER_LOADER_H
#define VISUALIZER_LOADER_H

#include "visualizer_interface.h"
#include <stdbool.h>

#define MAX_VISUALIZERS 16

typedef struct {
  char name[64];
  char path[512];
  void *handle; // dlopen handle
  const VisPlugin *plugin;
  bool active;
} VisualizerEntry;

/* Global state of loaded visualizers */
extern VisualizerEntry g_visualizers[MAX_VISUALIZERS];
extern int g_visualizer_count;
extern int g_active_visualizer_index;

/* Initialize loading system */
void visualizer_system_init(void);

/* Scan visualizers/ folder and load plugins */
void visualizer_scan_and_load(void);

/* Unload all plugins */
void visualizer_shutdown(void);

/* Get the current active visualizer (or built-in fallback if none) */
/* Actually, we might just store the index. */
const VisPlugin *visualizer_get_active(void);

/* Set active visualizer by index */
void visualizer_set_active(int index);

/* Save current visualizer's settings to database */
void visualizer_save_settings(void);

/* Load saved settings for a visualizer */
void visualizer_load_settings(const char *vis_name);

#endif // VISUALIZER_LOADER_H
