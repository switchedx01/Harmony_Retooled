#include "mpris_service.h"
#include "logging.h"
#include <gio/gio.h>

#include <stdio.h>
#include <string.h>

/* Introspection definition for minimal MPRIS support */
static const gchar introspection_xml[] =
    "<node>"
    "  <interface name='org.mpris.MediaPlayer2'>"
    "    <method name='Raise' />"
    "    <method name='Quit' />"
    "    <property name='CanQuit' type='b' access='read' />"
    "    <property name='CanRaise' type='b' access='read' />"
    "    <property name='HasTrackList' type='b' access='read' />"
    "    <property name='Identity' type='s' access='read' />"
    "    <property name='DesktopEntry' type='s' access='read' />"
    "    <property name='SupportedUriSchemes' type='as' access='read' />"
    "    <property name='SupportedMimeTypes' type='as' access='read' />"
    "  </interface>"
    "  <interface name='org.mpris.MediaPlayer2.Player'>"
    "    <method name='Next' />"
    "    <method name='Previous' />"
    "    <method name='Pause' />"
    "    <method name='PlayPause' />"
    "    <method name='Stop' />"
    "    <method name='Play' />"
    "    <method name='Seek'>"
    "      <arg direction='in' name='Offset' type='x' />"
    "    </method>"
    "    <method name='SetPosition'>"
    "      <arg direction='in' name='TrackId' type='o' />"
    "      <arg direction='in' name='Position' type='x' />"
    "    </method>"
    "    <method name='OpenUri'>"
    "      <arg direction='in' name='Uri' type='s' />"
    "    </method>"
    "    <property name='PlaybackStatus' type='s' access='read' />"
    "    <property name='LoopStatus' type='s' access='readwrite' />"
    "    <property name='Rate' type='d' access='readwrite' />"
    "    <property name='Shuffle' type='b' access='readwrite' />"
    "    <property name='Metadata' type='a{sv}' access='read' />"
    "    <property name='Volume' type='d' access='readwrite' />"
    "    <property name='Position' type='x' access='read' />"
    "    <property name='MinimumRate' type='d' access='read' />"
    "    <property name='MaximumRate' type='d' access='read' />"
    "    <property name='CanGoNext' type='b' access='read' />"
    "    <property name='CanGoPrevious' type='b' access='read' />"
    "    <property name='CanPlay' type='b' access='read' />"
    "    <property name='CanPause' type='b' access='read' />"
    "    <property name='CanSeek' type='b' access='read' />"
    "    <property name='CanControl' type='b' access='read' />"
    "  </interface>"
    "</node>";

/* Context state */
static GDBusNodeInfo *introspection_data = NULL;
static guint owner_id = 0;
static MprisCallbacks g_callbacks = {0};
static GDBusConnection *g_connection = NULL;

/* Property storage */
static gchar *g_playback_status = "Stopped"; /* Playing, Paused, Stopped */
static gchar *g_loop_status = "None";        /* None, Track, Playlist */
static gdouble g_rate = 1.0;
static gboolean g_shuffle = FALSE;
static gdouble g_volume = 1.0;
static gint64 g_position = 0;

/* Metadata storage */
static GVariant *g_metadata = NULL;

/* Helper to convert file path to URI */
static char *path_to_uri(const char *path) {
  if (!path)
    return NULL;
  GFile *file = g_file_new_for_path(path);
  char *uri = g_file_get_uri(file);
  g_object_unref(file);
  return uri;
}

static void update_metadata_variant(const char *title, const char *artist,
                                    const char *album, const char *art_url,
                                    int64_t duration_us) {
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

  /* Required ID - use a fake one as we don't expose full tracklist often */
  g_variant_builder_add(
      &builder, "{sv}", "mpris:trackid",
      g_variant_new("o", "/org/mpris/MediaPlayer2/CurrentTrack"));

  if (title)
    g_variant_builder_add(&builder, "{sv}", "xesam:title",
                          g_variant_new("s", title));

  if (artist) {
    GVariantBuilder ab;
    g_variant_builder_init(&ab, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&ab, "s", artist);
    g_variant_builder_add(&builder, "{sv}", "xesam:artist",
                          g_variant_builder_end(&ab));
  }

  if (album)
    g_variant_builder_add(&builder, "{sv}", "xesam:album",
                          g_variant_new("s", album));

  if (art_url) {
    /* If it's not a URI (doesn't start with file://), convert it */
    char *uri = NULL;
    if (strstr(art_url, "://") == NULL) {
      uri = path_to_uri(art_url);
    }
    g_variant_builder_add(&builder, "{sv}", "mpris:artUrl",
                          g_variant_new("s", uri ? uri : art_url));
    if (uri)
      g_free(uri);
  }

  if (duration_us > 0)
    g_variant_builder_add(&builder, "{sv}", "mpris:length",
                          g_variant_new("x", duration_us));

  if (g_metadata)
    g_variant_unref(g_metadata);

  g_metadata = g_variant_builder_end(&builder);
  g_variant_ref_sink(g_metadata); /* Ensure floating ref is claimed */
}

/* DBus Method Call Handler */
static void handle_method_call(GDBusConnection *connection, const gchar *sender,
                               const gchar *object_path,
                               const gchar *interface_name,
                               const gchar *method_name, GVariant *parameters,
                               GDBusMethodInvocation *invocation,
                               gpointer user_data) {
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)user_data;

  // log_message("DEBUG", "MPRIS Method: %s.%s", interface_name, method_name);

  /* org.mpris.MediaPlayer2 */
  if (g_strcmp0(interface_name, "org.mpris.MediaPlayer2") == 0) {
    if (g_strcmp0(method_name, "Raise") == 0) {
      if (g_callbacks.on_raise)
        g_callbacks.on_raise();
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Quit") == 0) {
      if (g_callbacks.on_quit)
        g_callbacks.on_quit();
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else {
      /* Unknown method */
    }
  }
  /* org.mpris.MediaPlayer2.Player */
  else if (g_strcmp0(interface_name, "org.mpris.MediaPlayer2.Player") == 0) {
    if (g_strcmp0(method_name, "Next") == 0) {
      if (g_callbacks.on_next)
        g_callbacks.on_next();
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Previous") == 0) {
      if (g_callbacks.on_prev)
        g_callbacks.on_prev();
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Pause") == 0) {
      if (g_callbacks.on_pause)
        g_callbacks.on_pause();
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "PlayPause") == 0) {
      /* Toggle based on current state */
      if (g_strcmp0(g_playback_status, "Playing") == 0) {
        if (g_callbacks.on_pause)
          g_callbacks.on_pause();
      } else {
        if (g_callbacks.on_play)
          g_callbacks.on_play();
      }
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Stop") == 0) {
      if (g_callbacks.on_pause)
        g_callbacks.on_pause(); /* Stop acts as pause usually */
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Play") == 0) {
      if (g_callbacks.on_play)
        g_callbacks.on_play();
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "Seek") == 0) {
      gint64 offset;
      g_variant_get(parameters, "(x)", &offset);
      if (g_callbacks.on_seek)
        g_callbacks.on_seek(offset);
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "SetPosition") == 0) {
      /* Arg1: TrackId (o), Arg2: Position (x) */
      /* We ignore TrackId check for simplicity for now */
      gchar *track_id;
      gint64 pos;
      g_variant_get(parameters, "(ox)", &track_id, &pos);
      if (g_callbacks.on_set_position)
        g_callbacks.on_set_position(pos);
      g_free(track_id);
      g_dbus_method_invocation_return_value(invocation, NULL);
    } else if (g_strcmp0(method_name, "OpenUri") == 0) {
      gchar *uri = NULL;
      /* MPRIS method parameters are packed as a tuple, so "(s)" is correct
         natively, BUT if it's failing here, let's use variant functions
         directly to be safe, or just fix the format string if we were receiving
         raw strings. Actually, dbus parameters are always a tuple. Let's trace
         it carefully. */
      log_message("DEBUG", "OpenUri method hit");
      g_variant_get(parameters, "(s)", &uri);

      if (uri) {
        log_message("DEBUG", "OpenUri uri extracted");
        if (g_callbacks.on_open_uri)
          g_callbacks.on_open_uri(uri);
        g_free(uri);
      } else {
        log_message("ERROR", "OpenUri parameter was null after parsing");
      }
      g_dbus_method_invocation_return_value(invocation, NULL);
    }
  }
}

/* DBus Property Get Handler */
static GVariant *handle_get_property(GDBusConnection *connection,
                                     const gchar *sender,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *property_name, GError **error,
                                     gpointer user_data) {
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)user_data;
  (void)error;

  if (g_strcmp0(interface_name, "org.mpris.MediaPlayer2") == 0) {
    if (g_strcmp0(property_name, "CanQuit") == 0)
      return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "CanRaise") == 0)
      return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "HasTrackList") == 0)
      return g_variant_new_boolean(FALSE);
    if (g_strcmp0(property_name, "Identity") == 0)
      return g_variant_new_string("Harmony Player");
    if (g_strcmp0(property_name, "DesktopEntry") == 0)
      return g_variant_new_string("harmony_player"); /* .desktop filename */
    if (g_strcmp0(property_name, "SupportedUriSchemes") == 0) {
      GVariantBuilder b;
      g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
      g_variant_builder_add(&b, "s", "file");
      return g_variant_builder_end(&b);
    }
    if (g_strcmp0(property_name, "SupportedMimeTypes") == 0) {
      GVariantBuilder b;
      g_variant_builder_init(&b, G_VARIANT_TYPE("as"));
      g_variant_builder_add(&b, "s", "audio/mpeg");
      g_variant_builder_add(&b, "s", "audio/x-wav");
      return g_variant_builder_end(&b);
    }
  } else if (g_strcmp0(interface_name, "org.mpris.MediaPlayer2.Player") == 0) {
    if (g_strcmp0(property_name, "PlaybackStatus") == 0)
      return g_variant_new_string(g_playback_status);
    if (g_strcmp0(property_name, "LoopStatus") == 0)
      return g_variant_new_string(g_loop_status);
    if (g_strcmp0(property_name, "Rate") == 0)
      return g_variant_new_double(g_rate);
    if (g_strcmp0(property_name, "Shuffle") == 0)
      return g_variant_new_boolean(g_shuffle);
    if (g_strcmp0(property_name, "Metadata") == 0)
      return g_metadata ? g_variant_ref(g_metadata)
                        : g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0);
    if (g_strcmp0(property_name, "Volume") == 0)
      return g_variant_new_double(g_volume);
    if (g_strcmp0(property_name, "Position") == 0)
      return g_variant_new_int64(
          g_position); /* Use stored position or query player? For simplicity
                          returned stored. */

    /* Capabilities */
    if (g_strcmp0(property_name, "MinimumRate") == 0)
      return g_variant_new_double(1.0);
    if (g_strcmp0(property_name, "MaximumRate") == 0)
      return g_variant_new_double(1.0);
    if (g_strcmp0(property_name, "CanGoNext") == 0)
      return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "CanGoPrevious") == 0)
      return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "CanPlay") == 0)
      return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "CanPause") == 0)
      return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "CanSeek") == 0)
      return g_variant_new_boolean(TRUE);
    if (g_strcmp0(property_name, "CanControl") == 0)
      return g_variant_new_boolean(TRUE);
  }

  return NULL;
}

static gboolean handle_set_property(GDBusConnection *connection,
                                    const gchar *sender,
                                    const gchar *object_path,
                                    const gchar *interface_name,
                                    const gchar *property_name, GVariant *value,
                                    GError **error, gpointer user_data) {
  /* Minimal implementation - mostly read-only for now */
  (void)connection;
  (void)sender;
  (void)object_path;
  (void)value;
  (void)error;
  (void)user_data;
  (void)interface_name;
  (void)property_name;
  return FALSE;
}

static const GDBusInterfaceVTable interface_vtable = {
    handle_method_call, handle_get_property, handle_set_property, {0}};

static void on_bus_acquired(GDBusConnection *connection, const gchar *name,
                            gpointer user_data) {
  (void)name;
  (void)user_data;

  g_connection = connection;
  g_object_ref(g_connection); /* Keep a ref */

  /* Register Objects */
  guint registration_id;
  GError *error = NULL;

  registration_id = g_dbus_connection_register_object(
      connection, "/org/mpris/MediaPlayer2",
      introspection_data->interfaces[0], /* org.mpris.MediaPlayer2 */
      &interface_vtable, NULL, NULL, &error);

  if (registration_id == 0) {
    log_message("ERROR", "MPRIS: Failed to register MediaPlayer2");
  }

  registration_id = g_dbus_connection_register_object(
      connection, "/org/mpris/MediaPlayer2",
      introspection_data->interfaces[1], /* org.mpris.MediaPlayer2.Player */
      &interface_vtable, NULL, NULL, &error);

  if (registration_id == 0) {
    log_message("ERROR", "MPRIS: Failed to register MediaPlayer2.Player");
  }

  log_message("INFO", "MPRIS: Bus acquired, objects registered.");
}

static void on_name_acquired(GDBusConnection *connection, const gchar *name,
                             gpointer user_data) {
  (void)connection;
  (void)name;
  (void)user_data;
  log_message("INFO", "MPRIS: Name acquired.");
}

static void on_name_lost(GDBusConnection *connection, const gchar *name,
                         gpointer user_data) {
  (void)connection;
  (void)name;
  (void)user_data;
  log_message("WARNING", "MPRIS: Name lost.");

  if (g_connection) {
    g_object_unref(g_connection);
    g_connection = NULL;
  }
}

/* --- Public API --- */

bool mpris_init(const MprisCallbacks *callbacks) {
  if (callbacks) {
    g_callbacks = *callbacks;
  }

  /* Initialize GMetadata default */
  update_metadata_variant(NULL, NULL, NULL, NULL, 0);

  /* Parse Introspection XML */
  GError *error = NULL;
  introspection_data = g_dbus_node_info_new_for_xml(introspection_xml, &error);
  if (!introspection_data) {
    return false;
  }

  owner_id =
      g_bus_own_name(G_BUS_TYPE_SESSION, "org.mpris.MediaPlayer2.harmony",
                     G_BUS_NAME_OWNER_FLAGS_NONE, on_bus_acquired,
                     on_name_acquired, on_name_lost, NULL, NULL);

  return (owner_id > 0);
}

void mpris_shutdown(void) {
  if (owner_id > 0) {
    g_bus_unown_name(owner_id);
    owner_id = 0;
  }
  if (introspection_data) {
    g_dbus_node_info_unref(introspection_data);
    introspection_data = NULL;
  }
  if (g_metadata) {
    g_variant_unref(g_metadata);
    g_metadata = NULL;
  }
  if (g_connection) {
    g_object_unref(g_connection);
    g_connection = NULL;
  }
}

void mpris_process(void) {
  /* Pump the GLib Main Context */
  while (g_main_context_iteration(NULL, FALSE))
    ;
}

static void emit_properties_changed(const char *interface_name,
                                    GVariant *changed_properties,
                                    const char *const *invalidated_properties) {
  if (g_connection) {
    // log_message("DEBUG", "Emitting PropertiesChanged for %s",
    // interface_name);
    g_dbus_connection_emit_signal(
        g_connection, NULL, "/org/mpris/MediaPlayer2",
        "org.freedesktop.DBus.Properties", "PropertiesChanged",
        g_variant_new("(s@a{sv}^as)", interface_name, changed_properties,
                      invalidated_properties ? invalidated_properties
                                             : (const char *[]){NULL}),
        NULL);
  }
}

void mpris_update_metadata(const char *title, const char *artist,
                           const char *album, const char *art_url,
                           int64_t duration_us) {
  update_metadata_variant(title, artist, album, art_url, duration_us);

  // log_message("DEBUG", "Updating Metadata: %s - %s", title ? title :
  // "Unknown", artist ? artist : "Unknown");

  /* Emit PropertyChanged signal */
  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
  g_variant_builder_add(&builder, "{sv}", "Metadata", g_metadata);

  emit_properties_changed("org.mpris.MediaPlayer2.Player",
                          g_variant_builder_end(&builder), NULL);
}

void mpris_update_playback_status(bool is_playing) {
  const char *new_status = is_playing ? "Playing" : "Paused";
  if (g_strcmp0(g_playback_status, new_status) != 0) {
    g_playback_status = (char *)new_status; /* String literal, safe to cast */

    // log_message("DEBUG", "Updating PlaybackStatus: %s", g_playback_status);

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "PlaybackStatus",
                          g_variant_new_string(g_playback_status));

    emit_properties_changed("org.mpris.MediaPlayer2.Player",
                            g_variant_builder_end(&builder), NULL);
  }
}

void mpris_update_position(int64_t position_us) {
  g_position = position_us;
  /* MPRIS says not to emit Position changes too often (e.g. not every second).
   */
  /* We'll only update the internal state for when Get() is called. */
  /* However, Seeked signal SHOULD be emitted on jumps. */
}
