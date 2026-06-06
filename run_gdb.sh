#!/bin/bash
gdb --batch \
  -ex "break player_init" \
  -ex "run" \
  -ex "finish" \
  -ex "watch *(unsigned long *)((char*)app_get_player() + 2853352)" \
  -ex "continue" \
  ./harmony_player
