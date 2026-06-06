#!/bin/bash
set -e

echo "Installing Harmony Player..."

if [ -z "$PREFIX" ]; then
    if [ "$EUID" -ne 0 ]; then
        PREFIX="$HOME/.local"
        echo "Running as standard user. Installing to $PREFIX"
    else
        PREFIX="/usr/local"
        echo "Running as root. Installing to $PREFIX"
    fi
fi

BIN_DIR="$PREFIX/bin"
SHARE_DIR="$PREFIX/share"
APPS_DIR="$SHARE_DIR/applications"
ICONS_DIR="$SHARE_DIR/icons/hicolor/512x512/apps"

echo "Step 1: Creating directories in $PREFIX..."
mkdir -p "$BIN_DIR"
mkdir -p "$APPS_DIR"
mkdir -p "$ICONS_DIR"

echo "Step 2: Building Harmony Player..."
make clean
make -j$(nproc)

echo "Step 3: Installing binary..."
cp harmony_player "$BIN_DIR/harmony_player"
chmod 755 "$BIN_DIR/harmony_player"

echo "Step 4: Installing App Icon..."
if [ -f "assets/icons/harmony_icon.png" ]; then
    cp "assets/icons/harmony_icon.png" "$ICONS_DIR/harmony_player.png"
else
    echo "Warning: Icon 'assets/icons/harmony_icon.png' not found. It may be missing from the background."
fi

echo "Step 5: Installing Desktop Entry and File Associations..."
cat <<EOF > "$APPS_DIR/harmony_player.desktop"
[Desktop Entry]
Name=Harmony Player
Comment=A modern, sleek audio player
Exec=harmony_player %U
Icon=harmony_player
Terminal=false
Type=Application
Categories=AudioVideo;Audio;Player;
MimeType=audio/mpeg;audio/x-wav;audio/flac;audio/mp4;audio/x-m4a;audio/x-matroska;audio/ogg;
EOF

echo "Step 6: Updating Desktop and Icon Databases..."
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APPS_DIR" || true
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$SHARE_DIR/icons/hicolor" || true
fi

echo "----------------------------------------"
echo "Installation complete!"
echo "Harmony Player is now installed to $BIN_DIR."
echo "The player is now associated with audio files and available in your Multimedia app launcher."
echo "IMPORTANT: If you installed to ~/.local/bin, make sure ~/.local/bin is in your PATH."
