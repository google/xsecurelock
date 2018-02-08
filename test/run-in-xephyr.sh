#!/bin/sh

make -C .. CPPFLAGS+=-DDEBUG_EVENTS clean all
startx \
  "$PWD"/../xsecurelock \
  -- \
  "$(which Xephyr)" :42 -retro -screen 800x480 -screen 1280x720 \
  +xinerama -resizeable
make -C .. clean all
