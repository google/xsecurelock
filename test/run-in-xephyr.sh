#!/bin/sh

export XSECURELOCK_SAVER=saver_xscreensaver

set -ex
make -C .. CPPFLAGS+=-DDEBUG_EVENTS clean all
startx \
  "$PWD"/../xsecurelock \
  -- \
  "$(which Xephyr)" :42 -retro -screen 800x480 -screen 1280x720 \
  +extension RANDR +xinerama -resizeable
make -C .. clean all
