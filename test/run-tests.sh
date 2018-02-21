#!/bin/sh

for test in *.xdo; do
  startx \
    /bin/sh "$PWD"/run-test.sh "$test" \
    -- \
    "$(which Xephyr)" :42 -retro -screen 640x480
done
