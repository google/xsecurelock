#!/bin/sh

for test in *.xdo; do
  startx \
    /bin/sh "$PWD"/run-test.sh "$test" \
    -- \
    "$(which Xephyr)" :42 -retro -screen 800x480 -screen 1280x720 +xinerama
done
