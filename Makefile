CC = gcc
# P10 Rule 10: All warnings enabled.
UNAME_S := $(shell uname -s)

# Include paths for new structure
CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -O3 \
         -Iinclude -Iinclude/core -Iinclude/ui -Iinclude/audio \
         -Iinclude/data -Iinclude/features -Iinclude/utils -Iinclude/visualizers \
         -Iinclude/vendor -D_POSIX_C_SOURCE=200809L
LDFLAGS = 

# SDL2 Configuration
CFLAGS += $(shell pkg-config --cflags sdl2 glib-2.0 gio-2.0)
LDFLAGS += $(shell pkg-config --libs sdl2 glib-2.0 gio-2.0)

# Platform specific
ifneq ($(UNAME_S),Darwin)
    LDFLAGS += -lsqlite3 -lm -ldl -lpthread -lzmq
endif

# Visualizer Plugins
VIS_SRCS = src/visualizers/vis_oscilloscope.c src/visualizers/vis_basic_wave.c src/visualizers/vis_pro_bars.c
VIS_PLUGINS = visualizers/vis_oscilloscope.so visualizers/vis_basic_wave.so visualizers/vis_pro_bars.so

# Shared flags for plugins
PLUGIN_CFLAGS = $(CFLAGS) -fPIC
PLUGIN_LDFLAGS = -shared

SRCS = src/main.c \
       src/core/app_context.c \
       src/core/command_dispatch.c \
       src/core/init.c \
       src/core/input_handler.c \
       src/utils/logging.c \
       src/utils/string_utils.c \
       src/utils/path_utils.c \
       src/utils/image_loader.c \
       src/utils/color_extractor.c \
       src/utils/cam16.c \
       src/utils/hct.c \
       src/utils/palette.c \
       src/utils/quantizer.c \
       src/utils/scheme.c \
       src/utils/color_utils.c \
       src/core/player.c \
       src/core/library_manager.c \
       src/core/visualizer_loader.c \
       src/data/database.c \
       src/audio/audio_backend.c \
       src/audio/visualizer_buffer.c \
       src/utils/ring_buffer.c \
       src/ui/window.c \
       src/ui/layout.c \
       src/ui/font_renderer.c \
       src/ui/material_renderer.c \
       src/ui/toast_overlay.c \
       src/ui/mini_player.c \
       src/visualizers/vis_basic_wave.c \
       src/utils/metadata_parser.c \
       src/utils/album_art.c \
       src/utils/ffmpeg_decoder.c \
       src/features/mpris_service.c \
       src/core/hub_client.c

# Object Files
SRCOBJS = $(SRCS:src/%.c=build/%.o)

BUILD_DIR = build
OBJS = $(SRCOBJS)
TARGET = harmony_player

.PHONY: all clean plugins

all: $(TARGET) plugins

plugins: $(VIS_PLUGINS)

visualizers/%.so: src/visualizers/%.c
	@mkdir -p visualizers
	$(CC) $(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS) $< -o $@

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful: $(TARGET)"

# Generic rule for building objects
# This handles subdirectories automatically if they exist in build/
build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET) visualizers/*.so
