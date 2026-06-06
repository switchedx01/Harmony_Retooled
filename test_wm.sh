#!/bin/bash
loginctl show-session $(awk '/tty/ {print $1}' <(loginctl)) -p Type | grep -q Wayland && echo "WAYLAND DETECTED" || echo "X11 DETECTED"
echo "$XDG_SESSION_TYPE"
