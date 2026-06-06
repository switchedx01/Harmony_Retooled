## Harmony Music Player (Retooled)

Harmony is a simple, beautiful, and straightforward music player for Linux. It's designed for a clean, no-gimmicks listening experience, focusing on what matters most: your music. The user interface dynamically adapts its colors to match the album art of the currently playing song, creating a seamless and immersive visual flow.

**This is the native C/SDL2 retooled version of the original Python player.**

# Main Features

- **Simple & Clean Interface**: No clutter, no distractions. Just you and your music.
- **Dynamic UI**: The player's background and elements change color to match the album art for a beautiful, cohesive look.
- **Audio Visualizers**: Built-in visualizations to accompany your music playback.
- **Standard Music Controls**: All the essential features you need: play, pause, skip, shuffle, and repeat, integrated into your platform's MPRIS service.
- **Library Management**: Powered by a robust local SQLite database to easily organize and access your music library.

# Technologies Used

- **C (C99)**: The core programming language.
- **SDL2**: Used for building the cross-platform GUI and hardware accelerated drawing.
- **miniaudio & stb**: Used for audio playback, font rendering, and image loading.
- **SQLite**: For fast, local data storage of your music library.
- **GLib / GIO**: For Linux MPRIS D-Bus integration.

# Installation Requirements

Before building, please ensure you have the following dependencies installed on your system.

On Ubuntu/Debian-based systems, run:
```bash
sudo apt-get update
sudo apt-get install build-essential gcc make pkg-config libsdl2-dev libglib2.0-dev libsqlite3-dev ffmpeg
```

> [!NOTE]
> `ffmpeg` is strictly required to convert and stream `.m4a`/AAC files. Ensure it is accessible from your system's PATH.

# Building & Installation

Getting Harmony Music Player set up is easy. Open a terminal in the project directory and run:

```bash
make
```

The Makefile will automatically compile the core executable (`harmony_player`) as well as any visualizer plugins into the local directory.

To install the application into your system so it's globally available to run from your launcher or terminal anywhere, we've provided an installation script:

```bash
sudo ./install.sh
```

# Usage

After a successful build or installation, open the application by running:

```bash
harmony
```

The first time you run it, you may be prompted to add your music library. Point the application to the folder where your music is stored, and Harmony will handle scanning and indexing.

# Contributing

Contributions are welcome! Whether you want to report a bug, suggest a feature, or write code, your help is appreciated.
Please use the Issues tab on GitHub to let me know about any problems or ideas you have. If you'd like to contribute code, open a Pull Request.

# License
This project is distributed under the MIT License. See the LICENSE file for more information.
