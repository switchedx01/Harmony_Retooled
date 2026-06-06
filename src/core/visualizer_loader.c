#include "visualizer_loader.h"
#include "common.h"
#include "database.h"
#include "logging.h"
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

VisualizerEntry g_visualizers[MAX_VISUALIZERS];
int g_visualizer_count = 0;
int g_active_visualizer_index = -1;

void visualizer_system_init(void) {
  memset(g_visualizers, 0, sizeof(g_visualizers));
  g_visualizer_count = 0;
  g_active_visualizer_index = -1;

  // We should scan on init
  visualizer_scan_and_load();
}

void visualizer_scan_and_load(void) {
  // First, unload any existing provided scanning
  visualizer_shutdown();

  // Create folder if not exists?
  // Assume visualizers/ is relative to executable or known path
  const char *vis_dir = "./visualizers";
  DIR *d = opendir(vis_dir);
  if (!d) {
    log_message("WARN", "Visualizers directory not found.");
    return;
  }

  struct dirent *dir;
  while ((dir = readdir(d)) != NULL) {
    if (g_visualizer_count >= MAX_VISUALIZERS)
      break;

    // Check for .so (Linux)
    // Windows would check .dll
    if (strstr(dir->d_name, ".so")) {
      /* Construct path and load */
      char full_path[MAX_PATH_LENGTH];
      snprintf(full_path, sizeof(full_path), "./visualizers/%s", dir->d_name);

      void *handle = dlopen(full_path, RTLD_NOW | RTLD_LOCAL);
      if (!handle) {
        continue;
      }

      union {
        void *obj;
        VisGetInfoFunc func;
      } cast;
      cast.obj = dlsym(handle, "visualizer_get_info");

      if (!cast.obj) {
        log_message("ERROR", dlerror());
        dlclose(handle);
        continue;
      }

      const VisPlugin *plugin = cast.func();

      // Success
      VisualizerEntry *entry = &g_visualizers[g_visualizer_count];
      strncpy(entry->name, plugin->name, sizeof(entry->name) - 1);
      strncpy(entry->path, full_path, sizeof(entry->path) - 1);
      entry->handle = handle;
      entry->plugin = plugin;
      entry->active = false;

      log_message("INFO", "Loaded visualizer plugin.");

      g_visualizer_count++;
    }
  }
  closedir(d);

  // Set default if any loaded
  if (g_visualizer_count > 0 && g_active_visualizer_index == -1) {
    g_active_visualizer_index = 0;
  }
}

void visualizer_shutdown(void) {
  for (int i = 0; i < g_visualizer_count; i++) {
    if (g_visualizers[i].active && g_visualizers[i].plugin->cleanup) {
      g_visualizers[i].plugin->cleanup();
    }
    if (g_visualizers[i].handle) {
      dlclose(g_visualizers[i].handle);
    }
  }
  memset(g_visualizers, 0, sizeof(g_visualizers));
  g_visualizer_count = 0;
  g_active_visualizer_index = -1;
}

#include "builtin_visualizers.h"

const VisPlugin *visualizer_get_active(void) {
  if (g_active_visualizer_index >= 0 &&
      g_active_visualizer_index < g_visualizer_count) {
    return g_visualizers[g_active_visualizer_index].plugin;
  }
  return &g_vis_basic_wave; // Internal fallback
}

void visualizer_set_active(int index) {
  if (index == g_active_visualizer_index) {
    return;
  }

  if (index >= -1 && index < g_visualizer_count) {
    // Cleanup old
    if (g_active_visualizer_index >= 0) {
      const VisPlugin *old = g_visualizers[g_active_visualizer_index].plugin;
      if (old && old->cleanup)
        old->cleanup();
    }

    g_active_visualizer_index = index;

    // Init new
    if (index >= 0) {
      const VisPlugin *new_plug = g_visualizers[index].plugin;
      if (new_plug && new_plug->init) {
        /* Default to 0,0 - renderer resize should handle it */
        new_plug->init(0, 0);
      }
      /* Load saved settings for this visualizer */
      visualizer_load_settings(g_visualizers[index].name);
    }
  }
}

/* ========================================
 *       Visualizer Settings Persistence
 * ======================================== */

/* Context for the load callback */
static const VisPlugin *g_load_target_plugin = NULL;

static void apply_loaded_param(const char *param_name, int value_type,
                               float value_float, int value_int) {
  if (!g_load_target_plugin || !g_load_target_plugin->get_param_count ||
      !g_load_target_plugin->get_param)
    return;

  int count = g_load_target_plugin->get_param_count();
  for (int i = 0; i < count; i++) {
    const VisParam *p = g_load_target_plugin->get_param(i);
    if (!p || !p->name || !p->value_ptr)
      continue;

    if (strcmp(p->name, param_name) == 0) {
      /* Match found - apply value based on type */
      if (p->type == VIS_PARAM_FLOAT && value_type == VIS_PARAM_FLOAT) {
        *(float *)p->value_ptr = value_float;
      } else if (p->type == VIS_PARAM_INT && value_type == VIS_PARAM_INT) {
        *(int *)p->value_ptr = value_int;
      } else if (p->type == VIS_PARAM_BOOL && value_type == VIS_PARAM_BOOL) {
        *(bool *)p->value_ptr = (value_int != 0);
      } else if (p->type == VIS_PARAM_ENUM && value_type == VIS_PARAM_ENUM) {
        *(int *)p->value_ptr = value_int;
      }
      break;
    }
  }
}

void visualizer_load_settings(const char *vis_name) {
  if (!vis_name)
    return;

  /* Find the plugin for this visualizer */
  for (int i = 0; i < g_visualizer_count; i++) {
    if (strcmp(g_visualizers[i].name, vis_name) == 0) {
      g_load_target_plugin = g_visualizers[i].plugin;
      break;
    }
  }

  if (!g_load_target_plugin) {
    /* Try built-in fallback */
    if (strcmp(vis_name, g_vis_basic_wave.name) == 0) {
      g_load_target_plugin = &g_vis_basic_wave;
    } else {
      return;
    }
  }

  db_load_visualizer_params(vis_name, apply_loaded_param);
  g_load_target_plugin = NULL;

  log_message("DEBUG", "Loaded visualizer settings");
}

void visualizer_save_settings(void) {
  const VisPlugin *active = visualizer_get_active();
  if (!active || !active->name || !active->get_param_count ||
      !active->get_param)
    return;

  int count = active->get_param_count();
  for (int i = 0; i < count; i++) {
    const VisParam *p = active->get_param(i);
    if (!p || !p->name || !p->value_ptr)
      continue;

    float value_float = 0.0f;
    int value_int = 0;

    if (p->type == VIS_PARAM_FLOAT) {
      value_float = *(float *)p->value_ptr;
    } else if (p->type == VIS_PARAM_INT) {
      value_int = *(int *)p->value_ptr;
    } else if (p->type == VIS_PARAM_BOOL) {
      value_int = *(bool *)p->value_ptr ? 1 : 0;
    } else if (p->type == VIS_PARAM_ENUM) {
      value_int = *(int *)p->value_ptr;
    }

    db_save_visualizer_param(active->name, p->name, (int)p->type, value_float,
                             value_int);
  }

  log_message("DEBUG", "Saved visualizer settings");
}
