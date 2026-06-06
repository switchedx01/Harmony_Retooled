#ifndef PLAYER_H
#define PLAYER_H

#include "common.h"
#include "database.h" /* pulls in EQ_BAND_COUNT_MAX + EqPreset */

#define HARMONY_VERSION "v2.0.01-beta (Moonlight)"

typedef enum {
  PLAYER_STATE_STOPPED,
  PLAYER_STATE_PLAYING,
  PLAYER_STATE_PAUSED
} PlayerState;

typedef enum { REPEAT_OFF, REPEAT_ALL, REPEAT_ONE } RepeatMode;

typedef struct {
  char title[MAX_SONG_TITLE];
  char artist[MAX_SONG_TITLE];
  char album[MAX_SONG_TITLE];
  char path[MAX_PATH_LENGTH];
  char art_path[MAX_PATH_LENGTH];
} Song;

typedef enum {
  QUEUE_TYPE_RECENTS,
  QUEUE_TYPE_ALBUM,
  QUEUE_TYPE_PLAYLIST
} QueueType;

typedef enum {
  SCENE_NOW_PLAYING,
  SCENE_LIBRARY,
  SCENE_PLAYLISTS,
  SCENE_SETTINGS,
  SCENE_VISUALIZER
} AppScene;

typedef enum {
  SPOTLIGHT_PHASE_1_INIT,   /* Ghost & Lift */
  SPOTLIGHT_PHASE_2_HUB,    /* Front of Card */
  SPOTLIGHT_PHASE_3_META,   /* Back of Card (The Flip) */
  SPOTLIGHT_PHASE_4_EXPAND, /* Panel View (80%) */
  SPOTLIGHT_PHASE_5_CLOSE   /* Return to Grid */
} SpotlightPhase;

typedef enum {
  LIBRARY_VIEW_GRID,
  LIBRARY_VIEW_LIST,
  LIBRARY_VIEW_PLAYLISTS
} LibraryViewMode;

typedef enum {
  LIBRARY_SORT_NAME,
  LIBRARY_SORT_ARTIST,
  LIBRARY_SORT_YEAR,
  LIBRARY_SORT_RECENT
} LibrarySortMode;

typedef enum {
  LIBRARY_FILTER_ALL,
  LIBRARY_FILTER_FAVORITES,
  LIBRARY_FILTER_RECENT
} LibraryFilterMode;

typedef struct {
  bool active;
  int x;
  int y;
  int target_type; /* 0=None, 1=Track, 2=Album, 3=Playlist */
  int target_id;   /* Database ID or Index depending on context */
  char target_path[MAX_PATH_LENGTH]; /* For files */
  /* Submenu State */
  bool show_playlists;
} ContextMenuState;

typedef struct {
  Song songs[MAX_PLAYLIST_SIZE];
  size_t count;
  int current_index;
  float volume;   /* 0.0 to 1.0 */
  float position; /* Current play position in seconds */
  float duration; /* Total duration in seconds */
  PlayerState state;
  RepeatMode repeat_mode;
  bool shuffle_mode;
  int *shuffle_indices; /* Dynamic array of indices if shuffled */

  /* App State */
  AppScene current_scene;
  char search_query[64];
  bool is_typing_search; /* True when search bar is focused */
  /* Queue/Context info */
  QueueType queue_type;
  char queue_context_name[MAX_SONG_TITLE];
  char queue_context_art_path[MAX_PATH_LENGTH];

  /* Recents history */
  Song recents[10];
  size_t recents_count;

  /* UI State */
  bool sidebar_left_open;
  bool sidebar_right_open;
  float sidebar_left_anim;  /* 0.0 = closed, 1.0 = open */
  float sidebar_right_anim; /* 0.0 = closed, 1.0 = open */
  bool settings_popup_open;
  float settings_popup_anim;
  int settings_active_tab; /* 0=Library, 1=Audio, 2=About */

  /* Settings & Input */
  char library_input_buffer[512];
  bool is_typing_library_path;
  bool setting_group_albums;
  bool setting_clean_db_on_scan;
  char input_clipboard_buffer[1024];

  /* Library State */
  float library_scroll_y;
  float library_max_scroll;
  float library_last_scroll_y;
  float library_header_offset; /* 0 to -100 (hidden) */

  LibraryViewMode library_view_mode;
  LibrarySortMode library_sort_mode;
  LibraryFilterMode library_filter_mode;
  int library_hovered_album_idx; /* -1 if none */
  int library_hovered_heart_idx; /* -1 if none */

  /* Library Cache */
  Album *library_albums;
  size_t library_album_count;
  size_t *library_filtered_indices;
  size_t library_filtered_count;

  Track *library_tracks;
  size_t library_track_count;
  size_t *library_filtered_track_indices;
  size_t library_filtered_track_count;

  /* Library Paths Cache */
  LibraryPath *library_paths;
  size_t library_path_count;

  /* Playlists Cache */
  Playlist *library_playlists;
  size_t library_playlist_count;
  bool is_creating_playlist; /* Dialog Open */
  char new_playlist_name[64];

  /* Context Menu */
  ContextMenuState context_menu;

  bool library_needs_filter;
  float
      library_search_album_scroll_x; /* Horizontal scroll for search results */

  /* Sidebar State */
  float sidebar_right_scroll_y;
  float sidebar_right_max_scroll;

  /* Visualizer State */
  int visualizer_active_index; /* -1 for none or internal default? Loader
                                  handles indices */
  bool visualizer_link_colors;
  bool visualizer_show_settings;
  bool visualizer_show_list;
  int visualizer_drag_param_index; /* -1 if not dragging */
  float visualizer_scroll_y;
  float visualizer_max_scroll;

  /* Spotlight Feature State */
  bool spotlight_active;
  float spotlight_anim;        /* 0.0 to 1.0 (Lift) */
  float spotlight_flip_anim;   /* 0.0 to 1.0 (Flip Width) */
  float spotlight_expand_anim; /* 0.0 to 1.0 (Expand) */
  SpotlightPhase spotlight_phase;
  bool spotlight_is_closing; /* Triggers return to grid logic */
  int spotlight_album_idx;   /* Global index in library_albums */
  bool spotlight_is_singular;
  char spotlight_song_path[MAX_PATH_LENGTH];
  char spotlight_song_title[MAX_SONG_TITLE];
  char spotlight_song_artist[MAX_SONG_TITLE];
  char spotlight_song_album[MAX_SONG_TITLE];
  char spotlight_song_genre[64];
  int spotlight_song_year;
  float spotlight_song_duration;
  char spotlight_song_art_path[MAX_PATH_LENGTH];
  struct {
    int x, y, w, h; /* Capture grid coords */
  } spotlight_source_rect;

  /* Sidebar Browsing Mode */
  bool sidebar_is_browsing;
  Track *browse_tracks;
  size_t browse_track_count;
  char browse_context_name[MAX_SONG_TITLE];
  char browse_context_art_path[MAX_PATH_LENGTH];

  Track *spotlight_tracks; /* Tracks for expanded view */
  size_t spotlight_track_count;
  float spotlight_scroll_y;
  float spotlight_max_scroll;

  /* Confirmation Dialog */
  bool confirm_dialog_open;
  char confirm_dialog_message[256];
  char confirm_dialog_action[64];

  /* Queue Drag-to-Reorder */
  int queue_drag_index; /* -1 if not dragging */
  bool queue_is_dragging;
  int queue_drag_target_index; /* insertion point */

  /* Hover Tooltip */
  char tooltip_text[128];
  int tooltip_x;
  int tooltip_y;
  float tooltip_timer; /* seconds hovered */

  /* Mini Player State */
  bool mini_player_visible;
  bool mini_player_expanded;
  bool mini_player_pinned;

  /* Audio Settings Stubs */
  bool setting_normalization;
  bool setting_gapless;

  /* Updater State */
  char update_status_msg[128];

  /* -----------------------------------------------------------------------
     EQ State
     ----------------------------------------------------------------------- */
  bool eq_popup_open; /* EQ popup is visible */
  bool eq_enabled;    /* EQ chain is active (bypass when false) */
  int eq_band_count;  /* 5 or 10 */
  float eq_gains[EQ_BAND_COUNT_MAX]; /* Current gains in dB (-12 to +12) */

  /* Preset selection: -1 = built-in (no delete allowed), >= 0 = user preset id
   */
  int eq_selected_preset_id;        /* -1 for a built-in preset */
  char eq_selected_preset_name[64]; /* display name of active preset */
  bool eq_is_new_mode;              /* user selected "New" from dropdown */
  char eq_new_preset_name[64];      /* name input buffer while saving */
  bool eq_typing_preset_name;       /* text input active for preset name */
  bool eq_dropdown_open;            /* preset dropdown is expanded */

  /* EQ Preset Cache (loaded from DB) */
  EqPreset *eq_presets;
  size_t eq_preset_count;
} PlayerContext;

/* Core Player API */
Result player_init(PlayerContext *ctx);
Result player_add_song(PlayerContext *ctx, const char *title,
                       const char *artist, const char *album, const char *path,
                       const char *art_path);
Result player_play(PlayerContext *ctx);
Result player_pause(PlayerContext *ctx);
Result player_stop(PlayerContext *ctx);
Result player_next(PlayerContext *ctx);
Result player_prev(PlayerContext *ctx);
Result player_play_at(PlayerContext *ctx, int index);
void player_add_to_recents(PlayerContext *ctx, const Song *song);
void player_set_shuffle(PlayerContext *ctx, bool enable);
void player_set_repeat(PlayerContext *ctx, RepeatMode mode);

/* Hub Integration */
void player_send_status_to_hub(PlayerContext *ctx, const char *event_name);

#endif /* PLAYER_H */
